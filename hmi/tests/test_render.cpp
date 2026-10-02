// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

// 모든 화면을 실제로 그려 보는 연기 테스트.
//
// 다른 테스트는 계산과 규약만 확인한다. 그리기 코드는 한 번도 실행되지
// 않았고, 그래서 페인트 경로에서만 터지는 버그를 놓쳤다. 실제로:
// 무명 네임스페이스의 monoFont 헬퍼가 같은 이름의 theme::monoFont 대신
// 자기 자신을 부르는 바람에 로봇팔 화면을 여는 순간 무한 재귀로 죽었다.
// 모든 단위 테스트가 통과한 상태였다.
//
// grab() 은 페인트 이벤트를 동기로 돌린다. 화면이 없어도 되도록
// offscreen 플랫폼에서 실행한다.

#include <QStandardPaths>
#include <QTest>
#include <QtMath>

#include <QLabel>
#include <QBuffer>
#include <QDoubleSpinBox>
#include <QImage>
#include <QJsonArray>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTcpServer>
#include <QTcpSocket>

#include <limits>

#include "Config.h"
#include "RobotDef.h"
#include "MainWindow.h"
#include "mapview/MapView.h"
#include "net/BridgeClient.h"
#include "net/Channels.h"
#include "panels/ArmPanel.h"
#include "panels/CapturePanel.h"
#include "panels/LocationPanel.h"
#include "panels/MissionPanel.h"
#include "panels/StatusPanel.h"
#include "panels/NavigationSpeedPanel.h"
#include "panels/TeleopPanel.h"
#include "panels/WaypointPanel.h"
#include "robot/Kinematics.h"
#include "TestRobot.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "views/SettingsDialog.h"
#include "views/WelcomeDialog.h"
#include "widgets/NotificationCenter.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"
#include "widgets/MapCard.h"
#include "widgets/Robot3DView.h"
#include "widgets/ValueSlider.h"

using namespace hmi;

namespace {

class CatalogBridge : public QObject {
public:
    CatalogBridge()
    {
        server.listen(QHostAddress::LocalHost);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            peer = server.nextPendingConnection();
            connect(peer, &QTcpSocket::readyRead, this, [this] {
                decoder.append(peer->readAll());
                net::Frame frame;
                while (decoder.next(frame) == net::FrameDecoder::Status::Ok)
                    if (const auto envelope = net::Envelope::fromHeader(frame.header))
                        received << *envelope;
            });
        });
    }
    net::Envelope lastRequest(const char *channel) const
    {
        for (auto it = received.crbegin(); it != received.crend(); ++it)
            if (it->t == QLatin1String(net::mtype::kReq) && it->ch == QLatin1String(channel))
                return *it;
        return {};
    }
    void send(const net::Envelope &envelope)
    {
        peer->write(net::encodeFrame(envelope.toHeader(), envelope.payload));
        peer->flush();
    }
    QTcpServer server;
    QTcpSocket *peer = nullptr;
    net::FrameDecoder decoder;
    QList<net::Envelope> received;
};

/// 창을 만들어 한 화면씩 그려 본다. 테마마다 색을 다시 계산하는 위젯이
/// 있어 두 테마 모두 돌린다.
void paintEveryView(const QString &theme)
{
    theme::setTheme(theme);
    qApp->setStyleSheet(theme::buildQss());

    auto *robot = new test::TestRobot;
    ui::MainWindow window(robot);
    window.resize(1400, 900);
    window.show();

    static const ui::NavItem kAll[] = {
        ui::NavItem::Drive,       ui::NavItem::Mission,    ui::NavItem::Arm,
        ui::NavItem::Capture,     ui::NavItem::Diagnostics,
        ui::NavItem::Data,        ui::NavItem::Events,
    };

    for (ui::NavItem item : kAll) {
        window.showView(item);
        QVERIFY2(!window.grab().isNull(),
                 qPrintable(QStringLiteral("%1 테마에서 화면 %2 를 그리지 못했다")
                                .arg(theme)
                                .arg(int(item))));
    }

    // 수동 모드로 바꾼 뒤 주행 화면도 다시 그린다. 본체 조작이 주행에
    // 통합된 뒤에도 모드 전환이 화면을 깨뜨리지 않는지 본다.
    window.setDriveMode(QStringLiteral("manual"));
    window.showView(ui::NavItem::Drive);
    QVERIFY(!window.grab().isNull());
}

/// 두 프레임이 실제로 달라졌는지 본다. QImage 의 내부 저장소가 달라도
/// 그림이 같을 수 있으므로 포인터나 cache key가 아니라 픽셀을 비교한다.
int changedPixels(const QImage &before, const QImage &after)
{
    if (before.size() != after.size() || before.format() != after.format())
        return -1;

    int changed = 0;
    for (int y = 0; y < before.height(); ++y) {
        for (int x = 0; x < before.width(); ++x) {
            if (before.pixel(x, y) != after.pixel(x, y))
                ++changed;
        }
    }
    return changed;
}

}  // namespace

