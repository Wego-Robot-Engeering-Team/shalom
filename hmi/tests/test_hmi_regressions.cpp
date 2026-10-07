// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QApplication>
#include <QBuffer>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QImage>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cmath>
#include <limits>

#include "Config.h"
#include "MainWindow.h"
#include "TestRobot.h"
#include "net/BridgeClient.h"
#include "net/Channels.h"
#include "panels/ArmPanel.h"
#include "panels/CapturePanel.h"
#include "panels/DataPanel.h"
#include "panels/LocationPanel.h"
#include "panels/MissionLibraryPanel.h"
#include "panels/WaypointPanel.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"
#include "widgets/ValueSlider.h"

using namespace hmi;

namespace {
QVariantMap point()
{
    return {{"id", "wp"}, {"name", "Point"}, {"x", 1.0}, {"y", 2.0}, {"theta", 0.0}};
}

void acceptDelete()
{
    if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
        dialog->button(QMessageBox::Yes)->click();
}

class Remote : public QObject {
public:
    Remote()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, this, [this] {
                decoder.append(peer->readAll());
                net::Frame frame;
                while (decoder.next(frame) == net::FrameDecoder::Status::Ok) {
                    if (auto envelope = net::Envelope::fromHeader(frame.header)) {
                        requests << *envelope;
                        if (envelope->t == QLatin1String(net::mtype::kHb))
                            send(net::makeHeartbeat(envelope->seq.value_or(0)));
                    }
                }
            });
        });
    }
    void send(const net::Envelope &envelope)
    {
        peer->write(net::encodeFrame(envelope.toHeader(), envelope.payload));
        peer->flush();
    }
    net::Envelope last(const char *channel) const
    {
        for (auto it = requests.crbegin(); it != requests.crend(); ++it)
            if (it->ch == QLatin1String(channel) && it->t == QLatin1String(net::mtype::kReq))
                return *it;
        return {};
    }
    bool map(net::BridgeClient *link)
    {
        link->connectToBridge();
        if (!QTest::qWaitFor([&] { return peer && link->isConnected(); })) return false;
        QSignalSpy active(link, &net::BridgeClient::activeMapReceived);
        send(net::makePublish(QLatin1String(ch::kActiveMap), {{"id", "map-1"}}));
        if (!QTest::qWaitFor([&] { return active.size() == 1; })) return false;
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        emit link->mapReceived(png, {{"width", 100}, {"height", 100},
            {"resolution", 0.1}, {"map_id", "map-1"}});
        return true;
    }
    QTcpServer server;
    QTcpSocket *peer = nullptr;
    net::FrameDecoder decoder;
    QList<net::Envelope> requests;
};
}

