#include "net/BridgeClient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTimer>

#include "net/Channels.h"

namespace hmi::net {

using hmi::robot::DriveMode;
using hmi::robot::MissionState;

namespace {

/// 하트비트 주기. 명세 §5.
constexpr int kHeartbeatMs = 200;

/// 이 시간 동안 상대 하트비트가 없으면 링크가 죽은 것으로 표시한다.
/// 로봇을 정지시키는 3초 기준과는 별개다 — 그쪽은 로봇측 safety 노드가
/// 스스로 판단하며, 이 값은 화면 표시용이다.
constexpr qint64 kLinkSilentMs = 1500;

/// 위치가 이보다 오래되면 신선하지 않은 것으로 본다. 위치 등록과 지도
/// 표시가 이 값을 본다 — 끊긴 동안의 마지막 좌표를 사실처럼 쓰면 안 된다.
constexpr qint64 kPoseStaleMs = 1000;

/// 명령 응답을 기다리는 시간. 넘으면 사유를 로그에 남긴다.
constexpr qint64 kRequestTimeoutMs = 3000;

constexpr int kReconnectMinMs = 500;
constexpr int kReconnectMaxMs = 5000;

/// 송신 버퍼가 이보다 쌓이면 손실 허용 메시지를 버린다. 밀린 속도 명령을
/// 뒤늦게 보내는 것은 안 보내느니만 못하다.
constexpr qint64 kBackpressureBytes = 4 * 1024 * 1024;

constexpr int kTelemetryHz = 20;

QList<QPointF> pointsFrom(const QJsonArray &arr)
{
    QList<QPointF> out;
    out.reserve(arr.size());
    for (const auto &v : arr) {
        const QJsonArray pair = v.toArray();
        if (pair.size() >= 2)
            out << QPointF(pair.at(0).toDouble(), pair.at(1).toDouble());
    }
    return out;
}

}  // namespace

BridgeClient::BridgeClient(QString host, quint16 port, QObject *parent)
    : hmi::robot::RobotLink(parent), host_(std::move(host)), port_(port)
{
    clock_.start();

    socket_ = new QTcpSocket(this);
    connect(socket_, &QTcpSocket::connected, this, &BridgeClient::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &BridgeClient::onDisconnected);
    connect(socket_, &QTcpSocket::errorOccurred, this, &BridgeClient::onSocketError);
    connect(socket_, &QTcpSocket::readyRead, this, &BridgeClient::onReadyRead);

    heartbeatTimer_ = new QTimer(this);
    heartbeatTimer_->setInterval(kHeartbeatMs);
    connect(heartbeatTimer_, &QTimer::timeout, this, [this] {
        sendEnvelope(makeHeartbeat(++heartbeatSeq_));
        heartbeatSentAt_.insert(heartbeatSeq_, clock_.elapsed());
    });

    watchdogTimer_ = new QTimer(this);
    watchdogTimer_->setInterval(250);
    connect(watchdogTimer_, &QTimer::timeout, this, &BridgeClient::checkTimeouts);
    watchdogTimer_->start();

    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, [this] {
        if (wantConnection_)
            socket_->connectToHost(host_, port_);
    });

    telemetryTimer_ = new QTimer(this);
    telemetryTimer_->setInterval(1000 / kTelemetryHz);
    connect(telemetryTimer_, &QTimer::timeout, this, &BridgeClient::emitTelemetry);
    telemetryTimer_->start();
}

BridgeClient::~BridgeClient() = default;

QString BridgeClient::describe() const
{
    // 식별자를 알면 그것을 보여준다. 주소보다 "어느 로봇인가" 가 먼저다.
    return robotId_.isEmpty() ? QStringLiteral("%1:%2").arg(host_).arg(port_)
                              : robotId_;
}

bool BridgeClient::isConnected() const
{
    return socket_->state() == QAbstractSocket::ConnectedState;
}

// ================= 연결 수명 =================

