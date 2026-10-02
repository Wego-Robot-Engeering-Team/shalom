// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

// BridgeClient tests against a mock bridge.
//
// A real TCP server is stood up on a loopback port and speaks the protocol
// back at the client. Without this the client's behaviour would be unknown
// until the robot arrives, and the failures that matter - a dropped link, a
// desynchronised stream, a command that never gets a reply - are exactly the
// ones that are inconvenient to reproduce on real hardware.
//
// What is pinned here:
//   - subscribing on connect, and heartbeating
//   - assembling per-channel messages into one telemetry snapshot
//   - a rejected command surfacing as an event rather than vanishing
//   - a corrupted stream closing the connection instead of resynchronising
//   - the pose being marked stale when it stops arriving
//   - reconnecting after the peer disappears

#include <QJsonArray>
#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTest>

#include <cmath>
#include <algorithm>

#include "net/BridgeClient.h"
#include "net/Channels.h"

using namespace hmi::net;
using hmi::robot::Telemetry;

namespace {

/// 프로토콜을 되받아 말하는 최소 서버.
class MockBridge : public QObject {
    Q_OBJECT
public:
    explicit MockBridge(QObject *parent = nullptr) : QObject(parent)
    {
        server_ = new QTcpServer(this);
        connect(server_, &QTcpServer::newConnection, this, [this] {
            peer_ = server_->nextPendingConnection();
            connect(peer_, &QTcpSocket::readyRead, this, &MockBridge::onReadyRead);
            emit clientConnected();
        });
        server_->listen(QHostAddress::LocalHost, 0);
        udp_ = new QUdpSocket(this);
        udp_->bind(QHostAddress::LocalHost, server_->serverPort());
        connect(udp_, &QUdpSocket::readyRead, this, [this] {
            while (udp_->hasPendingDatagrams()) {
                QByteArray bytes;
                bytes.resize(int(udp_->pendingDatagramSize()));
                udp_->readDatagram(bytes.data(), bytes.size());
                udpPackets << bytes;
            }
        });
    }

    quint16 port() const { return server_->serverPort(); }
    bool hasPeer() const { return peer_ && peer_->state() == QAbstractSocket::ConnectedState; }

    void send(const Envelope &env)
    {
        if (hasPeer())
            peer_->write(encodeFrame(env.toHeader(), env.payload));
    }

    void sendRaw(const QByteArray &bytes)
    {
        if (hasPeer())
            peer_->write(bytes);
    }

    void dropPeer()
    {
        if (peer_)
            peer_->abort();
    }

    void stopListening() { server_->close(); }
    void resumeListening() { server_->listen(QHostAddress::LocalHost, port_); }
    void rememberPort() { port_ = server_->serverPort(); }

    QList<Envelope> received;
    QList<QByteArray> udpPackets;
    bool echoHeartbeats = true;

signals:
    void clientConnected();
    void gotEnvelope(const QString &type, const QString &channel);

private:
    void onReadyRead()
    {
        decoder_.append(peer_->readAll());
        Frame f;
        while (decoder_.next(f) == FrameDecoder::Status::Ok) {
            if (const auto env = Envelope::fromHeader(f.header)) {
                received << *env;
                if (echoHeartbeats && env->t == QLatin1String(mtype::kHb))
                    send(makeHeartbeat(qint64(env->p.value(QStringLiteral("seq")).toDouble())));
                emit gotEnvelope(env->t, env->ch);
            }
        }
    }

