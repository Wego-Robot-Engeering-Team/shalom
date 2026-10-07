// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QApplication>
#include <QBuffer>
#include <QImage>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <algorithm>

#include "Config.h"
#include "MainWindow.h"
#include "net/BridgeClient.h"
#include "net/Channels.h"
#include "widgets/EStopButton.h"

using namespace hmi;

namespace {
class Remote : public QObject {
public:
    Remote()
    {
        for (int attempt = 0; attempt < 100; ++attempt) {
            if (server.listen(QHostAddress::LocalHost) && server.serverPort() < 65535 &&
                safetyServer.listen(QHostAddress::LocalHost, quint16(server.serverPort() + 1)))
                break;
            server.close();
        }
        connect(&server, &QTcpServer::newConnection, this, [this] {
            peer = server.nextPendingConnection();
            decoder.reset();
            connect(peer, &QTcpSocket::readyRead, this, [this] {
                decoder.append(peer->readAll());
                net::Frame frame;
                while (decoder.next(frame) == net::FrameDecoder::Status::Ok)
                    if (auto envelope = net::Envelope::fromHeader(frame.header)) {
                        requests << *envelope;
                        if (envelope->t == QLatin1String(net::mtype::kHb))
                            send(net::makeHeartbeat(envelope->seq.value_or(0)));
                    }
            });
        });
        connect(&safetyServer, &QTcpServer::newConnection, this, [this] {
            safetyPeer = safetyServer.nextPendingConnection();
            connect(safetyPeer, &QTcpSocket::readyRead, this, [this] {
                safetyDecoder.append(safetyPeer->readAll());
                net::Frame frame;
                while (safetyDecoder.next(frame) == net::FrameDecoder::Status::Ok)
                    if (auto envelope = net::Envelope::fromHeader(frame.header))
                        requests << *envelope;
            });
        });
    }
    void send(const net::Envelope &envelope)
    {
        peer->write(net::encodeFrame(envelope.toHeader(), envelope.payload));
        peer->flush();
    }
    void reportNormal()
    {
        send(net::makePublish(QLatin1String(ch::kSafety),
            {{"estop", false}, {"mode", "auto"}, {"state", "normal"}, {"state_fresh", true}}));
        send(net::makePublish(QLatin1String(ch::kNav),
            {{"status", "idle"}, {"navigation_state", "active"}}));
    }
    void map(net::BridgeClient *link)
    {
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        emit link->mapReceived(png,
            {{"width", 100}, {"height", 100}, {"resolution", 0.1}, {"map_id", "live"}});
    }
    QTcpServer server;
    QTcpServer safetyServer;
    QTcpSocket *peer = nullptr;
    QTcpSocket *safetyPeer = nullptr;
    net::FrameDecoder decoder;
    net::FrameDecoder safetyDecoder;
    QList<net::Envelope> requests;
};
}