void BridgeClient::connectToBridge()
{
    wantConnection_ = true;
    reconnectDelayMs_ = kReconnectMinMs;
    if (socket_->state() == QAbstractSocket::UnconnectedState)
        socket_->connectToHost(host_, port_);
}

void BridgeClient::disconnectFromBridge()
{
    // 의도적인 종료다. 자동으로 되살아나면 안 된다.
    wantConnection_ = false;
    reconnectTimer_->stop();
    socket_->abort();
}

void BridgeClient::onConnected()
{
    // Nagle 을 끈다. 하트비트와 조작 명령은 작고 지연에 민감해서,
    // 뭉쳐 보내면 수십 ms 가 붙는다 (명세 §1.0).
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    socket_->setSocketOption(QAbstractSocket::KeepAliveOption, 1);

    decoder_.reset();          // 이전 연결의 잔여 바이트를 버린다 (§1.3-6)
    reconnectDelayMs_ = kReconnectMinMs;
    lastHeartbeatMs_ = clock_.elapsed();

    QStringList channels;
    for (const auto *ch : {hmi::ch::kPose, hmi::ch::kBattery, hmi::ch::kSystem,
                           hmi::ch::kSafety, hmi::ch::kNav, hmi::ch::kPlan,
                           hmi::ch::kTrail, hmi::ch::kArm, hmi::ch::kApriltag,
                           hmi::ch::kMission, hmi::ch::kWaypoints, hmi::ch::kLocations,
                           hmi::ch::kMap,
                           hmi::ch::kPreview, hmi::ch::kCaptureSpool,
                           hmi::ch::kHealth})
        channels << QLatin1String(ch);
    sendEnvelope(makeSubscribe(channels));

    heartbeatTimer_->start();

    // 붙자마자 전원 정책을 다시 보낸다. 로봇이 재부팅했으면 정책을 잊었을
    // 수 있고, 그 상태로 두면 관제 화면에 적힌 값과 로봇이 지키는 값이
    // 갈라진다 — 눈에 보이지 않는 차이라 사고 뒤에야 드러난다.
    if (batteryReturnAt_ > 0.0)
        setBatteryPolicy(batteryReturnAt_, batteryDepartAt_);

    emit connectionChanged(true);
    // 첫 연결과 재연결을 구분한다. 처음 붙는 것을 "재연결됨" 이라고 하면
    // 조작자가 직전에 무슨 문제가 있었나 하고 로그를 뒤진다.
    emit robotEvent(everConnected_ ? QStringLiteral("LINK_RESTORED")
                                   : QStringLiteral("LINK_ESTABLISHED"),
                    {{"peer", describe()}});
    everConnected_ = true;
}

void BridgeClient::onDisconnected()
{
    heartbeatTimer_->stop();
    resetLinkState();
    emit connectionChanged(false);
    // 한 번도 붙은 적 없이 끊긴 것은 "연결 끊김" 이 아니라 연결 실패다.
    if (everConnected_)
        emit robotEvent(QStringLiteral("LINK_LOST"), {{"peer", describe()}});
    scheduleReconnect();
}

void BridgeClient::onSocketError()
{
    if (socket_->state() != QAbstractSocket::ConnectedState)
        scheduleReconnect();
}

void BridgeClient::resetLinkState()
{
    // 끊기면 식별자도 잊는다. 다시 붙은 상대가 같은 로봇이라는 보장이 없다.
    robotId_.clear();
    decoder_.reset();
    pending_.clear();
    heartbeatSentAt_.clear();
    lastSeq_.clear();
    // 위치를 즉시 오래된 것으로 표시한다. 끊긴 뒤에도 마지막 좌표가
    // 사실처럼 남아 있으면 위치 등록이 그 값을 저장해 버린다.
    telemetry_.poseFresh = false;
    ++telemetry_.link.reconnects;
    telemetry_.link.connected = false;
}