class TestRender : public QObject {
    Q_OBJECT

private slots:
    void goalSelectionRequiresExplicitStart()
    {
        CatalogBridge bridge;
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), bridge.server.serverPort());
        ui::MainWindow window(link);
        window.resize(1400, 900);
        window.show();
        link->connectToBridge();
        QTRY_VERIFY(bridge.peer && link->isConnected());
        auto *panel = window.findChild<ui::StatusPanel *>();
        auto *map = window.findChild<ui::MapCard *>();
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        const auto setMap = [&](const QString &id) {
            emit link->activeMapReceived({{"id", id}, {"name", id}});
            emit link->mapReceived(png, {{"width", 100}, {"height", 100}, {"resolution", 0.1}, {"map_id", id}});
        };
        setMap(QStringLiteral("map-1"));
        bridge.send(net::makePublish(QLatin1String(ch::kSafety), {{"mode", "auto"}}));
        bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", "idle"}}));
        QTRY_VERIFY(panel->goalButton()->isEnabled());
        QVERIFY(!panel->startButton()->isEnabled());
        panel->goalButton()->click();
        emit map->view()->goalRequested(4.2, 1.8, 1.2);
        QTRY_VERIFY(panel->startButton()->isEnabled());
        QCOMPARE(map->view()->draftGoal().value("theta").toDouble(), 1.2);
        bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", "idle"}}));
        QTest::qWait(150);
        QVERIFY(bridge.lastRequest(ch::kCmdGoto).id.isEmpty());
        QVERIFY(!map->view()->draftGoal().isEmpty());

        bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", "idle"}, {"navigation_state", "inactive"}}));
        QTRY_VERIFY(!panel->startButton()->isEnabled());
        QVERIFY(panel->goalButton()->isEnabled());
        bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", "idle"}, {"navigation_state", "active"}}));
        QTRY_VERIFY(panel->startButton()->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!panel->startButton()->isEnabled(), 2000);
        QVERIFY(!map->view()->draftGoal().isEmpty());
        bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", "idle"}, {"navigation_state", "active"}}));
        QTRY_VERIFY(panel->startButton()->isEnabled());

        // Cancel before starting is local: no robot command or movement.
        panel->navCancelButton()->click();
        QVERIFY(map->view()->draftGoal().isEmpty());
        QVERIFY(!panel->startButton()->isEnabled());
        QTest::qWait(30);
        QVERIFY(bridge.lastRequest(ch::kCmdNavCancel).id.isEmpty());

        emit map->view()->goalRequested(2.5, -1.0, -0.7);
        panel->startButton()->click();
        panel->startButton()->click();
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdGoto).id.isEmpty());
        auto request = bridge.lastRequest(ch::kCmdGoto);
        QCOMPARE(request.p.value("x").toDouble(), 2.5);
        QCOMPARE(request.p.value("y").toDouble(), -1.0);
        QCOMPARE(request.p.value("theta").toDouble(), -0.7);
        int count = 0;
        for (const auto &e : bridge.received) if (e.ch == QLatin1String(ch::kCmdGoto)) ++count;
        QCOMPARE(count, 1);
        QVERIFY(!panel->startButton()->isEnabled());
        bridge.send(net::makeResponse(request, false, QStringLiteral("E_BUSY"), QStringLiteral("속도 설정 중")));
        QTRY_VERIFY(panel->startButton()->isEnabled());
        QCOMPARE(panel->findChild<QLabel *>(QStringLiteral("NavigationError"))->text(), QStringLiteral("속도 설정 중"));
        panel->startButton()->click();
        QTRY_VERIFY(bridge.lastRequest(ch::kCmdGoto).id != request.id);
        bridge.send(net::makeResponse(bridge.lastRequest(ch::kCmdGoto), true));
        bridge.send(net::makePublish(QLatin1String(ch::kNav),
            {{"status", "navigating"}, {"goal", request.p}, {"navigation_state", "active"},
             {"localization_state", "active"}, {"distance_remaining_m", 4.2}, {"eta_s", 18.0},
             {"elapsed_s", 3.0}, {"recoveries", 1}}));
        QTRY_VERIFY(map->view()->draftGoal().isEmpty());
        QTRY_COMPARE(panel->findChild<QLabel *>(QStringLiteral("NavigationDistance"))->text(), QStringLiteral("4.2 m"));
        QCOMPARE(panel->findChild<QLabel *>(QStringLiteral("NavigationEta"))->text(), QStringLiteral("18 s"));
        QVERIFY(panel->navPauseButton()->isEnabled());

        bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", "succeeded"}}));
        QTRY_VERIFY(panel->goalButton()->isEnabled());
        emit map->view()->goalRequested(1.0, 2.0, 0.0);
        setMap(QStringLiteral("map-2"));
        QVERIFY(map->view()->draftGoal().isEmpty());
        QVERIFY(!panel->startButton()->isEnabled());
        emit map->view()->goalRequested(1.0, 2.0, 0.0);
        link->disconnectFromBridge();
        QTRY_VERIFY(map->view()->draftGoal().isEmpty());
        QVERIFY(!panel->startButton()->isEnabled());
    }

    void waypointSelectionStagesGoalInOperationPanel()
    {
        theme::setTheme(QStringLiteral("light"));
        qApp->setStyleSheet(theme::buildQss());
        CatalogBridge bridge;
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), bridge.server.serverPort());
        ui::MainWindow window(link);
        window.resize(1400, 900);
        window.show();
        link->connectToBridge();
        QTRY_VERIFY(bridge.peer && link->isConnected());
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
        emit link->activeMapReceived({{"id", "map-1"}, {"name", "map-1"}});
        emit link->mapReceived(png, {{"width", 100}, {"height", 100}, {"resolution", 0.1}, {"map_id", "map-1"}});
        bridge.send(net::makePublish(QLatin1String(ch::kSafety), {{"mode", "manual"}}));
        bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", "idle"}, {"navigation_state", "active"}, {"localization_state", "active"}}));
        QTRY_COMPARE(link->mode(), robot::DriveMode::Manual);
        auto *waypoints = window.findChild<ui::WaypointPanel *>();
        waypoints->setWaypoints({{{"id", "wp-1"}, {"name", "입구"}, {"x", 2.0}, {"y", 3.0}, {"theta", 0.9}}});
        emit waypoints->gotoRequested(QStringLiteral("wp-1"));
        auto *panel = window.findChild<ui::StatusPanel *>();
        auto *map = window.findChild<ui::MapCard *>();
        QTRY_VERIFY(!map->view()->draftGoal().isEmpty());
        QCOMPARE(map->view()->draftGoal().value("theta").toDouble(), 0.9);
        QVERIFY(panel->findChild<QLabel *>(QStringLiteral("NavigationTarget"))->isVisible());
        QVERIFY(!panel->startButton()->isEnabled());
        bridge.send(net::makePublish(QLatin1String(ch::kSafety), {{"mode", "auto"}}));
        QTRY_VERIFY(panel->startButton()->isEnabled());
        QVERIFY(bridge.lastRequest(ch::kCmdGoto).id.isEmpty());
        const auto snapshot = qEnvironmentVariable("HMI_NAV_SCREENSHOT");
        QTest::qWait(60);
        if (!snapshot.isEmpty()) {
            QVERIFY(window.grab().save(snapshot));
            theme::setTheme(QStringLiteral("dark"));
            qApp->setStyleSheet(theme::buildQss());
            window.resize(1100, 780);
            QTest::qWait(60);
            QVERIFY(window.grab().save(snapshot + QStringLiteral("-dark.png")));
        }
        bridge.send(net::makePublish(QLatin1String(ch::kMission),
            {{"state", "running"}, {"mission_id", "mission-1"}, {"current_index", 0}, {"total", 2}}));
        auto *mission = panel->findChild<QPushButton *>(QStringLiteral("NavigationMissionButton"));
        QTRY_VERIFY(mission->isVisible());
        QVERIFY(map->view()->draftGoal().isEmpty());
        QVERIFY(!panel->startButton()->isEnabled());
        QVERIFY(panel->goalButton()->isHidden());
        QCOMPARE(panel->findChild<QLabel *>(QStringLiteral("NavigationDistance"))->text(), QStringLiteral("—"));
        theme::setTheme(QStringLiteral("light"));
        qApp->setStyleSheet(theme::buildQss());
    }

    void mapSwitchRestoresCatalogPublishedBeforeAcknowledgement()
    {
        CatalogBridge bridge;
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), bridge.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(bridge.peer && link->isConnected());
        bridge.send(net::makePublish(QLatin1String(ch::kMaps),
            {{"maps", QJsonArray{QJsonObject{{"id", "map-2"}, {"name", "map-2"}}}}}));
        QTRY_VERIFY(window.findChild<ui::MapCard *>()->mapButton()->isEnabled());
        QTest::qWait(40);
        window.findChild<ui::MapCard *>()->mapButton()->click();
        auto *select = window.findChild<QPushButton *>(QStringLiteral("MapSelect_map-2"));
        QVERIFY(select);
        select->click();
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdMapsSelect).id.isEmpty());
        const auto request = bridge.lastRequest(ch::kCmdMapsSelect);
        bridge.send(net::makePublish(QLatin1String(ch::kActiveMap), {{"id", "map-2"}, {"name", "map-2"}}));
        bridge.send(net::makePublish(QLatin1String(ch::kWaypoints),
            {{"map_id", "map-2"}, {"points", QJsonArray{QJsonObject{{"id", "wp-2"},
                {"name", "입구"}, {"x", 1.0}, {"y", 2.0}, {"theta", 0.5}}}}}));
        QTRY_COMPARE(link->waypoints().size(), 1);
        auto *panel = window.findChild<ui::WaypointPanel *>();
        QCOMPARE(panel->waypoints().size(), 0);
        bridge.send(net::makeResponse(request, true));
        QTRY_COMPARE(panel->waypoints().size(), 1);
        QCOMPARE(panel->waypoints().first().value("id").toString(), QStringLiteral("wp-2"));
    }

    void waypointWriteWaitsForAcknowledgementAndKeepsFailedDraft()
    {
        CatalogBridge bridge;
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), bridge.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(bridge.peer && link->isConnected());
        bridge.send(net::makePublish(QLatin1String(ch::kActiveMap), {{"id", "map-1"}, {"name", "map-1"}}));
        const QJsonObject point{{"id", "wp-1"}, {"name", "원본"}, {"x", 1.0}, {"y", 2.0}, {"theta", 0.0}};
        const auto catalog = net::makePublish(QLatin1String(ch::kWaypoints),
            {{"map_id", "map-1"}, {"points", QJsonArray{point}}});
        bridge.send(catalog);
        auto *panel = window.findChild<ui::WaypointPanel *>();
        QTRY_COMPARE(panel->waypoints().size(), 1);
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        emit link->mapReceived(png, {{"width", 8}, {"height", 8}, {"resolution", 0.1}, {"map_id", "map-1"}});
        panel->setEditingEnabled(true);
        auto *list = panel->findChild<QListWidget *>();
        auto *row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        row->editButton()->click();
        row->findChild<QLineEdit *>(QStringLiteral("WaypointRowName"))->setText(QStringLiteral("수정"));
        row->findChild<QPushButton *>(QStringLiteral("WaypointRowSave"))->click();
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdWaypointsSet).id.isEmpty());
        const auto request = bridge.lastRequest(ch::kCmdWaypointsSet);
        bridge.send(catalog);
        QTest::qWait(50);
        auto *status = panel->findChild<QLabel *>(QStringLiteral("WaypointSaveStatus"));
        QVERIFY(!status->text().contains(QStringLiteral("저장됨")));
        QVERIFY(!row->editButton()->isEnabled());
        bridge.send(net::makeResponse(request, false, QStringLiteral("E_BUSY"), QStringLiteral("conflict")));
        QTRY_VERIFY(status->text().contains(QStringLiteral("저장 실패")));
        row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QCOMPARE(row->findChild<QLineEdit *>(QStringLiteral("WaypointRowName"))->text(), QStringLiteral("수정"));
        panel->setEditingEnabled(true);
        row->findChild<QPushButton *>(QStringLiteral("WaypointRowSave"))->click();
        QTRY_VERIFY(bridge.lastRequest(ch::kCmdWaypointsSet).id != request.id);
        bridge.send(net::makeResponse(bridge.lastRequest(ch::kCmdWaypointsSet), true));
        QTRY_COMPARE(panel->waypoints().first().value("name").toString(), QStringLiteral("수정"));
        QVERIFY(status->text().contains(QStringLiteral("저장됨")));
    }

    void goalNavigationPauseResumeCancel()
    {
        CatalogBridge bridge;
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), bridge.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(bridge.peer && link->isConnected());
        auto *card = window.findChild<ui::StatusPanel *>();
        auto *pause = card->navPauseButton();
        auto *cancel = card->navCancelButton();
        QVERIFY(!pause->isEnabled());
        QVERIFY(!cancel->isEnabled());
        bridge.send(net::makePublish(QLatin1String(ch::kSafety), {{"mode", "auto"}}));
        const QJsonObject goal{{"x", 1.0}, {"y", 2.0}, {"theta", 0.5}};
        auto report = [&](const char *status) {
            bridge.send(net::makePublish(QLatin1String(ch::kNav), {{"status", status}, {"goal", goal}}));
        };
        report("navigating");
        QTRY_VERIFY(pause->isEnabled());
        QVERIFY(cancel->isEnabled());
        QVERIFY(!card->goalButton()->isEnabled());
        pause->click();
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdNavPause).id.isEmpty());
        report("pausing");
        QTRY_VERIFY(!pause->isEnabled());
        report("paused");
        QTRY_COMPARE(pause->text(), QStringLiteral("재개"));
        QVERIFY(pause->isEnabled());
        pause->click();
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdNavResume).id.isEmpty());
        report("navigating");
        QTRY_COMPARE(pause->text(), QStringLiteral("일시정지"));
        cancel->click();
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdNavCancel).id.isEmpty());
        report("canceled");
        QTRY_VERIFY(!cancel->isEnabled());
        QVERIFY(!pause->isEnabled());
        report("paused");
        QTRY_VERIFY(pause->isEnabled());
        link->disconnectFromBridge();
        QTRY_VERIFY(!pause->isEnabled());
        QVERIFY(!cancel->isEnabled());
    }

    void rapidModeRequestsUnlockGoalWithUnchangedFinalReport()
    {
        CatalogBridge bridge;
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), bridge.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(bridge.peer && link->isConnected());
        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        emit link->activeMapReceived({{"id", "map-1"}, {"name", "map-1"}});
        emit link->mapReceived(png, {{"width", 8}, {"height", 8}, {"resolution", 0.1}, {"map_id", "map-1"}});
        const auto automatic = net::makePublish(QLatin1String(ch::kSafety), {{"mode", "auto"}});
        bridge.send(automatic);
        auto *goal = window.findChild<ui::StatusPanel *>()->goalButton();
        QTRY_VERIFY(goal->isEnabled());
        window.setDriveMode(QStringLiteral("manual"));
        window.setDriveMode(QStringLiteral("auto"));
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdMode).id.isEmpty());
        const auto first = bridge.lastRequest(ch::kCmdMode);
        QCOMPARE(first.p.value("mode").toString(), QStringLiteral("manual"));
        bridge.send(automatic);
        bridge.send(net::makeResponse(first, true));
        QTRY_VERIFY(bridge.lastRequest(ch::kCmdMode).id != first.id);
        QVERIFY(!goal->isEnabled());
        const auto last = bridge.lastRequest(ch::kCmdMode);
        QCOMPARE(last.p.value("mode").toString(), QStringLiteral("auto"));
        bridge.send(automatic);
        bridge.send(net::makeResponse(last, true));
        QTRY_VERIFY(goal->isEnabled());
        QVERIFY(!link->modeChangePending());
    }

    void armDraftRevisionAndOtherRowsSurviveMutationResults()
    {
        ui::ArmPanel panel;
        panel.setControlsEnabled(true);
        const QVariantList positions{0.0, -0.4, 0.5, -1.2, 0.0, 0.4};
        QVariantMap a{{"id", "a"}, {"name", "A"}, {"positions", positions}, {"revision", 1}};
        const QVariantMap b{{"id", "b"}, {"name", "B"}, {"positions", positions}, {"revision", 1}};
        panel.setPosePresets({a, b});
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        auto getRow = [list](int i) { return static_cast<ui::CatalogRow *>(list->itemWidget(list->item(i))); };
        getRow(0)->editButton()->click();
        getRow(0)->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->setText(QStringLiteral("A 초안"));
        auto *joint = panel.findChild<ui::ValueSlider *>(QStringLiteral("Joint1"));
        getRow(0)->findChild<QDoubleSpinBox *>(QStringLiteral("PoseRowJoint1"))->setValue(30.0);
        QVERIFY(qAbs(joint->command() - M_PI / 6) < 0.004);
        a["revision"] = 2;
        panel.setPosePresets({a, b});
        QSignalSpy writes(&panel, &ui::ArmPanel::updatePosePresetRequested);
        getRow(0)->findChild<QPushButton *>(QStringLiteral("PoseRowSave"))->click();
        QCOMPARE(writes.size(), 1);
        QCOMPARE(writes.first().at(1).toULongLong(), quint64{1});
        getRow(1)->editButton()->click();
        getRow(1)->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->setText(QStringLiteral("B 초안"));
        panel.setCommandResult(QStringLiteral("cmd/arm/pose_presets/update"), true, {}, {});
        QCOMPARE(getRow(1)->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->text(), QStringLiteral("B 초안"));
        QVERIFY(getRow(1)->isEditing());
        getRow(1)->findChild<QPushButton *>(QStringLiteral("PoseRowCancel"))->click();
        getRow(1)->editButton()->click();
        QCOMPARE(getRow(1)->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->text(), QStringLiteral("B"));
        getRow(1)->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->setText(QStringLiteral("B 새 초안"));
        panel.findChild<QPushButton *>(QStringLiteral("ArmSavePreviewPose"))->click();
        panel.setCommandResult(QStringLiteral("cmd/arm/pose_presets/save"), false, QStringLiteral("E_BUSY"), {});
        QCOMPARE(getRow(1)->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->text(), QStringLiteral("B 새 초안"));
    }

    void waypointCancelRestoresFieldsAndPreservesDraftAcrossReports()
    {
        ui::WaypointPanel panel;
        const QVariantMap original{{"id", "a"}, {"name", "A"}, {"x", 1.0}, {"y", 2.0}, {"theta", 0.0}};
        panel.setWaypoints({original});
        panel.setEditingEnabled(true);
        auto *list = panel.findChild<QListWidget *>();
        auto getRow = [list] { return static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0))); };
        getRow()->editButton()->click();
        getRow()->findChild<QLineEdit *>(QStringLiteral("WaypointRowName"))->setText(QStringLiteral("초안"));
        getRow()->findChild<QDoubleSpinBox *>(QStringLiteral("WaypointRowX"))->setValue(8.0);
        panel.setWaypoints({original});
        QCOMPARE(getRow()->findChild<QLineEdit *>(QStringLiteral("WaypointRowName"))->text(), QStringLiteral("초안"));
        getRow()->findChild<QPushButton *>(QStringLiteral("WaypointRowCancel"))->click();
        getRow()->editButton()->click();
        QCOMPARE(getRow()->findChild<QLineEdit *>(QStringLiteral("WaypointRowName"))->text(), QStringLiteral("A"));
        QCOMPARE(getRow()->findChild<QDoubleSpinBox *>(QStringLiteral("WaypointRowX"))->value(), 1.0);
    }

    void staleArmFeedbackDisablesExecutionButKeepsPreviewSaving()
    {
        ui::ArmPanel panel;
        panel.setControlsEnabled(true);
        panel.setExecutionAvailable(true);
        panel.setArmState({0.0, -0.4, 0.5, -1.2, 0.0, 0.4}, 0.1, 0.1);
        auto *save = panel.findChild<QPushButton *>(QStringLiteral("ArmSaveCurrentPose"));
        QVERIFY(save->isEnabled());
        panel.setFeedbackFresh(false);
        QVERIFY(!save->isEnabled());
        QVERIFY(panel.findChild<QPushButton *>(QStringLiteral("ArmSavePreviewPose"))->isEnabled());
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        for (auto *button : panel.findChildren<QPushButton *>())
            if (button->text().contains(QStringLiteral("전송")))
                QVERIFY(!button->isEnabled());
        QCOMPARE(goals.size(), 0);
    }

    void sharedSpeedPreservesDraftAndWaitsForConfirmation()
    {
        ui::NavigationSpeedPanel panel;
        auto *value = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedValue"));
        auto *angular = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationAngularSpeedValue"));
        auto *apply = panel.findChild<QPushButton *>(QStringLiteral("NavigationSpeedApply"));
        QVERIFY(value && angular && apply);
        QVERIFY(!value->isEnabled());
        QVERIFY(!angular->isEnabled());
        panel.setReportedLimits(0.30, 0.10, 0.60, 0.50, 0.05, 0.80, true);
        QCOMPARE(value->value(), 0.30);
        QCOMPARE(angular->value(), 0.50);
        QVERIFY(!apply->isEnabled());
        value->setValue(0.40);
        angular->setValue(0.65);
        panel.setReportedLimits(0.30, 0.10, 0.60, 0.50, 0.05, 0.80, true);
        QCOMPARE(value->value(), 0.40);
        QCOMPARE(angular->value(), 0.65);
        QSignalSpy requests(&panel, &ui::NavigationSpeedPanel::speedLimitsRequested);
        apply->click();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first().first().toDouble(), 0.40);
        QCOMPARE(requests.first().at(1).toDouble(), 0.65);
        QVERIFY(!value->isEnabled());
        QVERIFY(!angular->isEnabled());
        panel.setReportedLimits(0.30, 0.10, 0.60, 0.50, 0.05, 0.80, true);
        QCOMPARE(value->value(), 0.40);
        QCOMPARE(angular->value(), 0.65);
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedLimit), false,
                                  QStringLiteral("E_UNREACHABLE"), QStringLiteral("저장 실패"));
        QVERIFY(value->isEnabled());
        QVERIFY(apply->isEnabled());
        QCOMPARE(value->value(), 0.40);
        apply->click();
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedLimit), true, {}, {});
        panel.setReportedLimits(0.40, 0.10, 0.60, 0.65, 0.05, 0.80, false);
        QVERIFY(!apply->isEnabled());
        QCOMPARE(value->value(), 0.40);
        auto *reported = panel.findChild<QLabel *>(QStringLiteral("NavigationSpeedReported"));
        QVERIFY(reported->text().contains(QStringLiteral("적용 대기")));
        panel.setReportedLimits(0.40, 0.10, 0.60, 0.65, 0.05, 0.80, true);
        QVERIFY(!reported->text().contains(QStringLiteral("적용 대기")));
        // Changing only rotation does not alter the linear draft.
        angular->setValue(0.45);
        apply->click();
        QCOMPARE(requests.last().at(0).toDouble(), 0.40);
        QCOMPARE(requests.last().at(1).toDouble(), 0.45);
        panel.reset();
        QVERIFY(!value->isEnabled());
        panel.setReportedLimits(0.20, 0.10, 0.40, 0.30, 0.10, 0.50, true);
        QCOMPARE(value->value(), 0.20);
        QCOMPARE(value->maximum(), 0.40);
        QCOMPARE(angular->value(), 0.30);
        QCOMPARE(angular->maximum(), 0.50);
    }

    void speedSettingsValidateBothRangesAndPreserveDrafts()
    {
        ui::NavigationSpeedPanel panel(nullptr, true);
        auto *minimum = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedSettingsMinimum"));
        auto *maximum = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedSettingsMaximum"));
        auto *angularMinimum = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationAngularSpeedSettingsMinimum"));
        auto *angularMaximum = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationAngularSpeedSettingsMaximum"));
        auto *apply = panel.findChild<QPushButton *>(QStringLiteral("NavigationSpeedSettingsApply"));
        QVERIFY(minimum && maximum && angularMinimum && angularMaximum && apply);
        QCOMPARE(panel.findChildren<QDoubleSpinBox *>().size(), 4);
        QVERIFY(panel.findChildren<QSlider *>().isEmpty());
        QVERIFY(!minimum->isEnabled());
        panel.setReportedLimits(0.30, 0.10, 0.60, 0.50, 0.05, 0.80, true);
        minimum->setValue(0.40);
        QVERIFY(apply->isEnabled());
        auto *adjustment = panel.findChild<QLabel *>(QStringLiteral("NavigationSpeedSettingsAdjustment"));
        QVERIFY(!adjustment->isHidden());
        QVERIFY(adjustment->text().contains(QStringLiteral("0.40 m/s")));
        maximum->setValue(0.35);
        QVERIFY(!apply->isEnabled());
        minimum->setValue(0.20);
        maximum->setValue(0.45);
        angularMinimum->setValue(0.10);
        angularMaximum->setValue(0.70);
        QVERIFY(apply->isEnabled());
        QVERIFY(adjustment->isHidden());
        panel.setReportedLimits(0.30, 0.10, 0.60, 0.50, 0.05, 0.80, true);
        QCOMPARE(minimum->value(), 0.20);
        QCOMPARE(maximum->value(), 0.45);
        // A speed change from the driving tab must not overwrite this range draft.
        panel.setReportedLimits(0.50, 0.10, 0.60, 0.50, 0.05, 0.80, true);
        QCOMPARE(maximum->value(), 0.45);
        QVERIFY(adjustment->text().contains(QStringLiteral("0.45 m/s")));
        QSignalSpy requests(&panel, &ui::NavigationSpeedPanel::speedRangesRequested);
        apply->click();
        QCOMPARE(requests.size(), 1);
        QCOMPARE(requests.first(), QList<QVariant>({0.20, 0.45, 0.10, 0.70}));
        QVERIFY(!minimum->isEnabled());
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedLimit), true, {}, {});
        QVERIFY(!minimum->isEnabled()); // Responses from the driving card are independent.
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedSettings), false, {}, QStringLiteral("저장 실패"));
        QVERIFY(apply->isEnabled());
        QCOMPARE(maximum->value(), 0.45);
        panel.discardDraft();
        QCOMPARE(minimum->value(), 0.10);
        QCOMPARE(maximum->value(), 0.60);
        QCOMPARE(angularMaximum->value(), 0.80);
        QVERIFY(adjustment->isHidden());
        QVERIFY(!apply->isEnabled());
        panel.reset();
        QVERIFY(!minimum->isEnabled());
        QVERIFY(!apply->isEnabled());
    }

    void settingsSpeedUsesRobotStateAndDiscardsUnsavedEdits()
    {
        ui::SettingsDialog dialog;
        dialog.setCurrentTab(2);
        auto *minimum = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedSettingsMinimum"));
        auto *maximum = dialog.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedSettingsMaximum"));
        auto *apply = dialog.findChild<QPushButton *>(QStringLiteral("NavigationSpeedSettingsApply"));
        QVERIFY(minimum && maximum && apply);
        QVERIFY(!minimum->isEnabled());
        QVERIFY(!dialog.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedSettingsValue")));
        dialog.setNavigationSpeedState(0.35, 0.20, 0.50, 0.45, 0.10, 0.70, true);
        QCOMPARE(minimum->value(), 0.20);
        QCOMPARE(maximum->value(), 0.50);
        maximum->setValue(0.30);
        QSignalSpy requests(&dialog, &ui::SettingsDialog::navigationSpeedRangesRequested);
        apply->click();
        QCOMPARE(requests.size(), 1);
        dialog.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedSettings), true, {}, {});
        dialog.setNavigationSpeedState(0.30, 0.20, 0.30, 0.45, 0.10, 0.70, true);
        QVERIFY(!apply->isEnabled());
        maximum->setValue(0.60);
        dialog.reload();
        QCOMPARE(maximum->value(), 0.30);
        dialog.resetNavigationSpeed();
        QVERIFY(!minimum->isEnabled());
    }

    void speedRangeChangeUpdatesDrivingSliders()
    {
        ui::NavigationSpeedPanel panel;
        panel.setReportedLimits(0.30, 0.10, 0.60, 0.50, 0.05, 0.80, true);
        auto *value = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedValue"));
        auto *angular = panel.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationAngularSpeedValue"));
        auto *slider = panel.findChild<QSlider *>(QStringLiteral("NavigationSpeedSlider"));
        auto *angularSlider = panel.findChild<QSlider *>(QStringLiteral("NavigationAngularSpeedSlider"));
        value->setValue(0.55);
        angular->setValue(0.75);
        panel.setReportedLimits(0.40, 0.20, 0.40, 0.60, 0.10, 0.60, true);
        QCOMPARE(value->minimum(), 0.20);
        QCOMPARE(value->maximum(), 0.40);
        QCOMPARE(value->value(), 0.40);
        QCOMPARE(slider->minimum(), 20);
        QCOMPARE(slider->maximum(), 40);
        QCOMPARE(angular->minimum(), 0.10);
        QCOMPARE(angular->maximum(), 0.60);
        QCOMPARE(angular->value(), 0.60);
        QCOMPARE(angularSlider->minimum(), 10);
        QCOMPARE(angularSlider->maximum(), 60);
    }

    void settingsSpeedConnectsToTheSelectedRobot()
    {
        CatalogBridge bridge;
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), bridge.server.serverPort());
        ui::MainWindow window(link);
        link->connectToBridge();
        QTRY_VERIFY(bridge.peer && link->isConnected());
        QJsonObject settings{{"speed_limit_mps", 0.35}, {"min_speed_mps", 0.20}, {"max_speed_mps", 0.50},
                             {"angular_speed_limit_rps", 0.45}, {"min_angular_speed_rps", 0.10},
                             {"max_angular_speed_rps", 0.70}, {"autonomous_applied", true}};
        bridge.send(net::makePublish(QLatin1String(ch::kNavigationSpeed), settings));
        auto *drive = window.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedValue"));
        QTRY_COMPARE(drive->value(), 0.35);
        QPushButton *settingsButton = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->toolTip() == QStringLiteral("설정")) settingsButton = button;
        QVERIFY(settingsButton);
        settingsButton->click(); // Received state must populate a lazily created settings window.
        auto *dialog = window.findChild<ui::SettingsDialog *>();
        QVERIFY(dialog);
        dialog->setCurrentTab(2);
        auto *maximum = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedSettingsMaximum"));
        auto *apply = dialog->findChild<QPushButton *>(QStringLiteral("NavigationSpeedSettingsApply"));
        QVERIFY(!dialog->findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedSettingsValue")));
        QCOMPARE(maximum->value(), 0.50);
        maximum->setValue(0.40);
        apply->click();
        QTRY_VERIFY(!bridge.lastRequest(ch::kCmdNavigationSpeedSettings).id.isEmpty());
        const auto request = bridge.lastRequest(ch::kCmdNavigationSpeedSettings);
        QCOMPARE(request.p.size(), 4);
        QVERIFY(!request.p.contains(QStringLiteral("speed_limit_mps")));
        QVERIFY(!request.p.contains(QStringLiteral("angular_speed_limit_rps")));
        QCOMPARE(request.p.value(QStringLiteral("max_speed_mps")).toDouble(), 0.40);
        bridge.send(net::makeResponse(request, true));
        settings[QStringLiteral("max_speed_mps")] = 0.40;
        bridge.send(net::makePublish(QLatin1String(ch::kNavigationSpeed), settings));
        QTRY_COMPARE(drive->maximum(), 0.40);
        QTRY_VERIFY(!apply->isEnabled());
        dialog->show();
        QCoreApplication::processEvents();
        QVERIFY(!dialog->grab().isNull());
        const int width = dialog->width();
        for (auto *input : dialog->findChildren<QDoubleSpinBox *>()) {
            if (!input->isVisible()) continue;
            const int right = input->mapTo(dialog, QPoint(input->width(), 0)).x();
            QVERIFY2(right <= width, qPrintable(QStringLiteral("속도 입력란이 설정 창 폭을 넘었습니다: %1/%2")
                                               .arg(right).arg(width)));
        }
        emit link->connectionChanged(false);
        QVERIFY(!maximum->isEnabled());
        dialog->close();
        settingsButton->click();
        QVERIFY(!maximum->isEnabled());
        emit link->navigationSpeedLimitsChanged(0.25, 0.10, 0.35, 0.30, 0.05, 0.50, true);
        QVERIFY(maximum->isEnabled());
        QCOMPARE(maximum->value(), 0.35);
    }

    void robotReportedSpeedAlsoUpdatesAnActiveManualJog()
    {
        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        window.showView(ui::NavItem::Drive);
        auto *teleop = window.findChild<ui::TeleopPanel *>();
        QVERIFY(teleop);
        teleop->setJogEnabled(true);
        emit robot->navigationSpeedLimitsChanged(0.20, 0.10, 0.60, 0.30, 0.05, 0.80, true);
        QSignalSpy commands(teleop, &ui::TeleopPanel::cmdVel);
        QPushButton *forward = nullptr;
        for (auto *button : teleop->findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("▲"))
                forward = button;
        QVERIFY(forward);
        // Invoke the same momentary signals without relying on screen geometry.
        QVERIFY(QMetaObject::invokeMethod(forward, "pressed"));
        QCOMPARE(commands.last().at(0).toDouble(), 0.20);
        emit robot->navigationSpeedLimitsChanged(0.40, 0.10, 0.60, 0.30, 0.05, 0.80, true);
        QCOMPARE(commands.last().at(0).toDouble(), 0.40);
        QVERIFY(QMetaObject::invokeMethod(forward, "released"));
        QCOMPARE(commands.last().at(0).toDouble(), 0.0);
        QPushButton *rotate = nullptr;
        for (auto *button : teleop->findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("↺")) rotate = button;
        QVERIFY(rotate);
        QVERIFY(QMetaObject::invokeMethod(rotate, "pressed"));
        QCOMPARE(commands.last().at(2).toDouble(), 0.30);
        emit robot->navigationSpeedLimitsChanged(0.20, 0.10, 0.60, 0.30, 0.05, 0.80, true);
        QCOMPARE(commands.last().at(2).toDouble(), 0.30);
        emit robot->navigationSpeedLimitsChanged(0.20, 0.10, 0.60, 0.60, 0.05, 0.80, true);
        QCOMPARE(commands.last().at(2).toDouble(), 0.60);
        QVERIFY(QMetaObject::invokeMethod(rotate, "released"));
        QCOMPARE(commands.last().at(2).toDouble(), 0.0);
        emit robot->connectionChanged(false);
        auto *value = window.findChild<QDoubleSpinBox *>(QStringLiteral("NavigationSpeedValue"));
        QVERIFY(value && !value->isEnabled());
    }

    void captureOnlyReportsSaveAfterRobotConfirms()
    {
        ui::CapturePanel panel;
        panel.setContext(1.0, 2.0, 0.0, -1);
        panel.setCaptureAllowed(true);
        for (auto *field : panel.findChildren<QLineEdit *>()) {
            if (field->placeholderText().contains(QStringLiteral("GTXA")))
                field->setText(QStringLiteral("GTXA-042"));
            else if (field->placeholderText().contains(QStringLiteral("1234")))
                field->setText(QStringLiteral("1234"));
            else if (field->placeholderText().contains(QStringLiteral("05")))
                field->setText(QStringLiteral("05"));
            else if (field->placeholderText().contains(QStringLiteral("C01")))
                field->setText(QStringLiteral("C01-P03"));
        }
        auto *trigger = panel.findChild<QPushButton *>(QStringLiteral("CaptureTriggerButton"));
        QVERIFY(trigger && trigger->isEnabled());
        auto *captureHint = panel.findChild<QLabel *>(QStringLiteral("Hint"));
        QVERIFY(captureHint && captureHint->isHidden());
        QSignalSpy requests(&panel, &ui::CapturePanel::captureRequested);
        trigger->click();
        QCOMPARE(requests.size(), 1);
        QVERIFY(!trigger->isEnabled());
        panel.captureFailed(QStringLiteral("카메라 영상 없음"));
        QVERIFY(trigger->isEnabled());
        trigger->click();
        panel.captureStored();
        panel.setSavedFileName(QStringLiteral("GTXA-042_05_C01-P03,20260929120000.png"));
        bool foundSavedName = false;
        for (const auto *label : panel.findChildren<QLabel *>())
            foundSavedName |= label->text().contains(QStringLiteral("20260929120000.png"));
        QVERIFY(foundSavedName);
        panel.resetCapture();
        QVERIFY(!trigger->isEnabled());
        for (const auto *label : panel.findChildren<QLabel *>())
            QVERIFY(!label->text().contains(QStringLiteral("20260929120000.png")));
    }

    void armWithoutReportedMetricDoesNotWarnAboutSingularity()
    {
        ui::ArmPanel arm;
        const QList<double> home{0.0, -0.785, 0.0, -2.356, 0.0, 1.571};
        arm.setArmState(home, std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::quiet_NaN(), {});
        QLabel *advice = nullptr;
        for (auto *label : arm.findChildren<QLabel *>()) {
            if (label->toolTip().contains(QStringLiteral("조작성 지수"))) {
                advice = label;
                break;
            }
        }
        QVERIFY(advice);
        QVERIFY(advice->isHidden());
        arm.setArmState(home, 0.0, 0.0, QStringLiteral("idle"));
        QVERIFY(!advice->isHidden());
    }

    void armPreviewStaysEditableWhenExecutionIsDisabled()
    {
        ui::ArmPanel arm;
        arm.setControlsEnabled(true);
        arm.setArmState({0.0, -0.785, 0.0, -2.356, 0.0, 1.571},
                        std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::quiet_NaN());
        auto *joint = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint2"));
        QPushButton *send = nullptr;
        for (auto *button : arm.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("관절 목표 보내기"))
                send = button;
        QVERIFY(joint && send);
        joint->setCommand(-1.2);
        QVERIFY(joint->isEnabled());
        QVERIFY(!send->isEnabled());
        arm.setExecutionAvailable(true);
        QVERIFY(send->isEnabled());
    }

    void armSavedPoseAppliesOnlyWhenRowButtonIsPressed()
    {
        ui::ArmPanel arm;
        arm.resize(430, 700);
        arm.show();
        auto *sections = arm.findChild<QTabWidget *>(QStringLiteral("ArmSections"));
        auto *saved = arm.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        auto *joint = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint2"));
        auto *view = arm.findChild<ui::Robot3DView *>();
        QVERIFY(sections && saved && joint && view);
        QCOMPARE(sections->tabText(0), QStringLiteral("운용"));
        QCOMPARE(sections->tabText(1), QStringLiteral("자세 관리"));
        arm.setControlsEnabled(true);
        arm.setExecutionAvailable(true);
        sections->setCurrentIndex(1);
        QVERIFY(view->isVisible());
        arm.setArmState({0.0, -0.785, 0.0, -2.356, 0.0, 1.571}, 0.09, 0.06);
        QSignalSpy jointGoals(&arm, &ui::ArmPanel::jointGoal);
        QSignalSpy eeGoals(&arm, &ui::ArmPanel::eeGoal);
        const QVariantMap pose{{"id", QStringLiteral("inspect-left")},
                               {"name", QStringLiteral("왼쪽 점검")},
                               {"description", QStringLiteral("측면 촬영")},
                               {"positions", QVariantList{0.0, -1.2, 0.0, -2.0, 0.0, 1.5}},
                               {"revision", 1}};
        arm.setPosePresets({pose});
        QCOMPARE(saved->count(), 1);
        saved->setCurrentRow(0);
        QVERIFY(qAbs(joint->command() + 1.2) < 0.02); // 선택은 미리보기만 한다.
        QCOMPARE(jointGoals.size(), 0);
        auto *row = static_cast<ui::CatalogRow *>(saved->itemWidget(saved->item(0)));
        QVERIFY(row && row->applyButton()->isEnabled());
        row->applyButton()->click();
        QCOMPARE(jointGoals.size(), 1);
        QVERIFY(qAbs(joint->command() + 1.2) < 0.02);
        auto *preview = arm.findChild<QLabel *>(QStringLiteral("ArmPreviewStatus"));
        QCOMPARE(preview->text(), QStringLiteral("목표 미리보기"));
        QCOMPARE(eeGoals.size(), 0);

        // 로봇이 수정된 목록을 다시 보내도 운영자가 편집 중인 목표는 보존한다.
        joint->setCommand(-1.4);
        arm.setPosePresets({pose});
        QVERIFY(qAbs(joint->command() + 1.4) < 0.02);
        QVariantMap archivedPose = pose;
        archivedPose[QStringLiteral("archived")] = true;
        arm.setPosePresets({archivedPose});
        QCOMPARE(saved->count(), 0);
        QVERIFY(qAbs(joint->command() + 1.4) < 0.02);
        sections->setCurrentIndex(0);
        QVERIFY(view->isVisible());
    }

    void armHasNoBuiltInPoseChoices()
    {
        ui::ArmPanel arm;
        arm.resize(430, 700);
        arm.show();
        auto *sections = arm.findChild<QTabWidget *>(QStringLiteral("ArmSections"));
        auto *saved = arm.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        auto *save = arm.findChild<QPushButton *>(QStringLiteral("ArmSaveCurrentPose"));
        auto *previewSave = arm.findChild<QPushButton *>(QStringLiteral("ArmSavePreviewPose"));
        QVERIFY(sections && saved && save && previewSave);
        QCOMPARE(save->text(), QStringLiteral("현재 자세 저장"));
        QCOMPARE(previewSave->text(), QStringLiteral("미리보기 저장"));
        for (const QString &key : {QStringLiteral("home"), QStringLiteral("standby"),
                                   QStringLiteral("stow")})
            QVERIFY(!arm.findChild<QPushButton *>(QStringLiteral("ArmPreset_%1").arg(key)));
        QVERIFY(saved->count() == 0);
        sections->setCurrentIndex(1);
        QVERIFY(!sections->widget(1)->findChild<QPushButton *>(
            QStringLiteral("ArmSaveCurrentPose")));
    }

    void armCurrentPoseSavesImmediatelyAndEditsInItsRow()
    {
        ui::ArmPanel arm;
        arm.resize(430, 800);
        arm.show();
        const QList<double> actual{0.0, -0.785, 0.0, -2.356, 0.0, 1.571};
        arm.setControlsEnabled(true);
        arm.setExecutionAvailable(true);
        arm.setArmState(actual, 0.09, 0.06);
        auto *joint = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint2"));
        auto *button = arm.findChild<QPushButton *>(QStringLiteral("ArmSaveCurrentPose"));
        auto *list = arm.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        QVERIFY(joint && button && list);
        joint->setCommand(-1.2); // 미전송 초안은 '현재 자세 저장'의 대상이 아니다.
        QSignalSpy saves(&arm, &ui::ArmPanel::savePosePresetRequested);
        button->click();
        QCOMPARE(saves.size(), 1);
        QCOMPARE(list->count(), 1);
        const QVariantMap pending = saves.first().first().toMap();
        QVERIFY(qAbs(pending.value(QStringLiteral("positions")).toList().at(1).toDouble()
                     - actual.at(1)) < 1e-9);
        auto *row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row);
        QVERIFY(!row->editButton()->isEnabled());
        QVERIFY(!row->applyButton()->isEnabled());

        QVariantMap confirmed = pending;
        confirmed[QStringLiteral("revision")] = 1;
        arm.setPosePresets({confirmed});
        QVERIFY(!button->isEnabled());
        arm.setCommandResult(QStringLiteral("cmd/arm/pose_presets/save"), true, {}, {});
        QVERIFY(button->isEnabled());
        row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row->editButton()->isEnabled());
        const int collapsedHeight = list->item(0)->sizeHint().height();
        row->editButton()->click();
        QVERIFY(row->isEditing());
        QVERIFY(list->item(0)->sizeHint().height() > collapsedHeight);
        auto *name = row->findChild<QLineEdit *>(QStringLiteral("PoseRowName"));
        auto *j2 = row->findChild<QDoubleSpinBox *>(QStringLiteral("PoseRowJoint2"));
        auto *save = row->findChild<QPushButton *>(QStringLiteral("PoseRowSave"));
        QVERIFY(name && j2 && save);
        name->setText(QStringLiteral("검사 A"));
        j2->setValue(-60.0);
        QSignalSpy updates(&arm, &ui::ArmPanel::updatePosePresetRequested);
        save->click();
        QCOMPARE(updates.size(), 1);
        const QVariantMap updated = updates.first().first().toMap();
        QCOMPARE(updated.value(QStringLiteral("name")).toString(), QStringLiteral("검사 A"));
        QVERIFY(qAbs(updated.value(QStringLiteral("positions")).toList().at(1).toDouble()
                     + M_PI / 3) < 1e-5);

        QVariantMap fromRobot = updated;
        fromRobot[QStringLiteral("revision")] = 2;
        arm.setPosePresets({fromRobot});
        arm.setCommandResult(QStringLiteral("cmd/arm/pose_presets/update"), true, {}, {});
        row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QSignalSpy goals(&arm, &ui::ArmPanel::jointGoal);
        row->applyButton()->click();
        QCOMPARE(goals.size(), 1);
        QVERIFY(qAbs(goals.first().first().value<QList<double>>().at(1) + M_PI / 3) < 1e-5);
    }

    void armWithoutTelemetryCanSaveAnEditedPose()
    {
        ui::ArmPanel arm;
        arm.setControlsEnabled(true);
        auto *button = arm.findChild<QPushButton *>(QStringLiteral("ArmSavePreviewPose"));
        auto *measured = arm.findChild<QPushButton *>(QStringLiteral("ArmSaveCurrentPose"));
        auto *joint = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint2"));
        auto *eeX = arm.findChild<ui::ValueSlider *>(QStringLiteral("Ee_x"));
        auto *view = arm.findChild<ui::Robot3DView *>();
        QVERIFY(button && measured && joint && eeX && view);
        QCOMPARE(view->height(), 210);
        QCOMPARE(button->text(), QStringLiteral("미리보기 저장"));
        QVERIFY(button->isEnabled());
        QVERIFY(!measured->isEnabled());
        for (int i = 0; i < robot::kArmJointCount; ++i) {
            auto *slider = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint%1").arg(i + 1));
            QVERIFY(slider);
            QVERIFY(qAbs(slider->command() - robot::kArmHome.at(i)) < 0.01);
        }
        QList<double> initial;
        for (int i = 0; i < robot::kArmJointCount; ++i)
            initial << arm.findChild<ui::ValueSlider *>(
                QStringLiteral("Joint%1").arg(i + 1))->command();
        QVERIFY(qAbs(eeX->command() - robot::forwardKinematics(initial).x) < 0.01);

        joint->setCommand(-1.2);
        QVERIFY(button->isEnabled());
        QSignalSpy saves(&arm, &ui::ArmPanel::savePosePresetRequested);
        button->click();
        QCOMPARE(saves.size(), 1);
        const auto positions = saves.first().first().toMap()
                                   .value(QStringLiteral("positions")).toList();
        QCOMPARE(positions.size(), robot::kArmJointCount);
        QVERIFY(qAbs(positions.at(1).toDouble() - joint->command()) < 1e-9);

        QVariantMap confirmed = saves.first().first().toMap();
        confirmed[QStringLiteral("revision")] = 1;
        arm.setPosePresets({confirmed});
        auto *list = arm.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        QVERIFY(list && list->count() == 1);
        auto *row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row && !row->applyButton()->isEnabled()); // 팔 실행기 없는 시뮬레이션
        joint->setCommand(-1.4);
        QSignalSpy moves(&arm, &ui::ArmPanel::jointGoal);
        list->setCurrentRow(-1);
        list->setCurrentRow(0);
        QVERIFY(qAbs(joint->command() - positions.at(1).toDouble()) < 0.01);
        QCOMPARE(moves.size(), 0);

        arm.clearReportedState();
        QVERIFY(button->isEnabled());
        QVERIFY(!measured->isEnabled());
        QVERIFY(qAbs(joint->command() - robot::kArmHome.at(1)) < 0.01);
    }

    void armPreviewSaveDoesNotUseMeasuredJoints()
    {
        ui::ArmPanel arm;
        arm.setControlsEnabled(true);
        const QList<double> measured{0.0, -0.785, 0.0, -2.356, 0.0, 1.571};
        arm.setArmState(measured, 0.09, 0.06);
        auto *joint = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint2"));
        auto *save = arm.findChild<QPushButton *>(QStringLiteral("ArmSavePreviewPose"));
        QVERIFY(joint && save && save->isEnabled());
        joint->setCommand(-1.2);
        QSignalSpy requests(&arm, &ui::ArmPanel::savePosePresetRequested);
        save->click();
        QCOMPARE(requests.size(), 1);
        const auto values = requests.first().first().toMap()
                                .value(QStringLiteral("positions")).toList();
        QVERIFY(qAbs(values.at(1).toDouble() - joint->command()) < 1e-9);
        QVERIFY(qAbs(values.at(1).toDouble() - measured.at(1)) > 0.3);
    }

    void armCommandEditorsUseCompactTabs()
    {
        ui::ArmPanel arm;
        auto *sections = arm.findChild<QTabWidget *>(QStringLiteral("ArmSections"));
        auto *tabs = arm.findChild<QTabWidget *>(QStringLiteral("ArmCommandTabs"));
        auto *joint = arm.findChild<QWidget *>(QStringLiteral("ArmJointControls"));
        auto *ee = arm.findChild<QWidget *>(QStringLiteral("ArmEeControls"));
        auto *lastJoint = arm.findChild<ui::ValueSlider *>(
            QStringLiteral("Joint%1").arg(robot::kArmJointCount));
        auto *lastEe = arm.findChild<ui::ValueSlider *>(QStringLiteral("Ee_yaw"));
        QVERIFY(sections && tabs && joint && ee && lastJoint && lastEe);
        QVERIFY(sections->widget(0)->isAncestorOf(tabs));
        QCOMPARE(tabs->count(), 2);
        QCOMPARE(tabs->tabText(0), QStringLiteral("관절"));
        QCOMPARE(tabs->tabText(1), QStringLiteral("끝단 위치"));
        arm.resize(430, 800);
        arm.show();
        QCoreApplication::processEvents();
        QVERIFY(joint->isVisible());
        QVERIFY(!ee->isVisible());
        QPushButton *jointSend = nullptr;
        QPushButton *eeSend = nullptr;
        for (auto *button : arm.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("관절 목표 보내기"))
                jointSend = button;
            if (button->text() == QStringLiteral("끝단 목표 보내기"))
                eeSend = button;
        }
        QVERIFY(jointSend && eeSend);
        QVERIFY(jointSend->y() - lastJoint->geometry().bottom() <= 12);
        const int jointBottom = jointSend->mapTo(tabs, QPoint(0, jointSend->height())).y();
        QVERIFY2(tabs->height() - jointBottom < 48,
                 qPrintable(QStringLiteral("관절 조작 아래 빈 공간: %1px (탭 %2px)")
                                .arg(tabs->height() - jointBottom).arg(tabs->height())));
        tabs->setCurrentIndex(1);
        QCoreApplication::processEvents();
        QVERIFY(!joint->isVisible());
        QVERIFY(ee->isVisible());
        QVERIFY(eeSend->y() - lastEe->geometry().bottom() <= 12);
        const int eeBottom = eeSend->mapTo(tabs, QPoint(0, eeSend->height())).y();
        QVERIFY2(tabs->height() - eeBottom < 48,
                 qPrintable(QStringLiteral("끝단 조작 아래 빈 공간: %1px (탭 %2px)")
                                .arg(tabs->height() - eeBottom).arg(tabs->height())));
    }

    void armPoseManagementDoesNotResizeTheSections()
    {
        ui::ArmPanel arm;
        arm.resize(430, 800);
        arm.show();
        auto *sections = arm.findChild<QTabWidget *>(QStringLiteral("ArmSections"));
        auto *view = arm.findChild<ui::Robot3DView *>();
        QVERIFY(sections && view);
        QCoreApplication::processEvents();
        const int sectionHeight = sections->height();
        const int previewHeight = view->height();
        sections->setCurrentIndex(1);
        QCoreApplication::processEvents();
        QCOMPARE(sections->height(), sectionHeight);
        QCOMPARE(view->height(), previewHeight);

        QList<QVariantMap> poses;
        for (int i = 0; i < 8; ++i)
            poses << QVariantMap{{"id", QStringLiteral("pose-%1").arg(i)},
                                 {"name", QStringLiteral("자세 %1").arg(i + 1)},
                                 {"positions", QVariantList{0.0, -0.785, 0.0,
                                                             -2.356, 0.0, 1.571}},
                                 {"revision", 1}};
        arm.setPosePresets(poses);
        auto *list = arm.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        QVERIFY(list && list->count() == 8);
        QCoreApplication::processEvents();
        QCOMPARE(sections->height(), sectionHeight);
        QCOMPARE(view->height(), previewHeight);
        arm.setControlsEnabled(true);
        auto *row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row && row->editButton()->isEnabled());
        row->editButton()->click();
        QCoreApplication::processEvents();
        QVERIFY(row->isEditing());
        QCOMPARE(sections->height(), sectionHeight);
        QCOMPARE(view->height(), previewHeight);
    }

    void initTestCase()
    {
        // 창을 띄우면 이벤트 로그가 실제로 파일을 쓴다. 격리하지 않으면
        // 검사를 한 번 돌릴 때마다 개발자 홈에 로그 폴더가 하나씩 남는다.
        QStandardPaths::setTestModeEnabled(true);
    }

    void everyView_paints_light() { paintEveryView(QStringLiteral("light")); }
    void everyView_paints_dark() { paintEveryView(QStringLiteral("dark")); }

    void missionControlsAndDriveStateAreInTheirOwnTabs()
    {
        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        auto *stack = window.findChild<QStackedWidget *>();
        QVERIFY(stack);
        QVERIFY(stack->widget(0)->findChild<ui::StatusPanel *>());
        QVERIFY(!stack->widget(0)->findChild<ui::MissionPanel *>());
        QVERIFY(stack->widget(1)->findChild<ui::MissionPanel *>());
        QVERIFY(!stack->widget(1)->findChild<ui::StatusPanel *>());
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("MissionSummary")));
    }

    void connectedRobotIdIsPrimaryAndAliasStaysLocal()
    {
        auto &cfg = Config::instance();
        const auto previous = cfg.robots();
        const int previousCurrent = cfg.currentRobot();
        cfg.setRobots({{QStringLiteral("A검수선"), QStringLiteral("192.0.2.41"), 9090}});
        cfg.setCurrentRobot(0);

        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        auto *primary = window.findChild<QLabel *>(QStringLiteral("RobotPickerName"));
        auto *secondary = window.findChild<QLabel *>(QStringLiteral("RobotPickerAddr"));
        QVERIFY(primary && secondary);
        emit robot->robotIdentity(QStringLiteral("SE-0001"), QStringLiteral("내부 이름"));
        QCOMPARE(primary->text(), QStringLiteral("SE-0001"));
        QVERIFY(secondary->text().contains(QStringLiteral("A검수선")));
        QVERIFY(secondary->text().contains(QStringLiteral("192.0.2.41")));
        QCOMPARE(cfg.robots().first().name, QStringLiteral("A검수선"));

        cfg.setRobots(previous);
        cfg.setCurrentRobot(previousCurrent);
    }

    void robotAliasIsOptionalInConnectionSettings()
    {
        ui::SettingsDialog dialog;
        auto *alias = dialog.findChild<QLineEdit *>(QStringLiteral("RobotAliasInput"));
        auto *address = dialog.findChild<QLineEdit *>(QStringLiteral("RobotAddressInput"));
        auto *add = dialog.findChild<QPushButton *>(QStringLiteral("AddRobotButton"));
        auto *list = dialog.findChild<QTreeWidget *>(QStringLiteral("PickList"));
        QVERIFY(alias && address && add && list);
        QVERIFY(alias->text().isEmpty());
        address->setText(QStringLiteral("192.0.2.42"));
        QVERIFY(add->isEnabled());
        add->click();
        bool found = false;
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            const auto *item = list->topLevelItem(i);
            if (item->text(0) == QStringLiteral("192.0.2.42:9090")) {
                found = true;
                QCOMPARE(item->text(1), QStringLiteral("—"));
            }
        }
        QVERIFY(found);
    }

    void mapSelectorShowsCurrentNameWithAdjacentRefresh()
    {
        ui::MapCard card;
        card.resize(700, 420);
        card.setMapLabel(QStringLiteral("차량기지 A구역"), QStringLiteral("20×10 m"));
        card.show();
        auto *select = card.findChild<QPushButton *>(QStringLiteral("CurrentMapButton"));
        auto *refresh = card.findChild<ui::IconButton *>(
            QStringLiteral("RefreshMapListButton"));
        QVERIFY(select);
        QVERIFY(refresh);
        QVERIFY(select->text().contains(QStringLiteral("차량기지 A구역")));
        QVERIFY(refresh->mapTo(&card, QPoint(0, 0)).x() >
                select->mapTo(&card, QPoint(0, 0)).x());
        card.setMapListEnabled(true);
        QVERIFY(select->isEnabled());
        QVERIFY(refresh->isEnabled());
        card.setMapLabel(QStringLiteral("live"), {});
        QVERIFY(select->text().contains(QStringLiteral("실시간 지도")));
    }

    /// 첫 arm telemetry 전에도 조작자가 관절 또는 끝단 목표를 바꾸면
    /// 즉시 3D 미리보기가 따라야 한다. 예전에는 diverged()가 기준값 없음을
    /// false로 취급해 preview를 비워 버려, 값만 바뀌고 팔은 멈춰 보였다.
    void armEdit_updates3dPreviewBeforeFirstTelemetry()
    {
        theme::setTheme(QStringLiteral("light"));
        qApp->setStyleSheet(theme::buildQss());

        ui::ArmPanel arm;
        arm.resize(900, 900);
        arm.show();
        QTest::qWait(30);

        auto *view = arm.findChild<ui::Robot3DView *>();
        auto *j2 = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint2"));
        QVERIFY2(view && j2, "3D 뷰 또는 J2 슬라이더를 찾지 못했다");
        auto *preview = arm.findChild<QLabel *>(QStringLiteral("ArmPreviewStatus"));
        QVERIFY(preview && preview->text().isEmpty());

        const QImage before = view->grab().toImage();
        j2->setCommand(-1.2);
        QCOMPARE(preview->text(), QStringLiteral("목표 미리보기"));
        QTest::qWait(300);  // 미리보기 보간 타이머(20 ms)가 목표에 닿을 시간
        const QImage after = view->grab().toImage();

        QVERIFY2(changedPixels(before, after) > 100,
                 "텔레메트리 전 관절 편집이 3D 미리보기에 반영되지 않았다");
    }

    void armDraftSurvivesFirstTelemetryAndViewStaysVisible()
    {
        ui::ArmPanel arm;
        arm.resize(430, 700);
        arm.show();
        auto *view = arm.findChild<ui::Robot3DView *>();
        auto *joint = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint2"));
        auto *scroll = arm.findChild<QScrollArea *>();
        QVERIFY(view && joint && scroll);
        joint->setCommand(-1.2);
        const double draft = joint->command();
        arm.setArmState({robot::kArmHome.begin(), robot::kArmHome.end()}, 0.09, 0.06);
        QVERIFY(qAbs(joint->command() - draft) < 1e-6);
        QVERIFY(view->isVisible());
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QVERIFY(view->isVisible());
        auto *status = arm.findChild<QLabel *>(QStringLiteral("ArmPreviewStatus"));
        QVERIFY(status);
        QVERIFY(status->text().contains(QStringLiteral("미리보기")));
    }

    void armCommandRejectionIsVisible()
    {
        ui::ArmPanel arm;
        arm.setCommandResult(QStringLiteral("cmd/arm/joint_goal"), false,
                             QStringLiteral("E_UNREACHABLE"),
                             QStringLiteral("로봇팔 실행기가 연결되지 않았습니다"));
        auto *status = arm.findChild<QLabel *>(QStringLiteral("ArmCommandStatus"));
        QVERIFY(status);
        QVERIFY(!status->isHidden());
        QVERIFY(status->text().contains(QStringLiteral("실행기가 연결되지 않았습니다")));
    }

    void armSaveResultReachesPanelWithoutPriorSave()
    {
        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        auto *arm = window.findChild<ui::ArmPanel *>();
        QVERIFY(arm);
        auto *status = arm->findChild<QLabel *>(QStringLiteral("ArmCommandStatus"));
        QVERIFY(status);

        emit robot->commandResult(QStringLiteral("cmd/arm/pose_presets/save"), false,
                                  QStringLiteral("E_BAD_PAYLOAD"),
                                  QStringLiteral("같은 이름의 프리셋이 이미 있습니다"));
        QVERIFY(!status->isHidden());
        QVERIFY(status->text().contains(QStringLiteral("같은 이름")));
        emit robot->commandResult(QStringLiteral("cmd/arm/pose_presets/save"), true, {}, {});
        QCOMPARE(status->text(), QStringLiteral("자세 저장됨"));
    }

    void waypointsFollowRobotPublishedCatalog()
    {
        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        auto *panel = window.findChild<ui::WaypointPanel *>();
        QVERIFY(panel);
        const QVariantMap point{{"id", QStringLiteral("wp-1")},
                                {"name", QStringLiteral("입구")},
                                {"x", 1.0}, {"y", 2.0}};
        robot->setWaypoints({point});
        QCOMPARE(panel->waypoints().size(), 1);
        QCOMPARE(panel->waypoints().first().value(QStringLiteral("id")).toString(),
                 QStringLiteral("wp-1"));
        robot->setWaypoints({});
        QCOMPARE(panel->waypoints().size(), 0);

        emit robot->commandResult(QStringLiteral("cmd/waypoints/set"), false,
                                  QStringLiteral("E_MODE"),
                                  QStringLiteral("저장된 지도를 선택하십시오"));
        auto *status = panel->findChild<QLabel *>(QStringLiteral("WaypointSaveStatus"));
        QVERIFY(status);
        QVERIFY(!status->isHidden());
        QVERIFY(status->text().contains(QStringLiteral("저장된 지도를 선택하십시오")));
    }

    void waypointCreationHasOneHome()
    {
        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        auto *waypoints = window.findChild<ui::WaypointPanel *>();
        auto *locations = window.findChild<ui::LocationPanel *>();
        QVERIFY(waypoints && locations);
        QVERIFY(waypoints->findChild<QPushButton *>(QStringLiteral("WaypointFromRobotButton")));
        QVERIFY(waypoints->findChild<QPushButton *>(QStringLiteral("WaypointFromMapButton")));
        for (auto *button : locations->findChildren<QPushButton *>()) {
            QVERIFY(button->text() != QStringLiteral("로봇 위치로 추가"));
            QVERIFY(button->text() != QStringLiteral("지도에서 추가"));
        }
    }

    void goalButtonFollowsReportedModeAndMap()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), server.serverPort());
        ui::MainWindow window(link);
        auto *map = window.findChild<ui::MapCard *>();
        QVERIFY(map);
        QVERIFY(!window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());

        link->connectToBridge();
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        QTRY_VERIFY(link->isConnected());

        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        emit link->activeMapReceived({{QStringLiteral("id"), QStringLiteral("map-1")},
                                      {QStringLiteral("name"), QStringLiteral("map-1")}});
        emit link->mapReceived(png, {{QStringLiteral("width"), 8},
                                     {QStringLiteral("height"), 8},
                                     {QStringLiteral("resolution"), 0.1},
                                     {QStringLiteral("map_id"), QStringLiteral("map-1")}});
        QVERIFY(!window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());  // 지도가 있어도 모드 미확인

        const auto reportMode = [peer](const char *mode) {
            const auto state = net::makePublish(QLatin1String(hmi::ch::kSafety),
                                                {{QStringLiteral("mode"), QLatin1String(mode)}});
            peer->write(net::encodeFrame(state.toHeader(), state.payload));
            peer->flush();
        };
        reportMode("auto");
        QTRY_VERIFY(window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());
        window.showView(ui::NavItem::Arm);
        QVERIFY(window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled()); // 숨은 운용 패널도 보고 상태를 유지한다.
        window.showView(ui::NavItem::Drive);
        auto *driveTabs = window.findChild<QTabWidget *>(QStringLiteral("DriveTabs"));
        QVERIFY(driveTabs);
        driveTabs->setCurrentIndex(1);
        QVERIFY(window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());
        driveTabs->setCurrentIndex(0);

        window.setDriveMode(QStringLiteral("manual"));
        QVERIFY(!window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());
        reportMode("manual");
        QTRY_VERIFY(link->mode() == robot::DriveMode::Manual);
        QVERIFY(!window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());

        window.setDriveMode(QStringLiteral("auto"));
        QVERIFY(!window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());
        reportMode("auto");
        QTRY_VERIFY(window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());
    }

    void goalButtonAcceptsLiveSlamMapWithoutSavedMapId()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        auto *link = new net::BridgeClient(QStringLiteral("127.0.0.1"), server.serverPort());
        ui::MainWindow window(link);
        auto *map = window.findChild<ui::MapCard *>();
        QVERIFY(map);
        link->connectToBridge();
        QTRY_VERIFY(server.hasPendingConnections());
        auto *peer = server.nextPendingConnection();
        QTRY_VERIFY(link->isConnected());

        QImage image(8, 8, QImage::Format_RGB32);
        image.fill(Qt::white);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        emit link->activeMapReceived({});
        emit link->mapReceived(png, {{QStringLiteral("width"), 8},
                                     {QStringLiteral("height"), 8},
                                     {QStringLiteral("resolution"), 0.1},
                                     {QStringLiteral("map_id"), QString()}});
        QVERIFY(!window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());
        const auto state = net::makePublish(QLatin1String(hmi::ch::kSafety),
                                            {{QStringLiteral("mode"), QStringLiteral("auto")}});
        peer->write(net::encodeFrame(state.toHeader(), state.payload));
        peer->flush();
        QTRY_VERIFY(window.findChild<ui::StatusPanel *>()->goalButton()->isEnabled());
    }

    void driveModeButtonsRunFromManualToAuto()
    {
        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        window.resize(1400, 900);
        window.show();
        QCoreApplication::processEvents();
        QPushButton *manual = nullptr;
        QPushButton *autoMode = nullptr;
        for (auto *button : window.findChildren<QPushButton *>()) {
            if (button->text() == QStringLiteral("수동")) manual = button;
            if (button->text() == QStringLiteral("자율")) autoMode = button;
        }
        QVERIFY(manual && autoMode);
        QVERIFY(manual->mapToGlobal(QPoint()).x() < autoMode->mapToGlobal(QPoint()).x());
    }

    void waypointRowsEditAndApplyInPlace()
    {
        ui::WaypointPanel panel;
        panel.resize(420, 360);
        panel.show();
        QVariantMap point{{"id", QStringLiteral("wp-1")},
                          {"name", QStringLiteral("입구")},
                          {"x", 1.0}, {"y", 2.0}, {"theta", 0.0}};
        panel.setWaypoints({point});
        panel.setEditingEnabled(true);
        auto *list = panel.findChild<QListWidget *>();
        QVERIFY(list);
        auto *row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row);
        QVERIFY(row->findChild<QLabel *>(QStringLiteral("CatalogRowDetails"))
                    ->text().contains(QStringLiteral("yaw 0.0°")));
        point[QStringLiteral("theta")] = qDegreesToRadians(90.0);
        panel.setWaypoints({point});
        row = static_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row->findChild<QLabel *>(QStringLiteral("CatalogRowDetails"))
                    ->text().contains(QStringLiteral("yaw 90.0°")));
        row->editButton()->click();
        auto *name = row->findChild<QLineEdit *>(QStringLiteral("WaypointRowName"));
        auto *yaw = row->findChild<QDoubleSpinBox *>(QStringLiteral("WaypointRowYaw"));
        auto *save = row->findChild<QPushButton *>(QStringLiteral("WaypointRowSave"));
        QVERIFY(name && yaw && save);
        name->setText(QStringLiteral("입구 A"));
        yaw->setValue(45.0);
        QSignalSpy updates(&panel, &ui::WaypointPanel::updateRequested);
        save->click();
        QCOMPARE(updates.size(), 1);
        QCOMPARE(updates.first().at(0).toString(), QStringLiteral("wp-1"));
        QCOMPARE(updates.first().at(1).toMap().value(QStringLiteral("name")).toString(),
                 QStringLiteral("입구 A"));
        QVERIFY(qAbs(updates.first().at(1).toMap().value(QStringLiteral("theta")).toDouble()
                     - M_PI / 4) < 1e-6);
        QSignalSpy goals(&panel, &ui::WaypointPanel::gotoRequested);
        auto *cancel = row->findChild<QPushButton *>(QStringLiteral("WaypointRowCancel"));
        QVERIFY(cancel);
        cancel->click();
        row->applyButton()->click();
        QCOMPARE(goals.size(), 1);
        QCOMPARE(goals.first().first().toString(), QStringLiteral("wp-1"));
    }

    /// 대화상자는 창 계층 밖이라 위 순회에 걸리지 않는다.
    void dialogs_paint()
    {
        theme::setTheme(QStringLiteral("light"));
        qApp->setStyleSheet(theme::buildQss());

        ui::WelcomeDialog welcome;
        QVERIFY(!welcome.grab().isNull());
        auto *id = welcome.findChild<QLineEdit *>(QStringLiteral("LoginId"));
        QVERIFY(id);
        QVERIFY(id->placeholderText().isEmpty());
        QString welcomeText;
        for (const auto *label : welcome.findChildren<QLabel *>())
            welcomeText += label->text();
        QVERIFY(welcomeText.contains(QStringLiteral("하부점검 관제")));
        QVERIFY(!welcomeText.contains(QStringLiteral("로봇 연결 상태")));
        QVERIFY(!welcomeText.contains(QStringLiteral("통신 규격")));
        QVERIFY(!welcomeText.contains(QStringLiteral("하드웨어 정지 버튼")));

        ui::SettingsDialog settings;
        // 탭 수를 손으로 적어 두었더니 여섯 번째(안전)가 추가된 뒤로도
        // 다섯 개만 그려 보고 있었다.
        for (int tab = 0; tab < settings.tabCount(); ++tab) {
            settings.setCurrentTab(tab);
            QVERIFY2(!settings.grab().isNull(),
                     qPrintable(QStringLiteral("설정 %1 번째 탭을 그리지 못했다").arg(tab)));
        }
    }

    /// 자세 경고는 아이콘 하나뿐이라, 설명이 도구 설명에 들어가지 않으면
    /// 화면에 "!" 만 뜨고 무엇이 문제인지 알 방법이 없어진다.
    void poseWarning_carriesItsExplanation()
    {
        ui::ArmPanel arm;

        // 홈 자세를 알린 뒤 J5 를 0 으로 끌어 손목 축을 일직선으로 만든다.
        //
        // 예전에는 "stow" 프리셋을 눌러 경고를 띄웠다. 그건 프리셋이 관절
        // 한계에 붙어 있어서 우연히 경고가 뜬 것이었고, 검사가 아니라 설정의
        // 결함이었다 — 조작자가 팔을 접을 때마다 "보낼 수 없습니다" 를 보게
        // 된다. 프리셋은 이제 경고 없이 도달하도록 고른다
        // (test_robotview::presets_raiseNoWarning). 그래서 여기서는 경고가
        // 나야 할 자세를 직접 만든다.
        arm.setArmState({robot::kArmHome.begin(), robot::kArmHome.end()}, 0.09, 0.06);
        auto *j5 = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint5"));
        QVERIFY2(j5, "J5 슬라이더를 찾지 못했다");
        j5->setCommand(0.0);

        auto *badge = arm.findChild<QLabel *>(QStringLiteral("PoseWarning"));
        QVERIFY2(badge, "자세 경고 배지를 찾지 못했다");

        // 조건부로 검사하면 배지가 안 뜨는 채로도 통과한다. 이 자세에서는
        // 반드시 떠야 하므로 그것부터 못 박는다.
        QVERIFY2(!badge->isHidden(), "손목 특이자세인데 경고가 뜨지 않았다");
        QVERIFY2(!badge->toolTip().isEmpty(),
                 "경고 아이콘이 떴는데 설명이 비어 있다");
    }

    /// 관절 탭과 끝단 탭은 같은 하나의 자세를 다르게 적은 것이다. 둘이
    /// 어긋나면 조작자는 자기가 보낼 자세를 화면에서 읽을 수 없다.
    ///
    /// 예전에는 끝단 탭의 "지금" 이 마지막으로 보낸 값이었다. 팔이 실제로
    /// 어디 있는지와 아무 상관이 없는 숫자였고, 아무것도 보내지 않은
    /// 상태에서는 코드에 박아 둔 상수였다.
    void jointAndEeTabs_describeTheSamePose()
    {
        ui::ArmPanel arm;

        // 이름으로 찾는다. findChildren 의 순서는 계약이 아니다.
        QList<ui::ValueSlider *> joints;
        for (int i = 1; i <= robot::kArmJointCount; ++i) {
            auto *s = arm.findChild<ui::ValueSlider *>(QStringLiteral("Joint%1").arg(i));
            QVERIFY2(s, qPrintable(QStringLiteral("관절 %1 슬라이더가 없다").arg(i)));
            joints << s;
        }
        QList<ui::ValueSlider *> ee;
        for (const char *axis : {"x", "y", "z", "roll", "pitch", "yaw"}) {
            auto *s = arm.findChild<ui::ValueSlider *>(
                QStringLiteral("Ee_%1").arg(QLatin1String(axis)));
            QVERIFY2(s, qPrintable(QStringLiteral("끝단 %1 슬라이더가 없다")
                                       .arg(QLatin1String(axis))));
            ee << s;
        }
        const auto sliders = joints + ee;

        const QList<double> home{0.0, -0.785, 0.0, -2.356, 0.0, 1.571};
        arm.setArmState(home, 0.09, 0.06);

        // 끝단의 "지금" 은 실제 관절에서 나와야 한다.
        //
        // 뒤 세 축은 각도라 한 바퀴 차이는 같은 값이다. 실제로 이 자세의
        // Roll 은 정확히 ±π 자리에 있어서, 어느 부호로 나오는지는 부동소수
        // 반올림에 달렸다 — macOS 와 Ubuntu 가 서로 다르게 냈다.
        const auto pose = robot::forwardKinematics(home);
        const double want[] = {pose.x, pose.y, pose.z, pose.roll, pose.pitch, pose.yaw};
        for (int i = 0; i < 6; ++i) {
            double d = qAbs(ee.at(i)->actual() - want[i]);
            if (i >= 3)
                d = qMin(d, 2 * M_PI - d);
            QVERIFY2(d < 1e-6,
                     qPrintable(QStringLiteral("끝단 %1 번 축의 기준값이 정기구학과 다르다: "
                                               "%2 vs %3")
                                    .arg(i)
                                    .arg(ee.at(i)->actual())
                                    .arg(want[i])));
        }

        // 아직 아무것도 지시하지 않았다. 어느 탭에도 "보내지 않은 편집" 이
        // 떠 있으면 안 된다.
        for (auto *s : sliders)
            QVERIFY2(!s->diverged(),
                     qPrintable(QStringLiteral("아무것도 만지지 않았는데 %1 이 "
                                               "보내지 않은 편집으로 표시된다 "
                                               "(지금 %2, 보낼 값 %3)")
                                    .arg(s->objectName())
                                    .arg(s->actual())
                                    .arg(s->command())));

        // 관절을 만지면 끝단 명령값이 따라온다.
        joints.at(1)->setCommand(-1.2);
        QList<double> moved;
        for (auto *s : joints)
            moved << s->command();
        const auto after = robot::forwardKinematics(moved);

        // 슬라이더는 눈금 1000 칸이라 명령값이 그만큼 반올림된다. X 는 2 m
        // 범위이므로 한 칸이 2 mm 다. 그 안에 들어오면 따라온 것이다.
        const double stepX = 2.0 / 1000.0;
        QVERIFY2(qAbs(ee.at(0)->command() - after.x) <= stepX,
                 qPrintable(QStringLiteral("관절을 옮겼는데 끝단 X 가 따라오지 "
                                           "않았다: %1 vs %2")
                                .arg(ee.at(0)->command())
                                .arg(after.x)));
        QVERIFY2(qAbs(ee.at(2)->command() - after.z) <= stepX,
                 qPrintable(QStringLiteral("관절을 옮겼는데 끝단 Z 가 따라오지 "
                                           "않았다: %1 vs %2")
                                .arg(ee.at(2)->command())
                                .arg(after.z)));
    }

    /// 테마를 바꾸면 창이 실제로 다시 칠해져야 한다.
    ///
    /// 한 번은 설정이 Config 를 먼저 바꾸고 신호를 냈는데, 그 신호를 받은
    /// 쪽이 "지금 칠해져 있는 테마" 를 다시 적용했다. 버튼은 눌린 것처럼
    /// 보이고 화면은 그대로였다.
    void changingThemeThroughConfig_repaintsTheWindow()
    {
        theme::setTheme(QStringLiteral("light"));
        qApp->setStyleSheet(theme::buildQss());

        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        window.show();

        Config::instance().setTheme(QStringLiteral("dark"));
        QVERIFY2(theme::colors().isDark(),
                 "Config 로 다크를 지정했는데 화면은 라이트 그대로다");

        Config::instance().setTheme(QStringLiteral("light"));
        QVERIFY2(!theme::colors().isDark(),
                 "Config 로 라이트를 지정했는데 화면은 다크 그대로다");
    }

    /// -π 과 π 은 같은 각도다. 순환 값으로 다루지 않으면 같은 자세가 가장
    /// 크게 어긋난 것으로 나오고, 끝단 탭을 여는 순간 Roll 이 보내지 않은
    /// 편집으로 표시된다.
    void cyclicSlider_treatsBothEndsAsOneValue()
    {
        ui::ValueSlider s(QStringLiteral("Roll"), -M_PI, M_PI, QStringLiteral("°"), 0,
                          180.0 / M_PI);
        s.setCommand(M_PI);
        s.setActual(-M_PI);
        QVERIFY2(s.diverged(), "순환으로 표시하기 전에는 어긋난 것으로 보인다");

        s.setCyclic(true);
        QVERIFY2(!s.diverged(), "-π 과 π 은 같은 각도인데 어긋난 것으로 표시된다");

        // 정말로 다른 각도는 순환이어도 잡아야 한다.
        s.setActual(0.0);
        QVERIFY2(s.diverged(), "순환 값이라고 실제로 다른 각도까지 놓치면 안 된다");
    }

    /// 알림 목록은 항목이 있을 때와 없을 때 그리는 경로가 다르다.
    void notificationPopup_paints()
    {
        ui::NotificationPopup empty({});
        QVERIFY(!empty.grab().isNull());

        ui::NotificationPopup filled({
            {QDateTime::currentDateTime(), QStringLiteral("사람 접근 감지"),
             QStringLiteral("인원을 이격시키십시오"), QStringLiteral("warn")},
            {QDateTime::currentDateTime(), QStringLiteral("지도 불러오기 완료"), {},
             QStringLiteral("ok")},
        });
        QVERIFY(!filled.grab().isNull());
    }
};

QTEST_MAIN(TestRender)
#include "test_render.moc"