class TestSafetyPending : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        Config::instance().setRobots({});
    }

    void stopIntentBlocksMotionUntilPhysicalReport_data()
    {
        QTest::addColumn<bool>("accepted");
        QTest::newRow("accepted-awaits-report") << true;
        QTest::newRow("failure-retains-motion-lock") << false;
    }
    void stopIntentBlocksMotionUntilPhysicalReport()
    {
        QFETCH(bool, accepted);
        Remote remote;
        auto *link = new net::BridgeClient("127.0.0.1", remote.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(remote.peer && remote.peer->state() == QAbstractSocket::ConnectedState && link->isConnected());
        remote.map(link);
        remote.reportNormal();
        auto *goal = window.findChild<QPushButton *>("NavigationGoalButton");
        auto *stop = window.findChild<ui::EStopButton *>();
        QVERIFY(goal && stop);
        QTRY_VERIFY(goal->isEnabled());

        emit stop->engageRequested();
        QVERIFY(!goal->isEnabled());
        QVERIFY(!stop->isEngaged());
        emit link->commandResult(QLatin1String(ch::kCmdEstop), accepted,
            accepted ? QString{} : QStringLiteral("E_UNREACHABLE"), {});
        remote.reportNormal();
        QTest::qWait(150);
        QVERIFY(!goal->isEnabled());
        QVERIFY(!stop->isEngaged());

        // The same button can retry a failed request; no false physical latch.
        if (!accepted) {
            emit stop->engageRequested();
            emit link->commandResult(QLatin1String(ch::kCmdEstop), true, {}, {});
            QVERIFY(!goal->isEnabled());
        }
        remote.send(net::makePublish(QLatin1String(ch::kSafety),
            {{"estop", true}, {"mode", "auto"}, {"state", "e_stop_latched"}, {"state_fresh", true}}));
        QTRY_VERIFY(stop->isEngaged());
        QVERIFY(!goal->isEnabled());
        // Actual release is reported by the robot, not inferred from an ACK.
        remote.reportNormal();
        QTRY_VERIFY(!stop->isEngaged() && goal->isEnabled());
    }

    void unresolvedStopIntentIsScopedToConnection()
    {
        Remote remote;
        auto *link = new net::BridgeClient("127.0.0.1", remote.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(remote.peer && remote.peer->state() == QAbstractSocket::ConnectedState && link->isConnected());
        remote.map(link);
        remote.reportNormal();
        auto *goal = window.findChild<QPushButton *>("NavigationGoalButton");
        auto *stop = window.findChild<ui::EStopButton *>();
        QTRY_VERIFY(goal->isEnabled());
        emit stop->engageRequested();
        emit link->commandResult(QLatin1String(ch::kCmdEstop), false, "E_UNREACHABLE", {});
        QVERIFY(!goal->isEnabled());
        link->disconnectFromBridge();
        QTRY_VERIFY(!link->isConnected());
        link->connectToBridge();
        QTRY_VERIFY(remote.peer && remote.peer->state() == QAbstractSocket::ConnectedState && link->isConnected());
        remote.map(link);
        remote.reportNormal();
        QTRY_VERIFY(goal->isEnabled());
    }

    void estopCancelsQueuedModeAndDeferredGoal()
    {
        Remote remote;
        net::BridgeClient link("127.0.0.1", remote.server.serverPort());
        link.connectToBridge();
        QTRY_VERIFY(remote.peer && remote.safetyPeer && link.isConnected());
        link.setMode(robot::DriveMode::Manual);
        QTRY_VERIFY(std::any_of(remote.requests.cbegin(), remote.requests.cend(), [](const net::Envelope &request) {
            return request.ch == QLatin1String(ch::kCmdMode);
        }));
        net::Envelope modeRequest;
        for (const auto &request : remote.requests)
            if (request.ch == QLatin1String(ch::kCmdMode)) modeRequest = request;
        link.requestGoal(1.0, 2.0, 0.0);
        link.engageEstop();
        remote.send(net::makeResponse(modeRequest, true));
        QTest::qWait(150);
        int modes = 0, goals = 0;
        for (const auto &request : remote.requests) {
            if (request.ch == QLatin1String(ch::kCmdMode)) ++modes;
            if (request.ch == QLatin1String(ch::kCmdGoto)) ++goals;
        }
        QCOMPARE(modes, 1);
        QCOMPARE(goals, 0);
    }

    void releaseConfirmationRevalidatesRobotAndLatch_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("unchanged") << 0;
        QTest::newRow("connection-context-changed") << 1;
        QTest::newRow("latch-already-released") << 2;
    }
    void releaseConfirmationRevalidatesRobotAndLatch()
    {
        QFETCH(int, change);
        Remote remote;
        auto *link = new net::BridgeClient("127.0.0.1", remote.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(remote.peer && remote.safetyPeer && link->isConnected());
        remote.map(link);
        remote.send(net::makePublish(QLatin1String(ch::kSafety),
            {{"estop", true}, {"mode", "auto"}, {"state", "e_stop_latched"}, {"state_fresh", true}}));
        auto *stop = window.findChild<ui::EStopButton *>();
        QTRY_VERIFY(stop->isEngaged());
        QTimer::singleShot(0, &window, [&] {
            if (change == 1) {
                emit link->connectionChanged(false);
                emit link->connectionChanged(true);
                robot::Telemetry tm;
                tm.safetyFresh = tm.estop = true;
                tm.safetyState = "e_stop_latched";
                emit link->telemetry(tm);
            } else if (change == 2) {
                remote.reportNormal();
            }
            QTimer::singleShot(150, &window, [] {
                if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    dialog->button(QMessageBox::Yes)->click();
            });
        });
        emit stop->releaseRequested();
        QTest::qWait(100);
        int releaseRequests = 0;
        for (const auto &request : remote.requests)
            if (request.ch == QLatin1String(ch::kCmdEstopRelease)) ++releaseRequests;
        QCOMPARE(releaseRequests, change == 0 ? 1 : 0);
    }

    void malformedPoseCannotBecomeFresh_data()
    {
        QTest::addColumn<QJsonObject>("payload");
        QTest::newRow("missing-coordinate") << QJsonObject{{"x", 9.0}, {"theta", 0.0}};
        QTest::newRow("string-coordinate") << QJsonObject{{"x", "9.0"}, {"y", 2.0}, {"theta", 0.0}};
        QTest::newRow("null-angle") << QJsonObject{{"x", 9.0}, {"y", 2.0}, {"theta", QJsonValue::Null}};
    }
    void malformedPoseCannotBecomeFresh()
    {
        QFETCH(QJsonObject, payload);
        Remote remote;
        net::BridgeClient link("127.0.0.1", remote.server.serverPort());
        robot::Telemetry reported;
        connect(&link, &robot::RobotLink::telemetry, this,
            [&reported](const robot::Telemetry &tm) { reported = tm; });
        link.connectToBridge();
        QTRY_VERIFY(remote.peer && remote.peer->state() == QAbstractSocket::ConnectedState && link.isConnected());
        remote.send(net::makePublish(QLatin1String(ch::kPose),
            {{"x", 4.0}, {"y", 3.0}, {"theta", 0.25}, {"speed", 0.0}, {"yaw_rate", 0.0}}));
        QTRY_VERIFY(reported.poseFresh);
        remote.send(net::makePublish(QLatin1String(ch::kPose), payload));
        QTRY_VERIFY(!reported.poseFresh);
        QCOMPARE(reported.x, 4.0);
        QCOMPARE(reported.y, 3.0);
        QCOMPARE(reported.theta, 0.25);
    }

    void malformedSafetyCannotReleaseLatch_data()
    {
        QTest::addColumn<QJsonObject>("payload");
        const QJsonObject normal{{"mode", "auto"}, {"state", "normal"}, {"state_fresh", true}};
        QTest::newRow("missing-estop") << normal;
        auto value = normal; value["estop"] = "false";
        QTest::newRow("string-estop") << value;
        value["estop"] = QJsonValue::Null;
        QTest::newRow("null-estop") << value;
        value["estop"] = false; value["state_fresh"] = "true";
        QTest::newRow("malformed-freshness") << value;
    }
    void malformedSafetyCannotReleaseLatch()
    {
        QFETCH(QJsonObject, payload);
        Remote remote;
        net::BridgeClient link("127.0.0.1", remote.server.serverPort());
        robot::Telemetry reported;
        connect(&link, &robot::RobotLink::telemetry, this,
            [&reported](const robot::Telemetry &tm) { reported = tm; });
        QSignalSpy modes(&link, &robot::RobotLink::driveModeReported);
        link.connectToBridge();
        QTRY_VERIFY(remote.peer && remote.peer->state() == QAbstractSocket::ConnectedState && link.isConnected());
        remote.send(net::makePublish(QLatin1String(ch::kSafety),
            {{"estop", true}, {"mode", "manual"}, {"state", "e_stop_latched"}, {"state_fresh", true}}));
        QTRY_VERIFY(reported.estop && reported.safetyFresh);
        QCOMPARE(modes.size(), 1);
        remote.send(net::makePublish(QLatin1String(ch::kSafety), payload));
        QTRY_VERIFY(!reported.safetyFresh);
        QVERIFY(reported.estop);
        QVERIFY(link.estopEngaged());
        QCOMPARE(link.mode(), robot::DriveMode::Manual);
        QCOMPARE(modes.size(), 1);

        // Omitted state_fresh remains compatible with earlier bridge reports.
        remote.send(net::makePublish(QLatin1String(ch::kSafety),
            {{"estop", false}, {"mode", "auto"}, {"state", "normal"}}));
        QTRY_VERIFY(!reported.estop && reported.safetyFresh);
        QCOMPARE(link.mode(), robot::DriveMode::Auto);
    }
};

QTEST_MAIN(TestSafetyPending)
#include "test_safety_pending.moc"