void BridgeClient::scheduleReconnect()
{
    if (!wantConnection_ || reconnectTimer_->isActive())
        return;
    reconnectTimer_->start(reconnectDelayMs_);
    reconnectDelayMs_ = qMin(reconnectDelayMs_ * 2, kReconnectMaxMs);
}

// ================= 송신 =================

void BridgeClient::sendEnvelope(const Envelope &env)
{
    if (!isConnected())
        return;

    // 아는 식별자를 실어 보낸다. 로봇 쪽에서도 자기 앞으로 온 명령인지
    // 확인할 수 있어야, 나중에 한 대가 여러 관제를 상대하게 되어도 규약을
    // 바꾸지 않는다.
    Envelope out = env;
    if (out.robot.isEmpty())
        out.robot = robotId_;

    const QByteArray wire = encodeFrame(out.toHeader(), out.payload);
    socket_->write(wire);
    txBytes_ += wire.size();
}

void BridgeClient::sendRequest(const QString &channel, const QJsonObject &payload)
{
    if (!isConnected()) {
        emit robotEvent(QStringLiteral("LINK_LOST"),
                        {{"reason", QStringLiteral("연결되지 않아 명령을 보내지 못했습니다")},
                         {"channel", channel}});
        return;
    }
    const Envelope env = makeRequest(channel, payload);
    pending_.insert(env.id, {channel, clock_.elapsed()});
    sendEnvelope(env);
}

void BridgeClient::publish(const QString &channel, const QJsonObject &payload)
{
    if (!isConnected())
        return;
    // 송신이 밀렸으면 버린다. 손실 허용 채널이고, 지난 속도 명령을
    // 뒤늦게 전달하는 것은 위험하기까지 하다.
    if (socket_->bytesToWrite() > kBackpressureBytes)
        return;
    sendEnvelope(makePublish(channel, payload));
}

// ================= 수신 =================

void BridgeClient::onReadyRead()
{
    const QByteArray chunk = socket_->readAll();
    rxBytes_ += chunk.size();
    decoder_.append(chunk);

    // 한 번의 readyRead 에 여러 프레임이 올 수 있다. 버퍼가 마를 때까지
    // 돌린다 (명세 §1.3-2).
    Frame frame;
    for (;;) {
        const auto status = decoder_.next(frame);
        if (status == FrameDecoder::Status::NeedMore)
            return;
        if (status != FrameDecoder::Status::Ok) {
            // 스트림 정합이 깨졌다. 재동기화를 시도하지 않고 끊는다 (§1.3-3).
            ++telemetry_.link.decodeErrors;
            emit robotEvent(status == FrameDecoder::Status::BadMagic
                                ? QStringLiteral("FRAME_BAD_MAGIC")
                                : QStringLiteral("FRAME_TOO_LARGE"),
                            {{"peer", describe()}});
            socket_->abort();
            return;
        }
        handleFrame(frame);
    }
}

void BridgeClient::handleFrame(const Frame &frame)
{
    QString err;
    const auto env = Envelope::fromHeader(frame.header, &err);
    if (!env) {
        // 버전 불일치를 포함한다. 어긋난 채로 운용하는 것이 가장 위험하므로
        // 끊는다 (명세 §6).
        emit robotEvent(QStringLiteral("E_VERSION"), {{"detail", err}});
        socket_->abort();
        return;
    }

    Envelope e = *env;
    e.payload = frame.payload;

    // 처음 들은 로봇 식별자를 고정하고, 같은 연결에서 다른 식별자가 오면
    // 끊는다. 주소를 잘못 적어 옆 로봇에 붙어도 화면은 정상으로 보이는데,
    // 그 상태로 비상정지를 누르면 엉뚱한 로봇이 선다.
    if (!e.robot.isEmpty()) {
        if (robotId_.isEmpty()) {
            robotId_ = e.robot;
        } else if (robotId_ != e.robot) {
            emit robotEvent(QStringLiteral("E_ROBOT_MISMATCH"),
                            {{"expected", robotId_}, {"received", e.robot}});
            socket_->abort();
            return;
        }
    }

    if (e.t == QLatin1String(mtype::kHb))
        handleHeartbeat(e);
    else if (e.t == QLatin1String(mtype::kRes))
        handleResponse(e);
    else if (e.t == QLatin1String(mtype::kPub) || e.t == QLatin1String(mtype::kEvt))
        handlePublish(e);
}

