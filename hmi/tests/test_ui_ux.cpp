// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimer>
#include <limits>

#include "MainWindow.h"
#include "TestRobot.h"
#include "net/Channels.h"
#include "panels/DataPanel.h"
#include "panels/ArmPanel.h"
#include "panels/MissionPanel.h"
#include "panels/NavigationSpeedPanel.h"
#include "panels/StatusPanel.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/CatalogRow.h"
#include "widgets/Gauges.h"
#include "widgets/IconButton.h"
#include "widgets/ValueSlider.h"
#include "widgets/Robot3DView.h"

using namespace hmi;

class TestUiUx : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        theme::setTheme(QStringLiteral("light"));
        qApp->setStyleSheet(theme::buildQss());
    }

    void speedWaitsForBothReplyAndMatchingReport()
    {
        ui::NavigationSpeedPanel panel;
        panel.setReportedLimits(0.3, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        auto *value = panel.findChild<QDoubleSpinBox *>("NavigationSpeedValue");
        auto *apply = panel.findChild<QPushButton *>("NavigationSpeedApply");
        auto *reported = panel.findChild<QLabel *>("NavigationSpeedReported");
        value->setValue(0.4);
        apply->click();
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedLimit), true, {}, {});
        QVERIFY(!value->isEnabled());
        QVERIFY(reported->text().contains(QStringLiteral("확인 중")));
        panel.setReportedLimits(0.3, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        QVERIFY(!value->isEnabled());
        QCOMPARE(value->value(), 0.4);
        panel.setReportedLimits(0.4, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        QVERIFY(value->isEnabled());
        QVERIFY(!apply->isEnabled());
        QCOMPARE(reported->text(), QStringLiteral("적용됨"));
    }

    void speedReportBeforeReplyDoesNotFinishRequest()
    {
        ui::NavigationSpeedPanel panel;
        panel.setReportedLimits(0.3, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        auto *value = panel.findChild<QDoubleSpinBox *>("NavigationSpeedValue");
        value->setValue(0.4);
        panel.findChild<QPushButton *>("NavigationSpeedApply")->click();
        panel.setReportedLimits(0.4, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        QVERIFY(!value->isEnabled());
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedLimit), true, {}, {});
        QVERIFY(value->isEnabled());
    }

    void rangeRequestAlsoWaitsForReportedValues()
    {
        ui::NavigationSpeedPanel panel(nullptr, true);
        panel.setReportedLimits(0.3, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        auto *maximum = panel.findChild<QDoubleSpinBox *>("NavigationSpeedSettingsMaximum");
        maximum->setValue(0.4);
        panel.findChild<QPushButton *>("NavigationSpeedSettingsApply")->click();
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedSettings), true, {}, {});
        QVERIFY(!maximum->isEnabled());
        panel.setReportedLimits(0.3, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        QVERIFY(!maximum->isEnabled());
        panel.setReportedLimits(0.3, 0.1, 0.4, 0.5, 0.05, 0.8, true);
        QVERIFY(maximum->isEnabled());
        QVERIFY(!panel.findChild<QPushButton *>("NavigationSpeedSettingsApply")->isEnabled());
    }

    void speedConfirmationTimeoutPreservesDraft()
    {
        ui::NavigationSpeedPanel panel;
        panel.setReportedLimits(0.3, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        auto *value = panel.findChild<QDoubleSpinBox *>("NavigationSpeedValue");
        auto *apply = panel.findChild<QPushButton *>("NavigationSpeedApply");
        panel.findChild<QTimer *>("NavigationSpeedConfirmationTimeout")->setInterval(20);
        value->setValue(0.4);
        apply->click();
        panel.handleCommandResult(QLatin1String(ch::kCmdNavigationSpeedLimit), true, {}, {});
        QTRY_VERIFY(value->isEnabled());
        QCOMPARE(value->value(), 0.4);
        QVERIFY(apply->isEnabled());
        QVERIFY(!panel.findChild<QLabel *>("NavigationSpeedError")->isHidden());
    }

    void unsentSpeedDraftCanBeReverted()
    {
        ui::NavigationSpeedPanel panel;
        panel.setReportedLimits(0.3, 0.1, 0.6, 0.5, 0.05, 0.8, true);
        auto *value = panel.findChild<QDoubleSpinBox *>("NavigationSpeedValue");
        auto *angular = panel.findChild<QDoubleSpinBox *>("NavigationAngularSpeedValue");
        auto *revert = panel.findChild<QPushButton *>("NavigationSpeedRevert");
        QSignalSpy requests(&panel, &ui::NavigationSpeedPanel::speedLimitsRequested);
        QVERIFY(revert->isHidden());
        value->setValue(0.4);
        angular->setValue(0.65);
        QVERIFY(!revert->isHidden());
        revert->click();
        QCOMPARE(value->value(), 0.3);
        QCOMPARE(angular->value(), 0.5);
        QCOMPARE(requests.size(), 0);
        QVERIFY(revert->isHidden());
    }

    void catalogNamesAreLiteralAndActionsStayVisible_data()
    {
        QTest::addColumn<QString>("themeName");
        QTest::addColumn<int>("width");
        QTest::newRow("light-320") << QStringLiteral("light") << 320;
        QTest::newRow("dark-380") << QStringLiteral("dark") << 380;
    }

    void catalogNamesAreLiteralAndActionsStayVisible()
    {
        QFETCH(QString, themeName);
        QFETCH(int, width);
        theme::setTheme(themeName);
        qApp->setStyleSheet(theme::buildQss());
        ui::CatalogRow row;
        const auto name = QStringLiteral("<b>차량 왼쪽 점검용으로 길게 입력한 위치 이름</b>");
        row.setName(name);
        row.setDetails(QStringLiteral("X 4.20 m · Y 1.80 m · 90.0°"));
        row.resize(width, row.sizeHint().height());
        row.show();
        QCoreApplication::processEvents();
        auto *label = row.findChild<QLabel *>("CatalogRowName");
        QCOMPARE(label->textFormat(), Qt::PlainText);
        QCOMPARE(label->text(), name);
        QCOMPARE(label->toolTip(), name);
        QCOMPARE(label->alignment(), Qt::AlignLeft | Qt::AlignVCenter);
        QVERIFY(row.rect().contains(row.applyButton()->geometry()));
        QVERIFY(row.rect().contains(row.editButton()->geometry()));
        QVERIFY(label->geometry().right() < row.editButton()->x());
        QVERIFY(!row.grab().isNull());
        row.setDetails({});
        QVERIFY(row.findChild<QLabel *>("CatalogRowDetails")->isHidden());
    }

    void connectedRobotWithoutBatteryShowsWaiting()
    {
        auto *robot = new test::TestRobot;
        ui::MainWindow window(robot);
        window.findChild<ui::DataPanel *>()->setDirectory({});
        const auto *battery = window.findChild<ui::BatteryPill *>();
        QVERIFY(battery);
        robot::Telemetry telemetry;
        emit robot->telemetry(telemetry);
        QVERIFY(battery->toolTip().contains(QStringLiteral("수신 대기")));
        QVERIFY(!battery->toolTip().contains(QStringLiteral("0%")));
        telemetry.soc = 0.0;
        emit robot->telemetry(telemetry);
        QCOMPARE(battery->toolTip(), QStringLiteral("배터리 0%"));
    }

    void numericArmInputCanBeCancelledAndRejectsNonFinite()
    {
        ui::ValueSlider slider(QStringLiteral("J1"), -1.0, 1.0, QStringLiteral(" rad"), 2, 1.0);
        slider.resize(320, 32);
        slider.setCommand(0.25);
        slider.show();
        slider.setCommand(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(slider.command(), 0.25);
        slider.setCommand(std::numeric_limits<double>::infinity());
        QCOMPARE(slider.command(), 0.25);
        for (const auto &text : {QStringLiteral("nan"), QStringLiteral("inf")}) {
            QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(300, 16));
            auto *editor = slider.findChild<QLineEdit *>();
            QVERIFY(editor && editor->isVisible());
            editor->setText(text);
            QTest::keyClick(editor, Qt::Key_Return);
            QCOMPARE(slider.command(), 0.25);
        }
        QTest::mouseClick(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(300, 16));
        auto *editor = slider.findChild<QLineEdit *>();
        editor->setText(QStringLiteral("0.75"));
        QTest::keyClick(editor, Qt::Key_Escape);
        QVERIFY(editor->isHidden());
        QCOMPARE(slider.command(), 0.25);
        slider.setActual(0.0);
        slider.setCommand(0.75);
        QVERIFY(slider.toolTip().contains(QStringLiteral("목표 0.75 rad")));
        slider.setActual(std::numeric_limits<double>::quiet_NaN());
        QVERIFY(!slider.toolTip().contains(QStringLiteral("현재")));
    }

    void armControlsRemainReachableInShortWindow()
    {
        ui::MainWindow window(new test::TestRobot);
        window.findChild<ui::DataPanel *>()->setDirectory({});
        window.resize(1100, 720);
        window.showView(ui::NavItem::Arm);
        window.show();
        QTest::qWait(20);
        const auto *arm = window.findChild<ui::ArmPanel *>();
        auto *scroll = window.findChild<QScrollArea *>("ArmContextScroll");
        QVERIFY(arm && scroll && scroll->widget() == arm);
        QCOMPARE(arm->findChild<ui::Robot3DView *>()->height(), 210);
        auto *stop = arm->findChild<QPushButton *>("ArmStopButton");
        QVERIFY(stop && stop->isVisible());
        scroll->ensureWidgetVisible(stop);
        QCoreApplication::processEvents();
        const QRect stopBounds(stop->mapTo(scroll->viewport(), QPoint(0, 0)), stop->size());
        QVERIFY(scroll->viewport()->rect().contains(stopBounds));
        QCOMPARE(window.height(), 720);
    }

    void cancellationTracksMissionIdInsteadOfDisplayName()
    {
        ui::MissionPanel panel;
        panel.setMissionState(QStringLiteral("running"));
        const QStringList steps{QStringLiteral("주행"), QStringLiteral("촬영")};
        panel.setProgress(QStringLiteral("동일한 이름"), 1, 2, steps, QStringLiteral("first"));
        panel.show();
        QSignalSpy stops(&panel, &ui::MissionPanel::missionStop);
        bool confirmationOpened = false;
        QTimer::singleShot(0, &panel, [&panel, &steps, &confirmationOpened] {
            panel.setProgress(QStringLiteral("동일한 이름"), 1, 2, steps, QStringLiteral("second"));
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            {
                confirmationOpened = true;
                dialog->button(QMessageBox::Yes)->click();
            }
        });
        panel.findChild<QPushButton *>("MissionCancelButton")->click();
        QVERIFY(confirmationOpened);
        QCOMPARE(stops.size(), 0);
    }
};

QTEST_MAIN(TestUiUx)
#include "test_ui_ux.moc"
