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
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

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
                emit gotEnvelope(env->t, env->ch);
            }
        }
    }

    QTcpServer *server_ = nullptr;
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
        QJsonArray joints{0.1, -0.8, 0.0, -2.2, 0.0, 1.5, 0.7};
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
        QCOMPARE(tm.joints.size(), 7);
        QVERIFY(qFuzzyCompare(tm.manipulability, 0.07));
        QVERIFY2(tm.poseFresh, "방금 받은 위치는 신선해야 한다");
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

        // 보낸 값은 화면 쪽에서도 바로 읽혀야 한다. 로봇이 되돌려 줄 때까지
        // 예전 자리를 보여 주면 조작자는 등록이 안 먹은 줄 안다.
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
                     6000),
                 "응답 없는 명령이 보고되어야 한다");
    }

    void cmdVel_isPublishedNotRequested()
    {
        connectPair();
        client_->setCmdVel(0.3, 0.0, 0.1);

        QVERIFY(waitFor([this] {
            for (const auto &e : server_->received)
                if (e.ch == QLatin1String(hmi::ch::kCmdVel))
                    return true;
            return false;
        }));
        for (const auto &e : server_->received)
            if (e.ch == QLatin1String(hmi::ch::kCmdVel))
                QCOMPARE(e.t, QLatin1String(mtype::kPub));   // 응답을 기다리지 않는다
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
            if (sig.at(0).toString() == QLatin1String("FRAME_BAD_MAGIC"))
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