void BridgeClient::handleHeartbeat(const Envelope &env)
{
    lastHeartbeatMs_ = clock_.elapsed();
    const qint64 seq = qint64(env.p.value(QStringLiteral("seq")).toDouble());
    const auto it = heartbeatSentAt_.constFind(seq);
    if (it != heartbeatSentAt_.constEnd()) {
        telemetry_.link.rttMs = double(clock_.elapsed() - *it);
        telemetry_.rtt = telemetry_.link.rttMs;
        heartbeatSentAt_.remove(seq);
    }
}

void BridgeClient::handleResponse(const Envelope &env)
{
    pending_.remove(env.id);
    if (env.p.value(QStringLiteral("ok")).toBool())
        return;

    // 거부된 명령은 반드시 로그에 남는다. "왜 로봇이 안 움직이지" 의 답이
    // 대부분 여기 있다.
    const QJsonObject err = env.p.value(QStringLiteral("err")).toObject();
    const QString code = err.value(QStringLiteral("code")).toString();
    emit robotEvent(code.isEmpty() ? QStringLiteral("E_BAD_PAYLOAD") : code,
                    {{"channel", env.ch},
                     {"msg", err.value(QStringLiteral("msg")).toString()}});
}

void BridgeClient::handlePublish(const Envelope &env)
{
    // 스트림 유실 검출. 손실 허용이므로 약간은 정상이지만 급증은 신호다.
    if (env.seq.has_value()) {
        const auto prev = lastSeq_.constFind(env.ch);
        if (prev != lastSeq_.constEnd() && *env.seq > *prev + 1)
            telemetry_.link.seqGaps += int(*env.seq - *prev - 1);
        lastSeq_.insert(env.ch, *env.seq);
    }

    const QJsonObject &p = env.p;
    const QString &ch = env.ch;

    if (ch == QLatin1String(hmi::ch::kPose)) {
        telemetry_.x = p.value(QStringLiteral("x")).toDouble();
        telemetry_.y = p.value(QStringLiteral("y")).toDouble();
        telemetry_.theta = p.value(QStringLiteral("theta")).toDouble();
        telemetry_.speed = p.value(QStringLiteral("speed")).toDouble();
        lastPoseMs_ = clock_.elapsed();
        telemetry_.poseFresh = true;
    } else if (ch == QLatin1String(hmi::ch::kBattery)) {
        telemetry_.soc = p.value(QStringLiteral("soc")).toDouble();
    } else if (ch == QLatin1String(hmi::ch::kSystem)) {
        telemetry_.cpu = p.value(QStringLiteral("cpu_pct")).toDouble();
        telemetry_.gpu = p.value(QStringLiteral("gpu_pct")).toDouble();
        telemetry_.mem = p.value(QStringLiteral("mem_pct")).toDouble();
        telemetry_.cpuTemp = p.value(QStringLiteral("cpu_temp_c")).toDouble();
        telemetry_.gpuTemp = p.value(QStringLiteral("gpu_temp_c")).toDouble();
    } else if (ch == QLatin1String(hmi::ch::kSafety)) {
        const bool estop = p.value(QStringLiteral("estop")).toBool();
        if (estop != estop_) {
            estop_ = estop;
            telemetry_.estop = estop;
        }
        const QString mode = p.value(QStringLiteral("mode")).toString();
        if (!mode.isEmpty())
            mode_ = mode == QLatin1String("manual") ? DriveMode::Manual : DriveMode::Auto;
        telemetry_.localizationOk = !p.value(QStringLiteral("localization_degraded")).toBool();
    } else if (ch == QLatin1String(hmi::ch::kNav)) {
        telemetry_.navStatus = p.value(QStringLiteral("status")).toString();
    } else if (ch == QLatin1String(hmi::ch::kPlan)) {
        telemetry_.plan = pointsFrom(p.value(QStringLiteral("points")).toArray());
    } else if (ch == QLatin1String(hmi::ch::kTrail)) {
        const auto pts = pointsFrom(p.value(QStringLiteral("points")).toArray());
        if (p.value(QStringLiteral("reset")).toBool())
            telemetry_.trail = pts;
        else
            telemetry_.trail += pts;
        // 무한히 자라지 않게 자른다. 오래된 궤적은 표시 가치가 없다.
        if (telemetry_.trail.size() > 4000)
            telemetry_.trail = telemetry_.trail.mid(telemetry_.trail.size() - 4000);
    } else if (ch == QLatin1String(hmi::ch::kArm)) {
        QList<double> q;
        for (const auto &v : p.value(QStringLiteral("positions")).toArray())
            q << v.toDouble();
        telemetry_.joints = q;
        telemetry_.manipulability = p.value(QStringLiteral("manipulability")).toDouble();
        telemetry_.sigmaMin = p.value(QStringLiteral("sigma_min")).toDouble();
        const QString state = p.value(QStringLiteral("moveit_state")).toString();
        telemetry_.armState = state.isEmpty() ? QStringLiteral("idle") : state;
    } else if (ch == QLatin1String(hmi::ch::kApriltag)) {
        QSet<int> seen;
        for (const auto &v : p.value(QStringLiteral("tags")).toArray())
            seen.insert(v.toObject().value(QStringLiteral("id")).toInt());
        telemetry_.seenTags = seen;
    } else if (ch == QLatin1String(hmi::ch::kMission)) {
        const QString state = p.value(QStringLiteral("state")).toString();
        const MissionState next = state == QLatin1String("running") ? MissionState::Running
                                  : state == QLatin1String("paused") ? MissionState::Paused
                                                                     : MissionState::Idle;
        if (next != mission_) {
            mission_ = next;
            emit missionStateChanged(mission_);
        }
    } else if (ch == QLatin1String(hmi::ch::kWaypoints)) {
        QList<QVariantMap> wps;
        for (const auto &v : p.value(QStringLiteral("points")).toArray())
            wps << v.toObject().toVariantMap();
        waypoints_ = wps;
    } else if (ch == QLatin1String(hmi::ch::kLocations)) {
        // 충전소와 시작점은 순회 목록에 들어가지 않는다. 로봇이 스스로
        // 복귀할 때 쓰는 자리라 로봇이 말해 주는 것이 원본이다.
        for (const auto &v : p.value(QStringLiteral("locations")).toArray()) {
            const QVariantMap loc = v.toObject().toVariantMap();
            const QString kind = loc.value(QStringLiteral("kind")).toString();
            if (kind == QLatin1String("dock"))
                dock_ = loc;
            else if (kind == QLatin1String("home"))
                home_ = loc;
        }
    } else if (ch == QLatin1String(hmi::ch::kMarkers)) {
        QList<QVariantMap> ms;
        for (const auto &v : p.value(QStringLiteral("markers")).toArray())
            ms << v.toObject().toVariantMap();
        markers_ = ms;
    } else if (ch == QLatin1String(hmi::ch::kCaptureSpool)) {
        telemetry_.nasOnline = p.value(QStringLiteral("nas_online")).toBool();
        telemetry_.pendingUploads = p.value(QStringLiteral("pending")).toInt();
        telemetry_.spoolFreeMb = p.value(QStringLiteral("spool_free_mb")).toDouble();
    } else if (ch == QLatin1String(hmi::ch::kHealth)) {
        QList<hmi::ui::SensorHealth> sensors;
        for (const auto &v : p.value(QStringLiteral("sensors")).toArray()) {
            const QJsonObject o = v.toObject();
            hmi::ui::SensorHealth h;
            h.id = o.value(QStringLiteral("id")).toString();
            h.name = o.value(QStringLiteral("name")).toString();
            h.expectedHz = o.value(QStringLiteral("expected_hz")).toDouble();
            h.actualHz = o.value(QStringLiteral("actual_hz")).toDouble();
            h.lastSeenMs = qint64(o.value(QStringLiteral("last_seen_ms")).toDouble());
            h.state = o.value(QStringLiteral("state")).toString();
            h.detail = o.value(QStringLiteral("detail")).toString();
            sensors << h;
        }
        telemetry_.sensors = sensors;

        const QJsonObject link = p.value(QStringLiteral("link")).toObject();
        telemetry_.link.rssiDbm = link.value(QStringLiteral("rssi_dbm")).toDouble();
    } else if (ch == QLatin1String(hmi::ch::kMap)) {
        emit mapReceived(env.payload, p);
    } else if (ch == QLatin1String(hmi::ch::kLog)) {
        emit robotEvent(p.value(QStringLiteral("code")).toString(), p.toVariantMap());
    }
}