    QTcpServer *server_ = nullptr;
    QUdpSocket *udp_ = nullptr;
    QTcpSocket *peer_ = nullptr;
    FrameDecoder decoder_;
    quint16 port_ = 0;
};

/// 조건이 참이 될 때까지 이벤트 루프를 돌린다. 고정 대기보다 빠르고
/// 느린 머신에서도 흔들리지 않는다.
template <typename Predicate>
bool waitFor(Predicate pred, int timeoutMs = 3000)
{
    QElapsedTimer t;
    t.start();
    while (!pred()) {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

Envelope pub(const char *channel, const QJsonObject &payload)
{
    return makePublish(QLatin1String(channel), payload);
}

}  // namespace

class TestBridge : public QObject {
    Q_OBJECT

private:
    MockBridge *server_ = nullptr;
    BridgeClient *client_ = nullptr;

    void connectPair()
    {
        server_ = new MockBridge(this);
        client_ = new BridgeClient(QStringLiteral("127.0.0.1"), server_->port(), this);
        client_->connectToBridge();
        QVERIFY(waitFor([this] { return client_->isConnected() && server_->hasPeer(); }));
    }

private slots:

    void generalLinkLossStopsSafetyHeartbeatButKeepsEstopCommands()
    {
        QTcpServer safetyServer;
        for (int attempt = 0; attempt < 20; ++attempt) {
            delete server_;
            server_ = new MockBridge(this);
            if (server_->port() < 65535 &&
                safetyServer.listen(QHostAddress::LocalHost, quint16(server_->port() + 1)))
                break;
        }
        QVERIFY(safetyServer.isListening());
        server_->rememberPort();
        QTcpSocket *safetyPeer = nullptr;
        FrameDecoder decoder;
        QList<Envelope> safetyMessages;
        connect(&safetyServer, &QTcpServer::newConnection, &safetyServer, [&] {
            safetyPeer = safetyServer.nextPendingConnection();
            connect(safetyPeer, &QTcpSocket::readyRead, &safetyServer, [&] {
                decoder.append(safetyPeer->readAll());
                Frame frame;
                while (decoder.next(frame) == FrameDecoder::Status::Ok)
                    if (const auto env = Envelope::fromHeader(frame.header))
                        safetyMessages << *env;
            });
        });
        client_ = new BridgeClient(QStringLiteral("127.0.0.1"), server_->port(), this);
        client_->connectToBridge();
        const auto heartbeatCount = [&] {
            return std::count_if(safetyMessages.cbegin(), safetyMessages.cend(),
                [](const Envelope &env) { return env.t == QLatin1String(mtype::kHb); });
        };
        QVERIFY(waitFor([&] { return client_->isConnected() && heartbeatCount() >= 2; }));
        server_->stopListening();
        server_->dropPeer();
        QVERIFY(waitFor([&] { return !client_->isConnected(); }));
        QTest::qWait(100);  // Drain heartbeat bytes written before the disconnect.
        const auto stoppedAt = heartbeatCount();
        QTest::qWait(1100);
        QCOMPARE(heartbeatCount(), stoppedAt);
        QVERIFY(safetyPeer && safetyPeer->state() == QAbstractSocket::ConnectedState);
        client_->engageEstop();
        QVERIFY(waitFor([&] {
            return std::any_of(safetyMessages.cbegin(), safetyMessages.cend(),
                [](const Envelope &env) { return env.ch == QLatin1String(hmi::ch::kCmdEstop); });
        }));
        server_->resumeListening();
        QVERIFY(waitFor([&] { return client_->isConnected() && heartbeatCount() > stoppedAt; }, 5000));
        client_->disconnectFromBridge();
    }

    void missionReasonChangesAreReportedWithoutAnFsmTransition()
    {
        connectPair();
        QSignalSpy states(client_, &BridgeClient::missionStateChanged);
        QSignalSpy statuses(client_, &BridgeClient::missionStatusReported);
        Telemetry latest;
        connect(client_, &BridgeClient::telemetry, this, [&](const Telemetry &tm) { latest = tm; });
        server_->send(pub(hmi::ch::kMission, {{"state", "ready"},
            {"reason_code", "MISSION_START_REQUESTED"}, {"detail", "Waiting for base"}}));
        QVERIFY(waitFor([&] { return statuses.size() == 1; }));
        server_->send(pub(hmi::ch::kMission, {{"state", "ready"},
            {"reason_code", "MISSION_START_CANCELLED_BY_SAFETY"}, {"detail", "Safety fault"}}));
        QVERIFY(waitFor([&] { return statuses.size() == 2 &&
            latest.missionReasonCode == QLatin1String("MISSION_START_CANCELLED_BY_SAFETY"); }));
        QCOMPARE(states.size(), 1);
        QCOMPARE(latest.missionDetail, QStringLiteral("Safety fault"));
        server_->send(pub(hmi::ch::kMission, {{"state", "ready"},
            {"reason_code", "MISSION_START_CANCELLED_BY_SAFETY"}, {"detail", "Safety fault"}}));
        QTest::qWait(80);
        QCOMPARE(statuses.size(), 2);
    }

    void safetyStopAndSourceFreshnessAreRetained()
    {
        connectPair();
        Telemetry latest;
        connect(client_, &BridgeClient::telemetry, this, [&](const Telemetry &tm) { latest = tm; });
        server_->send(pub(hmi::ch::kSafety, {{"mode", "auto"}, {"estop", false},
            {"state", "controlled_stop"}, {"motion_permitted", false}, {"state_fresh", true},
            {"reason_code", "HMI_LINK_LOST"}, {"detail", "General control link lost"}}));
        QVERIFY(waitFor([&] { return latest.safetyFresh; }));
        QCOMPARE(latest.safetyState, QStringLiteral("controlled_stop"));
        QVERIFY(!latest.safetyMotionPermitted);
        QCOMPARE(latest.safetyReasonCode, QStringLiteral("HMI_LINK_LOST"));
        QCOMPARE(latest.safetyDetail, QStringLiteral("General control link lost"));
        server_->send(pub(hmi::ch::kSafety, {{"mode", "auto"}, {"estop", false},
            {"state", "normal"}, {"motion_permitted", true}, {"state_fresh", false}}));
        QVERIFY(waitFor([&] { return latest.safetyState == QLatin1String("normal"); }));
        QVERIFY(!latest.safetyFresh);
    }

    void silentGeneralConnectionIsClosed()
    {
        connectPair();
        server_->echoHeartbeats = false;
        server_->stopListening();
        QSignalSpy events(client_, &BridgeClient::robotEvent);
        QVERIFY(waitFor([&] { return !client_->isConnected(); }, 2500));
        bool timeoutReported = false;
        for (const auto &event : events)
            timeoutReported |= event.at(0).toString() == QLatin1String("LINK_HEARTBEAT_TIMEOUT");
        QVERIFY(timeoutReported);
    }

    void unchangedEstopReportSurvivesReconnectAndExpires()
    {
        connectPair();
        Telemetry latest;
        connect(client_, &BridgeClient::telemetry, this, [&](const Telemetry &tm) { latest = tm; });
        server_->send(pub(hmi::ch::kSafety, {{"mode", "auto"}, {"estop", true}}));
        QVERIFY(waitFor([&] { return latest.safetyFresh && latest.estop; }));
        client_->disconnectFromBridge();
        QVERIFY(waitFor([&] { return !client_->isConnected() && !latest.safetyFresh; }));
        QVERIFY(latest.estop);  // The last confirmed E-Stop never becomes a false release.
        client_->connectToBridge();
        QVERIFY(waitFor([&] { return client_->isConnected() && server_->hasPeer(); }));
        server_->send(pub(hmi::ch::kSafety, {{"mode", "auto"}, {"estop", true}}));
        QVERIFY(waitFor([&] { return latest.safetyFresh; }));
        QVERIFY(latest.estop);
        QVERIFY(client_->estopEngaged());
        QVERIFY(waitFor([&] { return !latest.safetyFresh; }, 2000));
        QVERIFY(latest.estop);
        server_->send(pub(hmi::ch::kSafety, {{"mode", "auto"}, {"estop", false}}));
        QVERIFY(waitFor([&] { return latest.safetyFresh && !latest.estop; }));
    }

    void navigationReportsRetainRevisionAcrossCoalescedSnapshots()
    {
        connectPair();
        Telemetry latest;
        connect(client_, &BridgeClient::telemetry, this, [&](const Telemetry &tm) { latest = tm; });
        server_->send(pub(hmi::ch::kNav, {{"status", "navigating"}}));
        server_->send(pub(hmi::ch::kNav, {{"status", "succeeded"}}));
        QVERIFY(waitFor([&] { return latest.navStatus == QLatin1String("succeeded"); }));
        QCOMPARE(latest.navRevision, quint64(2));
        QCOMPARE(client_->navigationRevision(), latest.navRevision);
        server_->send(pub(hmi::ch::kPose, {{"x", 0.0}, {"y", 0.0}, {"theta", 0.0},
            {"speed", QJsonValue::Null}, {"yaw_rate", QJsonValue::Null}}));
        QVERIFY(waitFor([&] { return latest.poseFresh; }));
        QVERIFY(std::isnan(latest.speed));
        QVERIFY(std::isnan(latest.angularSpeed));
    }

    void armFeedbackExpiresIndependentlyOfTheConnection()
    {
        connectPair();
        Telemetry latest;
        connect(client_, &BridgeClient::telemetry, this, [&](const Telemetry &value) { latest = value; });
        server_->send(pub(hmi::ch::kArm, {{"positions", QJsonArray{0.0, -0.4, 0.5, -1.2, 0.0, 0.4}}}));
        QVERIFY(waitFor([&] { return latest.armFresh; }));
        QVERIFY(waitFor([&] { return !latest.armFresh; }, 2000));
        QVERIFY(client_->isConnected());
        server_->send(pub(hmi::ch::kArm, {{"positions", QJsonArray{0.0, -0.4, 0.5, -1.2, 0.0, 0.4}}}));
        QVERIFY(waitFor([&] { return latest.armFresh; }));
        server_->send(pub(hmi::ch::kArm, {{"positions", QJsonArray{0.0, -0.4}}}));
        QVERIFY(waitFor([&] { return !latest.armFresh && latest.joints.isEmpty(); }));
    }

    void dockReturnWaitsForAutoConfirmationAndManualCancelsDeferredMotion()
    {
        connectPair();
        server_->send(pub(hmi::ch::kSafety, {{"mode", "manual"}}));
        QVERIFY(waitFor([&] { return client_->mode() == hmi::robot::DriveMode::Manual; }));
        client_->returnToDock();
        QVERIFY(waitFor([&] { return client_->modeChangePending() && std::any_of(
            server_->received.cbegin(), server_->received.cend(), [](const Envelope &e) {
                return e.ch == QLatin1String(hmi::ch::kCmdMode);
            }); }));
        Envelope mode;
        for (const auto &e : server_->received) {
            QVERIFY(e.ch != QLatin1String(hmi::ch::kCmdMissionReturnDock));
            if (e.ch == QLatin1String(hmi::ch::kCmdMode)) mode = e;
        }
        server_->send(pub(hmi::ch::kSafety, {{"mode", "auto"}}));
        server_->send(makeResponse(mode, true));
        QVERIFY(waitFor([&] { return std::any_of(server_->received.cbegin(), server_->received.cend(),
            [](const Envelope &e) { return e.ch == QLatin1String(hmi::ch::kCmdMissionReturnDock); }); }));
        server_->received.clear();
        client_->setMode(hmi::robot::DriveMode::Manual);
        client_->requestGoal(1.0, 2.0, 0.0);
        client_->setMode(hmi::robot::DriveMode::Manual);
        QVERIFY(waitFor([&] { return !server_->received.isEmpty(); }));
        QTest::qWait(50);
        for (const auto &e : server_->received)
            QVERIFY(e.ch != QLatin1String(hmi::ch::kCmdGoto));
    }

    void navigationSpeedUsesRobotReportedStateAndExplicitRequests()
    {
        connectPair();
        QSignalSpy reports(client_, &BridgeClient::navigationSpeedLimitsChanged);
        QJsonObject limits{{"speed_limit_mps", 0.30}, {"min_speed_mps", 0.10}, {"max_speed_mps", 0.60},
                           {"angular_speed_limit_rps", 0.50}, {"min_angular_speed_rps", 0.05},
                           {"max_angular_speed_rps", 0.80}, {"autonomous_applied", true}};
        server_->send(pub(hmi::ch::kNavigationSpeed, limits));
        QVERIFY(waitFor([&] { return reports.size() == 1; }));
        QCOMPARE(reports.first().at(0).toDouble(), 0.30);
        QCOMPARE(reports.first().at(3).toDouble(), 0.50);
        QCOMPARE(reports.first().at(6).toBool(), true);
        server_->send(pub(hmi::ch::kNavigationSpeed, {{"speed_limit_mps", 0.40}}));
        server_->send(pub(hmi::ch::kNavigationSpeed,
                          {{"speed_limit_mps", "0.40"}, {"min_speed_mps", 0.10}, {"max_speed_mps", 0.60}}));
        auto invalid = limits;
        invalid["angular_speed_limit_rps"] = 0.81;
        server_->send(pub(hmi::ch::kNavigationSpeed, invalid));
        invalid["angular_speed_limit_rps"] = "0.50";
        server_->send(pub(hmi::ch::kNavigationSpeed, invalid));
        invalid = limits;
        invalid["autonomous_applied"] = "true";
        server_->send(pub(hmi::ch::kNavigationSpeed, invalid));
        client_->setNavigationSpeedLimits(0.40, 0.65);
        QVERIFY(waitFor([&] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdNavigationSpeedLimit))
                    return e.p.value("speed_limit_mps").toDouble() == 0.40 &&
                           e.p.value("angular_speed_limit_rps").toDouble() == 0.65;
            return false;
        }));
        QCOMPARE(reports.size(), 1);
        client_->setNavigationSpeedSettings(0.35, 0.20, 0.50, 0.45, 0.10, 0.70);
        QVERIFY(waitFor([&] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdNavigationSpeedSettings))
                    return e.p == QJsonObject{{"speed_limit_mps", 0.35}, {"min_speed_mps", 0.20},
                        {"max_speed_mps", 0.50}, {"angular_speed_limit_rps", 0.45},
                        {"min_angular_speed_rps", 0.10}, {"max_angular_speed_rps", 0.70}};
            return false;
        }));
        bool subscribed = false;
        for (const auto &e : server_->received)
            if (e.t == QLatin1String(mtype::kSub))
                subscribed |= e.p.value("channels").toArray().contains(QLatin1String(hmi::ch::kNavigationSpeed));
        QVERIFY(subscribed);
    }

    void cleanup()
    {
        delete client_;
        client_ = nullptr;
        delete server_;
        server_ = nullptr;
    }

    // ---- 연결 ------------------------------------------------------------

    /// 로봇을 바꾸면 그 로봇에 가서 붙는다.
    ///
    /// 관제 한 대가 여러 로봇을 다루므로 이 전환이 조용히 실패하면 화면은
    /// 새 로봇의 이름을 달고 옛 로봇의 값을 보여 준다.
    void setEndpoint_movesToTheOtherRobot()
    {
        connectPair();

        MockBridge second;
        client_->setEndpoint(QStringLiteral("127.0.0.1"), second.port());

        QVERIFY2(waitFor([&] { return client_->isConnected() && second.hasPeer(); }),
                 "새 주소로 붙지 않았다");
        QVERIFY2(!server_->hasPeer(), "옛 연결이 살아 있다");
    }

    /// 바꾸는 순간 옛 로봇의 식별자를 버린다.
    void setEndpoint_forgetsTheOldIdentity()
    {
        connectPair();
        // 식별자는 봉투에 실려 온다. 페이로드의 robot_id 는 사람이 읽을
        // 이름을 함께 나르는 자리일 뿐이다.
        auto hello = makePublish(QLatin1String(hmi::ch::kSystem), QJsonObject{});
        hello.robot = QStringLiteral("R1");
        server_->send(hello);
        QVERIFY(waitFor([this] { return client_->describe() == QStringLiteral("R1"); }));

        MockBridge second;
        QSignalSpy spy(client_, &hmi::robot::RobotLink::robotIdentity);
        client_->setEndpoint(QStringLiteral("127.0.0.1"), second.port());

        QVERIFY2(spy.count() >= 1, "식별자를 버렸다고 알리지 않았다");
        QCOMPARE(spy.takeLast().at(0).toString(), QString());
    }

    void pausedNavigationKeepsGoalMarker()
    {
        connectPair();
        Telemetry latest;
        connect(client_, &BridgeClient::telemetry, this, [&](const Telemetry &tm) { latest = tm; });
        const QJsonObject goal{{"x", 2.0}, {"y", 3.0}, {"theta", 1.5}};
        for (const auto *status : {"accepting", "navigating", "pausing", "paused", "canceling"}) {
            server_->send(pub(hmi::ch::kNav, {{"status", status}, {"goal", goal}}));
            QVERIFY(waitFor([&] { return latest.navStatus == QLatin1String(status); }));
            QCOMPARE(latest.navGoal, goal.toVariantMap());
        }
        server_->send(pub(hmi::ch::kNav, {{"status", "canceled"}, {"goal", goal}}));
        QVERIFY(waitFor([&] { return latest.navStatus == QLatin1String("canceled"); }));
        QVERIFY(latest.navGoal.isEmpty());
    }

    void navigationProgressIsOptionalAndExpires()
    {
        connectPair();
        Telemetry latest;
        connect(client_, &BridgeClient::telemetry, this, [&](const Telemetry &tm) { latest = tm; });
        server_->send(pub(hmi::ch::kNav, {{"status", "navigating"}, {"distance_remaining_m", 4.2},
            {"eta_s", 18.0}, {"elapsed_s", 3.0}, {"recoveries", 2},
            {"navigation_state", "active"}, {"localization_state", "inactive"}}));
        QVERIFY(waitFor([&] { return latest.navFresh; }));
        QCOMPARE(latest.navDistance, 4.2);
        QCOMPARE(latest.navEta, 18.0);
        QCOMPARE(latest.navElapsed, 3.0);
        QCOMPARE(latest.navRecoveries, 2);
        QCOMPARE(latest.navigationLifecycle, QStringLiteral("active"));
        QCOMPARE(latest.localizationLifecycle, QStringLiteral("inactive"));
        QVERIFY(waitFor([&] { return !latest.navFresh; }, 2000));
        server_->send(pub(hmi::ch::kNav, {{"status", "failed"}, {"eta_s", QJsonValue::Null},
            {"distance_remaining_m", -1}, {"error", "planner unavailable"}}));
        QVERIFY(waitFor([&] { return latest.navFresh && latest.navStatus == QLatin1String("failed"); }));
        QVERIFY(std::isnan(latest.navDistance));
        QVERIFY(std::isnan(latest.navEta));
        QVERIFY(std::isnan(latest.navElapsed));
        QCOMPARE(latest.navRecoveries, -1);
        QCOMPARE(latest.navError, QStringLiteral("planner unavailable"));
        server_->send(pub(hmi::ch::kPose, {{"yaw_rate", 0.3}}));
        QVERIFY(waitFor([&] { return latest.angularSpeed == 0.3; }));
    }

    void setEndpoint_discardsPreviousRobotNavigation()
    {
        connectPair();
        QSignalSpy snapshots(client_, &BridgeClient::telemetry);
        server_->send(pub(hmi::ch::kNav,
                          {{"status", "navigating"},
                           {"goal", QJsonObject{{"x", 4.0}, {"y", 2.0}, {"theta", 0.5}}}}));
        server_->send(pub(hmi::ch::kPlan,
                          {{"points", QJsonArray{QJsonArray{1.0, 1.0}, QJsonArray{4.0, 2.0}}}}));
        server_->send(pub(hmi::ch::kTrail,
                          {{"reset", true}, {"points", QJsonArray{QJsonArray{1.0, 1.0}}}}));
        QVERIFY(waitFor([&snapshots] {
            if (snapshots.isEmpty()) return false;
            const auto tm = snapshots.last().at(0).value<Telemetry>();
            return !tm.navGoal.isEmpty() && !tm.plan.isEmpty() && !tm.trail.isEmpty();
        }));

        MockBridge second;
        client_->setEndpoint(QStringLiteral("127.0.0.1"), second.port());
        QVERIFY(waitFor([&] { return second.hasPeer(); }));
        QVERIFY(waitFor([&snapshots] {
            if (snapshots.isEmpty()) return false;
            const auto tm = snapshots.last().at(0).value<Telemetry>();
            return tm.navGoal.isEmpty() && tm.plan.isEmpty() && tm.trail.isEmpty();
        }));
        QVERIFY(waitFor([&second] {
            for (const auto &e : second.received)
                if (e.ch == QLatin1String(hmi::ch::kCmdTrailSnapshot)) return true;
            return false;
        }));
    }

    void connect_subscribesToStateChannels()
    {
        connectPair();
        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.t == QLatin1String(mtype::kSub))
                    return true;
            return false;
        }));

        QStringList channels;
        for (const auto &e : server_->received) {
            if (e.t != QLatin1String(mtype::kSub))
                continue;
            for (const auto &v : e.p.value(QStringLiteral("channels")).toArray())
                channels << v.toString();
        }
        // 최소한 위치·안전·미션은 구독해야 화면이 산다.
        QVERIFY(channels.contains(QLatin1String(hmi::ch::kPose)));
        QVERIFY(channels.contains(QLatin1String(hmi::ch::kSafety)));
        QVERIFY(channels.contains(QLatin1String(hmi::ch::kMission)));
    }

    /// 최초 실행에서 정책을 준비하는 것은 명령 실패가 아니다. 사용자가
    /// 로봇을 선택하기도 전에 "연결 끊김" 알림이 생기면 미연결 상태와
    /// 실제 통신 장애를 구별할 수 없다.
    void batteryPolicy_isDeferredUntilConnected()
    {
        client_ = new BridgeClient(QStringLiteral("127.0.0.1"), 9, this);
        QSignalSpy events(client_, &hmi::robot::RobotLink::robotEvent);

        client_->setBatteryPolicy(30.0, 80.0);
        QTest::qWait(50);

        QCOMPARE(events.count(), 0);
    }

    void heartbeat_isSentPeriodically()
    {
        connectPair();
        QVERIFY2(waitFor([this] {
                     int n = 0;
                     for (const auto &e : server_->received)
                         if (e.t == QLatin1String(mtype::kHb))
                             ++n;
                     return n >= 2;
                 }),
                 "하트비트가 주기적으로 나가야 한다");
    }

    // ---- 텔레메트리 ------------------------------------------------------

    void telemetry_assemblesChannelsIntoOneSnapshot()
    {
        connectPair();
        QSignalSpy spy(client_, &BridgeClient::telemetry);

        server_->send(pub(hmi::ch::kPose,
                          {{"x", 3.25}, {"y", -1.5}, {"theta", 0.75}, {"speed", 0.2}}));
        server_->send(pub(hmi::ch::kBattery, {{"soc", 63.0}}));
        server_->send(pub(hmi::ch::kSystem,
                          {{"capture_enabled", true}, {"arm_execution_enabled", false}}));
        QJsonArray joints{0.1, -0.8, 0.0, -2.2, 0.0, 1.5};
        server_->send(pub(hmi::ch::kArm,
                          {{"positions", joints}, {"manipulability", 0.07},
                           {"sigma_min", 0.05}}));

        QVERIFY(waitFor([&spy] {
            if (spy.isEmpty())
                return false;
            const auto tm = spy.last().at(0).value<Telemetry>();
            return qFuzzyCompare(tm.x, 3.25) && qFuzzyCompare(tm.soc, 63.0);
        }));

        const auto tm = spy.last().at(0).value<Telemetry>();
        QCOMPARE(tm.y, -1.5);
        QCOMPARE(tm.joints.size(), 6);
        QVERIFY(tm.armFresh);
        QVERIFY(qFuzzyCompare(tm.manipulability, 0.07));
        QVERIFY(tm.captureEnabled);
        QVERIFY(!tm.armExecutionEnabled);
        QVERIFY2(tm.poseFresh, "방금 받은 위치는 신선해야 한다");
    }

    void missingArmMetricsRemainUnknown()
    {
        connectPair();
        QSignalSpy snapshots(client_, &BridgeClient::telemetry);
        server_->send(pub(hmi::ch::kArm, {{"positions", QJsonArray{0.0, 0.0, 0.0,
            0.0, 0.0, 0.0}}}));
        QVERIFY(waitFor([&snapshots] {
            return !snapshots.isEmpty() &&
                snapshots.last().at(0).value<Telemetry>().joints.size() == 6;
        }));
        const auto tm = snapshots.last().at(0).value<Telemetry>();
        QVERIFY(std::isnan(tm.manipulability));
        QVERIFY(std::isnan(tm.sigmaMin));
        QVERIFY(tm.armState.isEmpty());
    }

    void captureAcknowledgesSavedFileAndForwardsPngPreview()
    {
        connectPair();
        QSignalSpy results(client_, &hmi::robot::RobotLink::commandResult);
        QSignalSpy previews(client_, &BridgeClient::previewReceived);
        client_->triggerCapture({{"vehicle_number", "GTXA-042"}, {"train_number", "1234"},
                                 {"car_number", "05"}, {"point_id", "C01-P03"}});
        QVERIFY(waitFor([this] {
            for (const auto &request : server_->received)
                if (request.ch == QLatin1String(hmi::ch::kCmdCapture)) return true;
            return false;
        }));
        for (const auto &request : server_->received) {
            if (request.ch != QLatin1String(hmi::ch::kCmdCapture))
                continue;
            auto response = makeResponse(request, true);
            response.p[QStringLiteral("file")] = QStringLiteral("capture.png");
            server_->send(response);
            break;
        }
        QVERIFY(waitFor([&results] { return !results.isEmpty(); }));
        QCOMPARE(results.last().at(0).toString(), QLatin1String(hmi::ch::kCmdCapture));
        QCOMPARE(results.last().at(1).toBool(), true);
        QCOMPARE(results.last().at(3).toString(), QStringLiteral("capture.png"));

        QImage sample(2, 2, QImage::Format_RGB32);
        sample.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(sample.save(&buffer, "PNG"));
        auto preview = pub(hmi::ch::kPreview, {{"file", "capture.png"}});
        preview.payload = png;
        server_->send(preview);
        QVERIFY(waitFor([&previews] { return !previews.isEmpty(); }));
        QImage received;
        QVERIFY(received.loadFromData(previews.last().at(0).toByteArray()));
        QCOMPARE(received.size(), sample.size());
    }

    void telemetry_planAndTrailDecode()
    {
        connectPair();
        QSignalSpy spy(client_, &BridgeClient::telemetry);

        QJsonArray pts{QJsonArray{1.0, 2.0}, QJsonArray{3.0, 4.0}};
        server_->send(pub(hmi::ch::kPlan, {{"points", pts}}));
        server_->send(pub(hmi::ch::kTrail, {{"points", pts}, {"reset", true}}));

        QVERIFY(waitFor([&spy] {
            return !spy.isEmpty() && spy.last().at(0).value<Telemetry>().plan.size() == 2;
        }));
        const auto tm = spy.last().at(0).value<Telemetry>();
        QCOMPARE(tm.plan.first(), QPointF(1.0, 2.0));
        QCOMPARE(tm.trail.size(), 2);
    }

    void telemetry_trailResetDropsPreviousTask()
    {
        connectPair();
        QSignalSpy spy(client_, &BridgeClient::telemetry);
        server_->send(pub(hmi::ch::kTrail,
                          {{"points", QJsonArray{QJsonArray{1.0, 2.0},
                                                   QJsonArray{3.0, 4.0}}},
                           {"reset", true}}));
        QVERIFY(waitFor([&spy] {
            return !spy.isEmpty() && spy.last().at(0).value<Telemetry>().trail.size() == 2;
        }));
        server_->send(pub(hmi::ch::kTrail, {{"points", QJsonArray{}}, {"reset", true}}));
        QVERIFY(waitFor([&spy] {
            return !spy.isEmpty() && spy.last().at(0).value<Telemetry>().trail.isEmpty();
        }));
        server_->send(pub(hmi::ch::kTrail,
                          {{"points", QJsonArray{QJsonArray{5.0, 6.0}}}, {"reset", false}}));
        QVERIFY(waitFor([&spy] {
            return !spy.isEmpty() && spy.last().at(0).value<Telemetry>().trail.size() == 1;
        }));
        QCOMPARE(spy.last().at(0).value<Telemetry>().trail.first(), QPointF(5.0, 6.0));
    }

    /// 링크가 살아 있어도 pose 만 끊길 수 있다. 그 상태의 좌표를 사실처럼
    /// 쓰면 위치 등록이 잘못된 값을 저장한다.
    void telemetry_poseGoesStaleWhenItStopsArriving()
    {
        connectPair();
        QSignalSpy spy(client_, &BridgeClient::telemetry);
        server_->send(pub(hmi::ch::kPose, {{"x", 1.0}, {"y", 1.0}, {"theta", 0.0}}));
        QVERIFY(waitFor([&spy] {
            return !spy.isEmpty() && spy.last().at(0).value<Telemetry>().poseFresh;
        }));

        // pose 를 더 보내지 않는다. 하트비트는 계속 흐른다.
        QVERIFY2(waitFor(
                     [&spy] {
                         return !spy.isEmpty()
                                && !spy.last().at(0).value<Telemetry>().poseFresh;
                     },
                     4000),
                 "위치가 끊기면 신선하지 않은 것으로 표시되어야 한다");
    }

    // ---- 명령 ------------------------------------------------------------

    void driveModeWaitsForRobotSafetyReport()
    {
        connectPair();
        QList<hmi::robot::DriveMode> reports;
        connect(client_, &hmi::robot::RobotLink::driveModeReported, this,
                [&reports](hmi::robot::DriveMode mode) { reports << mode; });

        client_->setMode(hmi::robot::DriveMode::Manual);
        QCOMPARE(client_->mode(), hmi::robot::DriveMode::Auto);
        QVERIFY(reports.isEmpty());

        server_->send(pub(hmi::ch::kSafety, {{"mode", "manual"}}));
        QVERIFY(waitFor([&] { return reports.size() == 1; }));
        QCOMPARE(reports.last(), hmi::robot::DriveMode::Manual);
        QCOMPARE(client_->mode(), hmi::robot::DriveMode::Manual);

        client_->setMode(hmi::robot::DriveMode::Auto);
        QCOMPARE(client_->mode(), hmi::robot::DriveMode::Manual);
        server_->send(pub(hmi::ch::kSafety, {{"mode", "auto"}}));
        QVERIFY(waitFor([&] { return reports.size() == 2; }));
        QCOMPARE(client_->mode(), hmi::robot::DriveMode::Auto);
    }

    void command_isSentAsRequest()
    {
        connectPair();
        client_->requestGoal(2.0, 3.0, 0.5);

        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.t == QLatin1String(mtype::kReq)
                    && e.ch == QLatin1String(hmi::ch::kCmdGoto))
                    return true;
            return false;
        }));

        for (const auto &e : server_->received) {
            if (e.ch != QLatin1String(hmi::ch::kCmdGoto))
                continue;
            QCOMPARE(e.p.value(QStringLiteral("x")).toDouble(), 2.0);
            QVERIFY2(!e.id.isEmpty(), "요청에는 상관 ID 가 있어야 한다");
        }
    }

    void waypoints_followRobotConfirmationAndMapSwitch()
    {
        connectPair();
        QSignalSpy changes(client_, &hmi::robot::RobotLink::waypointsChanged);
        const QVariantMap draft{{"id", QStringLiteral("wp-1")},
                                {"name", QStringLiteral("입구")},
                                {"x", 1.0}, {"y", 2.0},
                                {"status", QStringLiteral("done")}};
        client_->setWaypoints({draft});
        QCOMPARE(client_->waypoints().size(), 0);

        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdWaypointsSet))
                    return true;
            return false;
        }));
        for (const auto &e : server_->received)
            if (e.ch == QLatin1String(hmi::ch::kCmdWaypointsSet))
                QVERIFY(!e.p.value(QStringLiteral("points")).toArray().first()
                             .toObject().contains(QStringLiteral("status")));
        for (const auto &e : server_->received)
            if (e.ch == QLatin1String(hmi::ch::kCmdWaypointsSet))
                server_->send(makeResponse(e, false, QLatin1String(err::kMode),
                                           QStringLiteral("저장된 지도가 필요합니다")));
        QTest::qWait(30);
        QCOMPARE(client_->waypoints().size(), 0);
        QCOMPARE(changes.size(), 0);

        server_->send(pub(hmi::ch::kWaypoints,
                          {{"points", QJsonArray{QJsonObject::fromVariantMap(draft)}}}));
        QVERIFY(waitFor([&changes] { return changes.size() == 1; }));
        QCOMPARE(client_->waypoints().first().value(QStringLiteral("id")).toString(),
                 QStringLiteral("wp-1"));

        // 지도를 바꾸면 서버가 빈 목록을 보낸다. 이전 지도의 포인트는 남지 않는다.
        server_->send(pub(hmi::ch::kWaypoints, {{"points", QJsonArray{}}}));
        QVERIFY(waitFor([&changes] { return changes.size() == 2; }));
        QCOMPARE(client_->waypoints().size(), 0);
    }

    void missions_ignoreCatalogFromOtherMap()
    {
        connectPair();
        QSignalSpy changes(client_, &hmi::robot::RobotLink::missionsChanged);
        const QJsonArray one{QJsonObject{{"id", "mission-a"}}};
        server_->send(pub(hmi::ch::kActiveMap, {{"id", "map-a"}}));
        server_->send(pub(hmi::ch::kMissions, {{"map_id", "map-a"}, {"missions", one}}));
        QVERIFY(waitFor([&changes] { return changes.size() == 1; }));

        server_->send(pub(hmi::ch::kActiveMap, {{"id", "map-b"}}));
        server_->send(pub(hmi::ch::kMissions, {{"map_id", "map-a"}, {"missions", one}}));
        QTest::qWait(30);
        QCOMPARE(changes.size(), 1);

        server_->send(pub(hmi::ch::kMissions, {{"map_id", "map-b"}, {"missions", QJsonArray{}}}));
        QVERIFY(waitFor([&changes] { return changes.size() == 2; }));
        QCOMPARE(changes.last().at(0).value<QList<QVariantMap>>().size(), 0);
    }

    void armPoseSave_resultAndPublishedListAreDistinct()
    {
        connectPair();
        QSignalSpy results(client_, &hmi::robot::RobotLink::commandResult);
        QSignalSpy presets(client_, &hmi::robot::RobotLink::armPosePresetsChanged);
        const QVariantMap preset{{"id", QStringLiteral("pose-1")},
                                 {"name", QStringLiteral("점검 자세")},
                                 {"positions", QVariantList{0.0, -1.0, 0.0, -1.0, 0.0, 0.0}}};
        client_->saveArmPosePreset(preset);
        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdArmPosePresetsSave))
                    return true;
            return false;
        }));
        for (const auto &e : server_->received)
            if (e.ch == QLatin1String(hmi::ch::kCmdArmPosePresetsSave))
                server_->send(makeResponse(e, true));
        QVERIFY(waitFor([&results] { return !results.isEmpty(); }));
        QCOMPARE(results.last().at(0).toString(),
                 QLatin1String(hmi::ch::kCmdArmPosePresetsSave));
        QVERIFY(results.last().at(1).toBool());
        QCOMPARE(presets.size(), 0);

        server_->send(pub(hmi::ch::kArmPosePresets,
                          {{"presets", QJsonArray{QJsonObject::fromVariantMap(preset)}}}));
        QVERIFY(waitFor([&presets] { return presets.size() == 1; }));
        QCOMPARE(presets.last().at(0).value<QList<QVariantMap>>().first()
                     .value(QStringLiteral("name")).toString(), QStringLiteral("점검 자세"));
    }

    void mapManagementCommands_areSentToRobot()
    {
        connectPair();
        client_->renameMap(QStringLiteral("map-1"), QStringLiteral("시험 지도"));
        client_->deleteMap(QStringLiteral("map-2"));
        client_->setDefaultMap(QStringLiteral("map-1"));
        QVERIFY(waitFor([this] {
            bool renamed = false;
            bool deleted = false;
            bool defaultSet = false;
            for (const auto &e : server_->received) {
                renamed |= e.ch == QLatin1String(hmi::ch::kCmdMapsRename);
                deleted |= e.ch == QLatin1String(hmi::ch::kCmdMapsDelete);
                defaultSet |= e.ch == QLatin1String(hmi::ch::kCmdMapsSetDefault);
            }
            return renamed && deleted && defaultSet;
        }));
        for (const auto &e : server_->received) {
            if (e.ch == QLatin1String(hmi::ch::kCmdMapsRename)) {
                QCOMPARE(e.p.value(QStringLiteral("id")).toString(), QStringLiteral("map-1"));
                QCOMPARE(e.p.value(QStringLiteral("name")).toString(), QStringLiteral("시험 지도"));
            }
            if (e.ch == QLatin1String(hmi::ch::kCmdMapsDelete))
                QCOMPARE(e.p.value(QStringLiteral("id")).toString(), QStringLiteral("map-2"));
            if (e.ch == QLatin1String(hmi::ch::kCmdMapsSetDefault))
                QCOMPARE(e.p.value(QStringLiteral("id")).toString(), QStringLiteral("map-1"));
        }
    }

    void emptyMapLocations_clearPreviousDockAndHome()
    {
        connectPair();
        server_->send(pub(hmi::ch::kLocations,
                          {{"locations", QJsonArray{
                              QJsonObject{{"kind", "dock"}, {"x", 1.0}},
                              QJsonObject{{"kind", "home"}, {"x", 2.0}}
                          }}}));
        QVERIFY(waitFor([this] {
            return !client_->dockPose().isEmpty() && !client_->homePose().isEmpty();
        }));
        server_->send(pub(hmi::ch::kLocations, {{"locations", QJsonArray{}}}));
        QVERIFY(waitFor([this] {
            return client_->dockPose().isEmpty() && client_->homePose().isEmpty();
        }));
    }

    void armPoseUpdate_sendsMetadataAndRevisionToRobot()
    {
        connectPair();
        const QVariantMap preset{{"id", QStringLiteral("pose-1")},
                                 {"name", QStringLiteral("수정한 자세")},
                                 {"description", QStringLiteral("왼쪽 하부 촬영")},
                                 {"positions", QVariantList{0.0, -1.0, 0.0, -1.0, 0.0, 0.0}}};
        client_->updateArmPosePreset(preset, 4);
        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdArmPosePresetsUpdate))
                    return true;
            return false;
        }));
        for (const auto &e : server_->received) {
            if (e.ch != QLatin1String(hmi::ch::kCmdArmPosePresetsUpdate))
                continue;
            QCOMPARE(e.p.value(QStringLiteral("expected_revision")).toInt(), 4);
            const auto sent = e.p.value(QStringLiteral("preset")).toObject();
            QCOMPARE(sent.value(QStringLiteral("name")).toString(), QStringLiteral("수정한 자세"));
            QCOMPARE(sent.value(QStringLiteral("description")).toString(),
                     QStringLiteral("왼쪽 하부 촬영"));
        }
    }

    void armPoseArchive_sendsIdAndRevisionToRobot()
    {
        connectPair();
        client_->archiveArmPosePreset(QStringLiteral("inspect-left"), 7);
        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdArmPosePresetsArchive))
                    return true;
            return false;
        }));
        for (const auto &e : server_->received) {
            if (e.ch != QLatin1String(hmi::ch::kCmdArmPosePresetsArchive))
                continue;
            QCOMPARE(e.p.value(QStringLiteral("id")).toString(), QStringLiteral("inspect-left"));
            QCOMPARE(e.p.value(QStringLiteral("expected_revision")).toInt(), 7);
        }
    }

    /// 조작자가 다시 등록한 충전소는 로봇에게 가야 한다.
    ///
    /// 화면에만 적어 두면 배터리 복귀와 점검 종료 복귀는 로봇이 예전부터
    /// 알던 자리로 간다. 조작자는 방금 옮겨 놓은 줄 알고 있고, 그 차이는
    /// 로봇이 엉뚱한 데로 갈 때에야 드러난다.
    void taughtDock_reachesTheRobot()
    {
        connectPair();
        client_->setLocations({QVariantMap{{"kind", QStringLiteral("dock")},
                                           {"x", -12.5},
                                           {"y", 3.25},
                                           {"theta", 1.0}}});

        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.t == QLatin1String(mtype::kReq)
                    && e.ch == QLatin1String(hmi::ch::kCmdLocationsSet))
                    return true;
            return false;
        }));

        for (const auto &e : server_->received) {
            if (e.ch != QLatin1String(hmi::ch::kCmdLocationsSet))
                continue;
            const auto arr = e.p.value(QStringLiteral("locations")).toArray();
            QCOMPARE(arr.size(), 1);
            QCOMPARE(arr.at(0).toObject().value(QStringLiteral("x")).toDouble(), -12.5);
        }

        QVERIFY(client_->dockPose().isEmpty());
        server_->send(pub(hmi::ch::kLocations,
                          {{"locations", QJsonArray{QJsonObject{{"kind", "dock"},
                              {"x", -12.5}, {"y", 3.25}, {"theta", 1.0}}}}}));
        QVERIFY(waitFor([this] { return !client_->dockPose().isEmpty(); }));
        QCOMPARE(client_->dockPose().value(QStringLiteral("x")).toDouble(), -12.5);
    }

    /// 로봇이 알려 주는 고정 위치를 받아 둔다.
    ///
    /// 관제 화면은 등록한 위치를 스스로 저장하지 않는다. 로봇에게 보내고
    /// 로봇이 다시 알려 주는 이 경로가, 프로그램을 껐다 켜도 위치가 남는
    /// 유일한 이유다.
    void fixedLocationsFromRobot_areRemembered()
    {
        connectPair();
        server_->send(pub(hmi::ch::kLocations,
                          {{"locations",
                            QJsonArray{QJsonObject{{"kind", QStringLiteral("dock")},
                                                   {"x", -85.0},
                                                   {"y", -6.0},
                                                   {"theta", 0.0}},
                                       QJsonObject{{"kind", QStringLiteral("home")},
                                                   {"x", 1.5},
                                                   {"y", 2.5},
                                                   {"theta", 0.0}}}}}));

        QVERIFY(waitFor([this] {
            return !client_->dockPose().isEmpty() && !client_->homePose().isEmpty();
        }));
        QCOMPARE(client_->dockPose().value(QStringLiteral("y")).toDouble(), -6.0);
        QCOMPARE(client_->homePose().value(QStringLiteral("x")).toDouble(), 1.5);
    }

    /// 거부된 명령이 조용히 사라지면 조작자는 명령이 먹은 줄 안다.
    void rejectedCommand_surfacesAsEvent()
    {
        connectPair();
        QSignalSpy events(client_, &BridgeClient::robotEvent);
        client_->requestGoal(1.0, 1.0, 0.0);

        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdGoto))
                    return true;
            return false;
        }));

        Envelope req;
        for (const auto &e : server_->received)
            if (e.ch == QLatin1String(hmi::ch::kCmdGoto))
                req = e;
        server_->send(makeResponse(req, false, QLatin1String(err::kEstopEngaged),
                                   QStringLiteral("정지 중")));

        QVERIFY(waitFor([&events] {
            for (const auto &sig : events)
                if (sig.at(0).toString() == QLatin1String(err::kEstopEngaged))
                    return true;
            return false;
        }));
    }

    /// 응답 없는 명령도 보고되어야 한다.
    void unansweredCommand_timesOut()
    {
        connectPair();
        QSignalSpy events(client_, &BridgeClient::robotEvent);
        client_->missionStart();   // 서버가 응답하지 않는다

        QVERIFY2(waitFor(
                     [&events] {
                         for (const auto &sig : events)
                             if (sig.at(1).toMap().value(QStringLiteral("channel")).toString()
                                 == QLatin1String(hmi::ch::kCmdMissionStart))
                                 return true;
                         return false;
                     },
                     7500),
                 "응답 없는 명령이 보고되어야 한다");
    }

    void cmdVel_isSentOverUdpWithDeadman()
    {
        connectPair();
        auto identity = pub(hmi::ch::kSystem, {});
        identity.robot = QStringLiteral("R1");
        server_->send(identity);
        QVERIFY(waitFor([this] { return client_->describe() == QLatin1String("R1"); }));
        client_->setCmdVel(0.3, 0.0, 0.1);

        QVERIFY(waitFor([this] {
            return !server_->udpPackets.isEmpty();
        }));
        const auto packet = QJsonDocument::fromJson(server_->udpPackets.last()).object();
        QCOMPARE(packet.value(QStringLiteral("v")).toInt(), 1);
        QCOMPARE(packet.value(QStringLiteral("t")).toString(), QLatin1String("teleop"));
        QCOMPARE(packet.value(QStringLiteral("robot")).toString(), QLatin1String("R1"));
        QVERIFY(packet.value(QStringLiteral("deadman")).toBool());
        QCOMPARE(packet.value(QStringLiteral("vx")).toDouble(), 0.3);
    }

    // ---- 미션 상태 -------------------------------------------------------

    /// 미션 상태의 주인은 로봇이다. UI 는 따라간다.
    void missionState_followsRobot()
    {
        connectPair();
        QSignalSpy spy(client_, &BridgeClient::missionStateChanged);
        server_->send(pub(hmi::ch::kMission, {{"state", QStringLiteral("running")}}));

        QVERIFY(waitFor([&spy] { return !spy.isEmpty(); }));
        QCOMPARE(client_->missionState(), hmi::robot::MissionState::Running);

        server_->send(pub(hmi::ch::kMission, {{"state", QStringLiteral("paused")}}));
        QVERIFY(waitFor([this] {
            return client_->missionState() == hmi::robot::MissionState::Paused;
        }));
    }

    void firstIdleMissionStateIsPublishedAfterConnection()
    {
        connectPair();
        QSignalSpy spy(client_, &BridgeClient::missionStateChanged);
        server_->send(pub(hmi::ch::kMission, {{"state", QStringLiteral("idle")}}));
        QVERIFY(waitFor([&spy] { return spy.size() == 1; }));
        QCOMPARE(client_->missionState(), hmi::robot::MissionState::Idle);
    }

    // ---- 본체 자세 -------------------------------------------------------

    /// 자세는 로봇이 알려 준 것만 표시한다. 버튼을 눌렀다고 화면이 먼저
    /// 바꾸면, 로봇이 거절했을 때 화면과 로봇이 어긋난 채로 남는다.
    void basePosture_followsRobot()
    {
        connectPair();
        QSignalSpy spy(client_, &hmi::robot::RobotLink::baseStateChanged);
        server_->send(pub(hmi::ch::kBase, {{"posture", QStringLiteral("stand_down")},
                                           {"motion_authority", QStringLiteral("none")}}));

        QVERIFY(waitFor([&spy] { return !spy.isEmpty(); }));
        QCOMPARE(client_->basePosture(), QStringLiteral("stand_down"));
        QCOMPARE(client_->motionAuthority(), QStringLiteral("none"));
    }

    /// damp 은 confirm 이 같이 가야 브릿지가 받는다. 이 필드가 빠지면
    /// 조작자는 버튼이 먹지 않는 이유를 알 수 없다.
    void basePosture_carriesConfirmationForDamp()
    {
        connectPair();
        client_->setBasePosture(QStringLiteral("stand_up"));
        client_->setBasePosture(QStringLiteral("damp"), true);

        QVERIFY(waitFor([this] {
            int n = 0;
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdBasePosture))
                    ++n;
            return n >= 2;
        }));

        for (const auto &e : server_->received) {
            if (e.ch != QLatin1String(hmi::ch::kCmdBasePosture))
                continue;
            QCOMPARE(e.t, QString::fromLatin1(mtype::kReq));
            const QString posture = e.p.value(QStringLiteral("posture")).toString();
            QCOMPARE(e.p.value(QStringLiteral("confirm")).toBool(),
                     posture == QLatin1String("damp"));
        }
    }

    // ---- 스트림 손상 -----------------------------------------------------

    /// 정합이 깨지면 재동기화하지 않고 끊는다 (명세 §1.3-3).
    void corruptedStream_closesConnection()
    {
        connectPair();
        QSignalSpy events(client_, &BridgeClient::robotEvent);
        // 길이를 손으로 적으면 리터럴을 고칠 때 같이 안 고친다. 실제로
        // 15 바이트짜리를 20 바이트라고 적어 두어, 검사가 리터럴 뒤의
        // 남의 메모리를 4 바이트 읽어 보내고 있었다 (ASan 이 잡았다).
        static const char kGarbage[] = "\xDE\xAD\xBE\xEF\x08\x00\x00\x00garbage";
        server_->sendRaw(QByteArray(kGarbage, sizeof(kGarbage) - 1));

        QVERIFY(waitFor([this] { return !client_->isConnected(); }));
        bool sawFrameError = false;
        for (const auto &sig : events)
            if (sig.at(0).toString() == QLatin1String("LINK_FRAME_CORRUPT"))
                sawFrameError = true;
        QVERIFY2(sawFrameError, "정합 손상이 코드로 보고되어야 한다");
    }

    // ---- 재연결 ----------------------------------------------------------

    void reconnect_afterPeerDisappears()
    {
        connectPair();
        QSignalSpy conn(client_, &BridgeClient::connectionChanged);

        server_->dropPeer();
        QVERIFY(waitFor([this] { return !client_->isConnected(); }));

        // 백오프가 지나면 스스로 다시 붙어야 한다.
        QVERIFY2(waitFor([this] { return client_->isConnected(); }, 8000),
                 "끊긴 뒤 자동으로 재연결되어야 한다");

        bool sawFalse = false, sawTrueAgain = false;
        for (const auto &sig : conn) {
            if (!sig.at(0).toBool())
                sawFalse = true;
            else if (sawFalse)
                sawTrueAgain = true;
        }
        QVERIFY(sawFalse && sawTrueAgain);
    }

    /// 의도적으로 끊었으면 되살아나면 안 된다.
    void explicitDisconnect_doesNotReconnect()
    {
        connectPair();
        client_->disconnectFromBridge();
        QVERIFY(waitFor([this] { return !client_->isConnected(); }));

        // 충분히 기다려도 다시 붙지 않아야 한다.
        QTest::qWait(1500);
        QVERIFY(!client_->isConnected());
    }
};

QTEST_MAIN(TestBridge)
#include "test_bridge.moc"
