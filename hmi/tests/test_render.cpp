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
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStyleOptionViewItem>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QTcpServer>
#include <QTcpSocket>

#include <limits>

#include "Config.h"
#include "RobotDef.h"
#include "MainWindow.h"
#include "net/BridgeClient.h"
#include "net/Channels.h"
#include "panels/ArmPanel.h"
#include "panels/CapturePanel.h"
#include "panels/LocationPanel.h"
#include "panels/MissionPanel.h"
#include "panels/StatusPanel.h"
#include "panels/WaypointPanel.h"
#include "robot/Kinematics.h"
#include "TestRobot.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "views/SettingsDialog.h"
#include "views/WelcomeDialog.h"
#include "widgets/NotificationCenter.h"
#include "widgets/IconButton.h"
#include "widgets/MapCard.h"
#include "widgets/Robot3DView.h"
#include "widgets/ValueSlider.h"
#include "widgets/WaypointDelegate.h"

using namespace hmi;

namespace {

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
        const QList<double> home{0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785};
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

        const QImage before = view->grab().toImage();
        j2->setCommand(-1.2);
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
        QVERIFY(status->text().contains(QStringLiteral("저장했습니다")));
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
        QVERIFY(!map->goalButton()->isEnabled());

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
        QVERIFY(!map->goalButton()->isEnabled());  // 지도가 있어도 모드 미확인

        const auto reportMode = [peer](const char *mode) {
            const auto state = net::makePublish(QLatin1String(hmi::ch::kSafety),
                                                {{QStringLiteral("mode"), QLatin1String(mode)}});
            peer->write(net::encodeFrame(state.toHeader(), state.payload));
            peer->flush();
        };
        reportMode("auto");
        QTRY_VERIFY(map->goalButton()->isEnabled());

        window.setDriveMode(QStringLiteral("manual"));
        QVERIFY(!map->goalButton()->isEnabled());
        reportMode("manual");
        QTRY_VERIFY(link->mode() == robot::DriveMode::Manual);
        QVERIFY(!map->goalButton()->isEnabled());

        window.setDriveMode(QStringLiteral("auto"));
        QVERIFY(!map->goalButton()->isEnabled());
        reportMode("auto");
        QTRY_VERIFY(map->goalButton()->isEnabled());
    }

    void waypointPanelShowsHeadingAndOffersEdit()
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
        auto *edit = panel.findChild<QPushButton *>(QStringLiteral("WaypointEditButton"));
        QVERIFY(list);
        QVERIFY(edit);
        QVERIFY(!edit->isEnabled());

        auto paintRow = [list] {
            QImage image(420, ui::kWaypointRowHeight, QImage::Format_ARGB32);
            image.fill(Qt::white);
            QPainter painter(&image);
            QStyleOptionViewItem option;
            option.rect = image.rect();
            ui::WaypointDelegate delegate;
            delegate.paint(&painter, option, list->model()->index(0, 0));
            return image;
        };
        const QImage east = paintRow();
        point[QStringLiteral("theta")] = qDegreesToRadians(90.0);
        panel.setWaypoints({point});
        const QImage north = paintRow();
        QVERIFY(changedPixels(east, north) > 0);

        list->setCurrentRow(0);
        QVERIFY(edit->isEnabled());
        QSignalSpy edits(&panel, &ui::WaypointPanel::editRequested);
        edit->click();
        QCOMPARE(edits.size(), 1);
        QCOMPARE(edits.first().first().toString(), QStringLiteral("wp-1"));
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
        QVERIFY(welcomeText.contains(QStringLiteral("로봇 연결 상태")));
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

        const QList<double> home{0.0, -0.785, 0.0, -2.356, 0.0, 1.571, 0.785};
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