// ================= 감시 =================

void BridgeClient::checkTimeouts()
{
    const qint64 now = clock_.elapsed();

    // 위치 신선도. 링크가 살아 있어도 pose 만 끊길 수 있다.
    if (telemetry_.poseFresh && now - lastPoseMs_ > kPoseStaleMs)
        telemetry_.poseFresh = false;

    if (isConnected() && now - lastHeartbeatMs_ > kLinkSilentMs) {
        emit robotEvent(QStringLiteral("HEARTBEAT_TIMEOUT"),
                        {{"silent_ms", now - lastHeartbeatMs_}});
        lastHeartbeatMs_ = now;   // 매 주기마다 반복해서 쏟아내지 않는다
    }

    // 응답 없는 명령. 조용히 사라지면 조작자는 명령이 먹은 줄 안다.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->sentAtMs > kRequestTimeoutMs) {
            emit robotEvent(QStringLiteral("E_BUSY"),
                            {{"channel", it->channel},
                             {"msg", QStringLiteral("응답 없음 (%1 ms 초과)")
                                         .arg(kRequestTimeoutMs)}});
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }

    // 초당 바이트. 1 초마다 누적을 비율로 환산한다.
    if (now - lastThroughputMs_ >= 1000) {
        const double secs = double(now - lastThroughputMs_) / 1000.0;
        telemetry_.link.rxBytesPerS = rxBytes_ / secs;
        telemetry_.link.txBytesPerS = txBytes_ / secs;
        rxBytes_ = txBytes_ = 0;
        lastThroughputMs_ = now;
    }
}

