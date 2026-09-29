// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QAbstractItemView>
#include <QApplication>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTest>
#include <QTimer>

#include "panels/MissionLibraryPanel.h"
#include "panels/MissionPanel.h"
#include "panels/WaypointPanel.h"

using namespace hmi::ui;

namespace {
QPushButton *button(QWidget &panel, const QString &text)
{
    for (auto *candidate : panel.findChildren<QPushButton *>())
        if (candidate->text() == text)
            return candidate;
    return nullptr;
}

QVariantMap mission(const QVariantList &steps)
{
    return {{"id", "inspection-1"}, {"name", "정기 점검"},
            {"map_id", "map-1"}, {"revision", 1}, {"archived", false},
            {"steps", steps}};
}
}

class MissionTest : public QObject {
    Q_OBJECT
private slots:
    void storedNavigateMissionRuns()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setMissions({mission({QVariantMap{{"id", "move-1"}, {"type", "navigate"},
                                         {"location_id", "wp-1"}}})});
        auto *run = button(panel, QStringLiteral("선택 미션 실행"));
        QVERIFY(run);
        QVERIFY2(run->isEnabled(), qPrintable(run->toolTip()));
        QSignalSpy spy(&panel, &MissionLibraryPanel::runRequested);
        run->click();
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("inspection-1"));
    }

    void futureExecutorsCanBeSavedButNotRun()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setMissions({mission({QVariantMap{{"id", "move-1"}, {"type", "navigate"},
                                         {"location_id", "wp-1"}},
                                    QVariantMap{{"id", "photo-1"}, {"type", "capture"},
                                                {"preset", "detail"}}})});
        auto *run = button(panel, QStringLiteral("선택 미션 실행"));
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(run && save);
        QVERIFY(!run->isEnabled());
        QVERIFY(run->toolTip().contains(QStringLiteral("실행기")));
        QVERIFY(save->isEnabled());
        QSignalSpy spy(&panel, &MissionLibraryPanel::saveRequested);
        save->click();
        QCOMPARE(spy.size(), 1);
        const auto sent = spy.first().at(0).toMap();
        const auto photo = sent.value(QStringLiteral("steps")).toList().at(1).toMap();
        QCOMPARE(photo.value(QStringLiteral("preset")).toString(), QStringLiteral("detail"));
        QVERIFY(!photo.contains(QStringLiteral("capture_preset_id")));
    }

    void armStepUsesBridgePoseField()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setMissions({mission({QVariantMap{{"id", "arm-1"}, {"type", "arm_move"},
                                         {"pose", "inspection-a"}}})});
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(save);
        QSignalSpy spy(&panel, &MissionLibraryPanel::saveRequested);
        save->click();
        QCOMPARE(spy.size(), 1);
        const auto step = spy.first().at(0).toMap().value(QStringLiteral("steps"))
                              .toList().first().toMap();
        QCOMPARE(step.value(QStringLiteral("pose")).toString(), QStringLiteral("inspection-a"));
        QVERIFY(!step.contains(QStringLiteral("arm_pose_id")));
    }

    void editingDoesNotDiscardUnshownMissionMetadata()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        QVariantMap stored = mission({QVariantMap{{"id", "move-1"}, {"type", "navigate"},
                                                {"location_id", "wp-1"},
                                                {"requires", QStringList{QStringLiteral("nav")}}}});
        stored[QStringLiteral("description")] = QStringLiteral("차량 하부 점검");
        panel.setMissions({stored});
        auto *run = button(panel, QStringLiteral("선택 미션 실행"));
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(run && save);
        QVERIFY(run->isEnabled());
        QSignalSpy spy(&panel, &MissionLibraryPanel::saveRequested);
        save->click();
        QCOMPARE(spy.size(), 1);
        const QVariantMap sent = spy.first().at(0).toMap();
        QCOMPARE(sent.value(QStringLiteral("description")).toString(),
                 QStringLiteral("차량 하부 점검"));
        QCOMPARE(sent.value(QStringLiteral("steps")).toList().first().toMap()
                     .value(QStringLiteral("requires")).toStringList(),
                 QStringList{QStringLiteral("nav")});
    }

    void waypointCatalogCannotBeReordered()
    {
        WaypointPanel panel;
        panel.setWaypoints({QVariantMap{{"id", "wp-1"}, {"name", "입구"},
                                       {"x", 1.0}, {"y", 2.0}}});
        auto *list = panel.findChild<QListWidget *>();
        auto *go = button(panel, QStringLiteral("선택 위치로 이동"));
        QVERIFY(list && go);
        QCOMPARE(list->dragDropMode(), QAbstractItemView::NoDragDrop);
        QSignalSpy spy(&panel, &WaypointPanel::gotoRequested);
        panel.setEditingEnabled(true);
        list->setCurrentRow(0);
        go->click();
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("wp-1"));
    }

    void missionStepOrderCanBeEdited()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setMissions({mission({QVariantMap{{"id", "first"}, {"type", "navigate"},
                                         {"location_id", "wp-1"}},
                                    QVariantMap{{"id", "second"}, {"type", "navigate"},
                                                {"location_id", "wp-2"}}})});
        auto *steps = panel.findChild<QTableWidget *>(QStringLiteral("MissionSteps"));
        auto *up = button(panel, QStringLiteral("위로"));
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(steps && up && save);
        steps->selectRow(1);
        up->click();
        QCOMPARE(steps->item(0, 0)->text(), QStringLiteral("second"));
        QCOMPARE(steps->item(0, 2)->text(), QStringLiteral("wp-2"));
        QSignalSpy spy(&panel, &MissionLibraryPanel::saveRequested);
        save->click();
        QCOMPARE(spy.size(), 1);
        const auto sent = spy.first().at(0).toMap();
        QCOMPARE(sent.value(QStringLiteral("steps")).toList().first().toMap()
                     .value(QStringLiteral("id")).toString(), QStringLiteral("second"));
    }

    void refreshingMissionsPreservesDraftUntilRobotStoresIt()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        const QVariantMap stored = mission({QVariantMap{{"id", "move-1"},
            {"type", "navigate"}, {"location_id", "wp-1"}}});
        panel.setMissions({stored});
        auto *name = panel.findChild<QLineEdit *>();
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(name && save);
        name->setText(QStringLiteral("수정 중인 이름"));

        panel.setMissions({stored});
        QCOMPARE(name->text(), QStringLiteral("수정 중인 이름"));
        QSignalSpy saves(&panel, &MissionLibraryPanel::saveRequested);
        save->click();
        QCOMPARE(saves.size(), 1);
        QCOMPARE(saves.last().at(1).toULongLong(), 1ULL);

        QVariantMap updated = stored;
        updated[QStringLiteral("name")] = QStringLiteral("수정 중인 이름");
        updated[QStringLiteral("revision")] = 2;
        panel.setMissions({updated});
        QCOMPARE(name->text(), QStringLiteral("수정 중인 이름"));
        save->click();
        QCOMPARE(saves.size(), 2);
        QCOMPARE(saves.last().at(1).toULongLong(), 2ULL);
    }

    void stepTargetCanBeChosenByWaypointName()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setWaypoints({QVariantMap{{"id", "wp-1"}, {"name", "입구"}},
                            QVariantMap{{"id", "wp-2"}, {"name", "후면"}}});
        button(panel, QStringLiteral("새 미션"))->click();
        button(panel, QStringLiteral("단계 추가"))->click();
        auto *steps = panel.findChild<QTableWidget *>(QStringLiteral("MissionSteps"));
        auto *choose = button(panel, QStringLiteral("대상 선택"));
        QVERIFY(steps && choose);
        steps->selectRow(0);
        QVERIFY(choose->isEnabled());
        QTimer::singleShot(0, [] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            dialog->setTextValue(QStringLiteral("후면  (wp-2)"));
            dialog->accept();
        });
        choose->click();
        QCOMPARE(steps->item(0, 2)->text(), QStringLiteral("wp-2"));
    }

    void missionLibraryFitsNarrowContextColumn()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-with-a-long-name-2026-09-28"));
        auto *newMission = button(panel, QStringLiteral("새 미션"));
        QVERIFY(newMission);
        newMission->click();
        panel.resize(360, 800);
        panel.show();
        QCoreApplication::processEvents();

        QVERIFY2(panel.minimumSizeHint().width() <= 360,
                 "미션 라이브러리가 주행·미션 패널 너비보다 넓습니다.");
        for (auto *control : panel.findChildren<QPushButton *>()) {
            const QPoint right = control->mapTo(&panel, control->rect().topRight());
            QVERIFY2(right.x() < panel.width(), qPrintable(control->text()));
        }
    }

    void missionProgressDoesNotUseCatalogOrder()
    {
        MissionPanel panel;
        auto *pauseResume = panel.findChild<QPushButton *>(
            QStringLiteral("MissionPauseResumeButton"));
        auto *cancel = panel.findChild<QPushButton *>(
            QStringLiteral("MissionCancelButton"));
        QVERIFY(pauseResume && cancel);
        QVERIFY(pauseResume->isHidden());
        QVERIFY(cancel->isHidden());
        for (const auto *button : panel.findChildren<QPushButton *>())
            QVERIFY(button->text() != QStringLiteral("미션 선택"));
        panel.setProgress(QStringLiteral("정기 점검"), 1, 3,
                          {QStringLiteral("후면"), QStringLiteral("전면"), QStringLiteral("복귀 전")});
        panel.setMissionState(QStringLiteral("running"));
        QVERIFY(!pauseResume->isHidden());
        QVERIFY(!cancel->isHidden());
        const auto labels = panel.findChildren<QLabel *>();
        bool found = false;
        for (const auto *label : labels)
            found |= label->text().contains(QStringLiteral("2. 전면"));
        QVERIFY(found);
        auto *currentMission = panel.findChild<QLabel *>(QStringLiteral("CurrentMissionName"));
        auto *nextMission = panel.findChild<QLabel *>(QStringLiteral("NextMissionName"));
        QVERIFY(currentMission && nextMission);
        QCOMPARE(currentMission->text(), QStringLiteral("정기 점검"));
        QCOMPARE(nextMission->text(), QStringLiteral("예약된 미션 없음"));
        auto *pause = button(panel, QStringLiteral("일시정지"));
        QVERIFY(pause && pause->isEnabled());
        QSignalSpy pauseRequested(&panel, &MissionPanel::missionPause);
        pause->click();
        QCOMPARE(pauseRequested.size(), 1);
        panel.setMissionState(QStringLiteral("paused"));
        auto *resume = button(panel, QStringLiteral("미션 재개"));
        QVERIFY(resume && resume->isEnabled());
        QSignalSpy resumeRequested(&panel, &MissionPanel::missionResume);
        resume->click();
        QCOMPARE(resumeRequested.size(), 1);
        panel.setMissionState(QStringLiteral("completed"));
        QVERIFY(pauseResume->isHidden());
        QVERIFY(cancel->isHidden());
    }
};

QTEST_MAIN(MissionTest)
#include "test_mission.moc"