class TestHmiRegressions : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        Config::instance().setRobots({});
    }

    void uneditedArmFollowsExternalMotionButPreservesDraft()
    {
        ui::ArmPanel arm;
        arm.setControlsEnabled(true);
        arm.setExecutionAvailable(true);
        QList<double> q{0.0, -0.785, 0.0, -2.356, 0.0, 1.571};
        arm.setArmState(q, 1.0, 1.0, "idle");
        q[1] += 0.5;
        arm.setArmState(q, 1.0, 1.0, "idle");
        auto *j1 = arm.findChild<ui::ValueSlider *>("Joint1");
        auto *j2 = arm.findChild<ui::ValueSlider *>("Joint2");
        QVERIFY(j1 && j2);
        QVERIFY(std::abs(j2->command() - q[1]) < 0.01);
        j1->setCommand(0.2);
        const auto draft = j1->command();
        q[0] = 0.7;
        q[1] += 0.4;
        arm.setArmState(q, 1.0, 1.0, "idle");
        QCOMPARE(j1->command(), draft);
        QSignalSpy goals(&arm, &ui::ArmPanel::jointGoal);
        for (auto *button : arm.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("관절 목표 보내기")) button->click();
        QCOMPARE(goals.size(), 1);
        const auto sent = qvariant_cast<QList<double>>(goals.first().first());
        QVERIFY(std::abs(sent[1] - (q[1] - 0.4)) < 0.01);
    }

    void waypointDeleteRevalidatesCatalog_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("unchanged") << 0;
        QTest::newRow("new-catalog") << 1;
        QTest::newRow("disconnect-reconnect") << 2;
    }
    void waypointDeleteRevalidatesCatalog()
    {
        QFETCH(int, change);
        ui::WaypointPanel panel;
        panel.setEditingEnabled(true);
        panel.setWaypoints({point()});
        auto *list = panel.findChild<QListWidget *>();
        auto *row = dynamic_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row);
        row->editButton()->click();
        QSignalSpy deletions(&panel, &ui::WaypointPanel::deleteRequested);
        QTimer::singleShot(0, &panel, [&] {
            if (change == 1) {
                auto changed = point(); changed["x"] = 8.0;
                panel.setWaypoints({changed});
            } else if (change == 2) {
                panel.setEditingEnabled(false); panel.setEditingEnabled(true);
            }
            acceptDelete();
        });
        panel.findChild<QPushButton *>("WaypointRowDelete")->click();
        QCOMPARE(deletions.size(), change == 0 ? 1 : 0);
    }

    void missionDeleteRevalidatesContext_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("unchanged") << 0;
        QTest::newRow("map-switch") << 1;
        QTest::newRow("revision-change") << 2;
        QTest::newRow("disconnect-reconnect") << 3;
    }
    void missionDeleteRevalidatesContext()
    {
        QFETCH(int, change);
        ui::MissionLibraryPanel panel;
        panel.setMapId("map-a");
        QVariantMap mission{{"id", "mission"}, {"name", "Mission"}, {"revision", 1},
            {"steps", QVariantList{}}};
        panel.setMissions({mission});
        auto *button = panel.findChild<QPushButton *>("MissionDelete_mission");
        QVERIFY(button);
        QSignalSpy deletions(&panel, &ui::MissionLibraryPanel::archiveRequested);
        QTimer::singleShot(0, &panel, [&] {
            if (change == 1) { panel.setMapId("map-b"); panel.setMissions({mission}); }
            if (change == 2) { mission["revision"] = 2; panel.setMissions({mission}); }
            if (change == 3) { panel.setEditingEnabled(false); panel.setEditingEnabled(true); }
            acceptDelete();
        });
        button->click();
        QCOMPARE(deletions.size(), change == 0 ? 1 : 0);
    }

    void captureChecksLinearAngularAndUnknownMotion()
    {
        auto *link = new test::TestRobot;
        ui::MainWindow window(link);
        auto *panel = window.findChild<ui::CapturePanel *>();
        for (auto *field : panel->findChildren<QLineEdit *>())
            field->setText("A01");
        auto *button = panel->findChild<QPushButton *>("CaptureTriggerButton");
        robot::Telemetry tm;
        tm.captureEnabled = tm.nasOnline = tm.poseFresh = true;
        tm.angularSpeed = 0.5;
        emit link->telemetry(tm);
        QVERIFY(!button->isEnabled());
        tm.angularSpeed = 0;
        emit link->telemetry(tm);
        QVERIFY(button->isEnabled());
        tm.speed = 0.04;
        emit link->telemetry(tm);
        QVERIFY(!button->isEnabled());
        tm.captureMaxLinearSpeed = 0.1;
        emit link->telemetry(tm);
        QVERIFY(button->isEnabled());
        tm.angularSpeed = std::numeric_limits<double>::quiet_NaN();
        emit link->telemetry(tm);
        QVERIFY(!button->isEnabled());
    }

    void markerEditPreservesOtherUpdates_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("other-marker") << 0;
        QTest::newRow("same-marker") << 1;
        QTest::newRow("map-switch") << 2;
    }
    void markerEditPreservesOtherUpdates()
    {
        QFETCH(int, change);
        Remote remote;
        auto *link = new net::BridgeClient("127.0.0.1", remote.server.serverPort());
        ui::MainWindow window(link);
        QVERIFY(remote.map(link));
        auto *panel = window.findChild<ui::LocationPanel *>();
        QVariantMap a{{"id", 1}, {"x", 1.0}, {"y", 0.0}, {"z", 1.2}, {"yaw", 0.0}};
        QVariantMap b{{"id", 2}, {"x", 2.0}, {"y", 0.0}, {"z", 1.2}, {"yaw", 0.0}};
        panel->setMarkers({a, b});
        QTimer::singleShot(0, &window, [&] {
            auto latest = change == 1 ? a : b;
            latest["x"] = 8.0;
            panel->setMarkers(change == 1 ? QList<QVariantMap>{latest, b} : QList<QVariantMap>{a, latest});
            if (change == 2) emit link->activeMapReceived({{"id", "map-2"}});
            auto *dialog = QApplication::activeModalWidget();
            dialog->findChild<QDoubleSpinBox *>("MarkerX")->setValue(4.0);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        emit panel->editMarkerRequested(0);
        if (change == 0) {
            QTRY_VERIFY(!remote.last(ch::kCmdMarkersSet).id.isEmpty());
            const auto payload = remote.last(ch::kCmdMarkersSet).p;
            QCOMPARE(payload.value("markers").toArray().at(1).toObject().value("x").toDouble(), 8.0);
            QCOMPARE(payload.value("markers").toArray().at(0).toObject().value("x").toDouble(), 4.0);
            QCOMPARE(payload.value("expected_markers").toArray().at(0).toObject().value("x").toDouble(), 1.0);
            QCOMPARE(payload.value("map_id").toString(), QStringLiteral("map-1"));
        } else {
            QTest::qWait(50);
            QVERIFY(remote.last(ch::kCmdMarkersSet).id.isEmpty());
        }
    }

    void bridgeUsesRobotSnapshotAndCaptureThresholds()
    {
        Remote remote;
        net::BridgeClient link("127.0.0.1", remote.server.serverPort());
        QVERIFY(remote.map(&link));
        robot::Telemetry reported;
        connect(&link, &robot::RobotLink::telemetry, this,
                [&reported](const robot::Telemetry &tm) { reported = tm; });
        remote.send(net::makePublish(QLatin1String(ch::kSystem),
            {{"capture_enabled", true}, {"capture_max_linear_speed", 0.01},
             {"capture_max_angular_speed", 0.02}}));
        const QJsonObject dock{{"kind", "dock"}, {"x", 1.0}, {"y", 2.0}, {"theta", 0.0}};
        remote.send(net::makePublish(QLatin1String(ch::kLocations), {{"locations", QJsonArray{dock}}}));
        const QJsonObject marker{{"id", 1}, {"x", 1.0}, {"y", 2.0}, {"z", 1.0}, {"yaw", 0.0}};
        remote.send(net::makePublish(QLatin1String(ch::kMarkers), {{"markers", QJsonArray{marker}}}));
        QTRY_VERIFY(reported.captureEnabled && !link.dockPose().isEmpty() && link.markers().size() == 1);
        QCOMPARE(reported.captureMaxLinearSpeed, 0.01);
        QCOMPARE(reported.captureMaxAngularSpeed, 0.02);
        auto changedDock = dock.toVariantMap(); changedDock["x"] = 8.0;
        auto changedMarker = marker.toVariantMap(); changedMarker["x"] = 8.0;
        link.setLocations({changedDock});
        link.setMarkers({changedMarker});
        link.archiveMission("mission", 1);
        QTRY_VERIFY(!remote.last(ch::kCmdLocationsSet).id.isEmpty() &&
                    !remote.last(ch::kCmdMarkersSet).id.isEmpty() &&
                    !remote.last(ch::kCmdMissionsArchive).id.isEmpty());
        QCOMPARE(remote.last(ch::kCmdLocationsSet).p.value("expected_locations").toArray(), QJsonArray{dock});
        QCOMPARE(remote.last(ch::kCmdMarkersSet).p.value("expected_markers").toArray(), QJsonArray{marker});
        QCOMPARE(remote.last(ch::kCmdMarkersSet).p.value("map_id").toString(), QStringLiteral("map-1"));
        QCOMPARE(remote.last(ch::kCmdMissionsArchive).p.value("map_id").toString(), QStringLiteral("map-1"));
    }

    void historyScanIsAsyncAndDiscardsPreviousDirectory()
    {
        QTemporaryDir first, second;
        QVERIFY(first.isValid() && second.isValid());
        QImage image(2, 2, QImage::Format_RGB32);
        image.fill(Qt::white);
        QVERIFY(image.save(first.filePath("A_01_P1,20261007120000.png")));
        QVERIFY(image.save(second.filePath("A_01_P2,20261007120000.png")));
        ui::DataPanel panel;
        QSignalSpy changed(&panel, &ui::DataPanel::recordsChanged);
        panel.setDirectory(first.path());
        QVERIFY(panel.isScanning());
        QVERIFY(panel.recordsForPoint("P1").isEmpty());
        panel.setDirectory(second.path());
        QTRY_VERIFY(!panel.isScanning());
        QVERIFY(panel.recordsForPoint("P1").isEmpty());
        QCOMPARE(panel.recordsForPoint("P2").size(), 1);
        QVERIFY(changed.size() >= 2);
        panel.setDirectory({});
        QVERIFY(panel.recordsForPoint("P2").isEmpty());
        QCOMPARE(panel.findChild<QListWidget *>("DataRecords")->count(), 0);
    }
};

QTEST_MAIN(TestHmiRegressions)
#include "test_hmi_regressions.moc"