void BridgeClient::emitTelemetry()
{
    telemetry_.link.connected = isConnected();
    emit telemetry(telemetry_);
}

// ================= 명령 =================

void BridgeClient::setCmdVel(double vx, double vy, double wz)
{
    // 발행이 멈추면 브릿지가 300 ms 데드맨으로 0 을 래치한다. 여기서 별도
    // 정지 명령을 보내지 않는 것은 그 계약을 신뢰한다는 뜻이다.
    publish(QLatin1String(hmi::ch::kCmdVel),
            {{"vx", vx}, {"vy", vy}, {"wz", wz}});
}

void BridgeClient::requestGoal(double x, double y, double theta)
{
    sendRequest(QLatin1String(hmi::ch::kCmdGoto), {{"x", x}, {"y", y}, {"theta", theta}});
}

void BridgeClient::cancelNav()
{
    sendRequest(QLatin1String(hmi::ch::kCmdNavCancel));
}

void BridgeClient::setWaypoints(const QList<QVariantMap> &waypoints)
{
    waypoints_ = waypoints;
    QJsonArray arr;
    for (const auto &w : waypoints)
        arr.append(QJsonObject::fromVariantMap(w));
    sendRequest(QLatin1String(hmi::ch::kCmdWaypointsSet), {{"points", arr}});
}

void BridgeClient::setLocations(const QList<QVariantMap> &locations)
{
    QJsonArray arr;
    for (const auto &loc : locations) {
        arr.append(QJsonObject::fromVariantMap(loc));
        const QString kind = loc.value(QStringLiteral("kind")).toString();
        if (kind == QLatin1String("dock"))
            dock_ = loc;
        else if (kind == QLatin1String("home"))
            home_ = loc;
    }
    sendRequest(QLatin1String(hmi::ch::kCmdLocationsSet), {{"locations", arr}});
}

void BridgeClient::setMarkers(const QList<QVariantMap> &markers)
{
    // 화면 값을 먼저 갱신한다. 로봇이 되돌려 줄 때까지 기다리면 방금 찍은
    // 마커가 잠깐 사라졌다 나타나 조작자가 실패한 줄 안다.
    markers_ = markers;
    QJsonArray arr;
    for (const auto &m : markers)
        arr.append(QJsonObject::fromVariantMap(m));
    sendRequest(QLatin1String(hmi::ch::kCmdMarkersSet), {{"markers", arr}});
}

void BridgeClient::setBatteryPolicy(double returnAt, double departAt)
{
    // 값을 들고 있다가 재연결 때 다시 보낸다. 로봇이 재부팅하면 관제 화면에
    // 적힌 값과 로봇이 지키는 값이 갈라지는데, 그 차이는 눈에 보이지 않는다.
    batteryReturnAt_ = returnAt;
    batteryDepartAt_ = departAt;
    sendRequest(QLatin1String(hmi::ch::kCmdPowerPolicy),
                {{"return_at", returnAt}, {"depart_at", departAt}});
}

void BridgeClient::missionStart()
{
    sendRequest(QLatin1String(hmi::ch::kCmdMissionStart), {{"from_index", 0}});
}

void BridgeClient::missionPause()
{
    sendRequest(QLatin1String(hmi::ch::kCmdMissionPause));
}

void BridgeClient::missionResume()
{
    sendRequest(QLatin1String(hmi::ch::kCmdMissionResume));
}

void BridgeClient::missionStop()
{
    sendRequest(QLatin1String(hmi::ch::kCmdMissionStop));
}

void BridgeClient::engageEstop()
{
    // 다른 명령과 달리 연결 상태를 확인하지 않고 시도한다. 링크가 이미
    // 끊겼다면 로봇측 safety 노드가 3 초 규칙으로 스스로 정지한다.
    sendRequest(QLatin1String(hmi::ch::kCmdEstop));
    estop_ = true;
    telemetry_.estop = true;
}

void BridgeClient::releaseEstop()
{
    sendRequest(QLatin1String(hmi::ch::kCmdEstopRelease), {{"confirm", true}});
}

void BridgeClient::setMode(DriveMode mode)
{
    mode_ = mode;
    sendRequest(QLatin1String(hmi::ch::kCmdMode),
                {{"mode", mode == DriveMode::Manual ? QStringLiteral("manual")
                                                    : QStringLiteral("auto")}});
}

void BridgeClient::setArmJointGoal(const QList<double> &q)
{
    QJsonArray arr;
    for (double v : q)
        arr.append(v);
    sendRequest(QLatin1String(hmi::ch::kCmdArmJointGoal), {{"positions", arr}});
}

void BridgeClient::setArmPreset(const QString &name)
{
    sendRequest(QLatin1String(hmi::ch::kCmdArmPreset), {{"name", name}});
}

void BridgeClient::stopArm()
{
    sendRequest(QLatin1String(hmi::ch::kCmdArmStop));
}

}  // namespace hmi::net
