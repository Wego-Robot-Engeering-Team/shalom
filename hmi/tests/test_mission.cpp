// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QTimer>

#include "panels/MissionLibraryPanel.h"
#include "panels/MissionPanel.h"
#include "panels/WaypointPanel.h"
#include "widgets/CatalogRow.h"

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
            {"revision", 1}, {"archived", false},
            {"steps", steps}};
}
}

class MissionTest : public QObject {
    Q_OBJECT
private slots:
    void mapSwitchClearsPreviousMissionList()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setMissions({mission({QVariantMap{{"id", "move-1"}, {"type", "navigate"},
                                         {"location_id", "wp-1"}}})});
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("MissionList"));
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        panel.setMapId(QStringLiteral("map-2"));
        QCOMPARE(list->count(), 0);
    }

    void storedNavigateMissionRuns()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setMissions({mission({QVariantMap{{"id", "move-1"}, {"type", "navigate"},
                                         {"location_id", "wp-1"}}})});
        auto *run = panel.findChild<QPushButton *>(QStringLiteral("MissionRun_inspection-1"));
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
        auto *run = panel.findChild<QPushButton *>(QStringLiteral("MissionRun_inspection-1"));
        auto *edit = panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection-1"));
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(run && edit && save);
        QVERIFY(!run->isEnabled());
        QVERIFY(run->toolTip().contains(QStringLiteral("실행기")));
        edit->click();
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
        auto *edit = panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection-1"));
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(edit && save);
        edit->click();
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
        auto *run = panel.findChild<QPushButton *>(QStringLiteral("MissionRun_inspection-1"));
        auto *edit = panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection-1"));
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(run && edit && save);
        QVERIFY(run->isEnabled());
        edit->click();
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
        auto *go = list ? static_cast<CatalogRow *>(list->itemWidget(list->item(0)))
                            ->applyButton() : nullptr;
        QVERIFY(list && go);
        QCOMPARE(list->dragDropMode(), QAbstractItemView::NoDragDrop);
        QSignalSpy spy(&panel, &WaypointPanel::gotoRequested);
        panel.setEditingEnabled(true);
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
        auto *edit = panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection-1"));
        QVERIFY(edit);
        edit->click();
        auto *steps = panel.findChild<QListWidget *>(QStringLiteral("MissionSteps"));
        auto *up = panel.findChild<QPushButton *>(QStringLiteral("MissionStepUp_1"));
        auto *save = button(panel, QStringLiteral("저장"));
        QVERIFY(steps && up && save);
        up->click();
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
        auto *edit = panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection-1"));
        QVERIFY(edit);
        edit->click();
        auto *name = panel.findChild<QLineEdit *>(QStringLiteral("MissionName"));
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
        auto *pages = panel.findChild<QStackedWidget *>(QStringLiteral("MissionLibraryPages"));
        QVERIFY(pages);
        QCOMPARE(pages->currentIndex(), 0);
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("MissionList"));
        edit = list && list->count()
            ? list->itemWidget(list->item(0))->findChild<QPushButton *>(
                  QStringLiteral("MissionEdit_inspection-1")) : nullptr;
        QVERIFY(edit);
        edit->click();
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
        auto *steps = panel.findChild<QListWidget *>(QStringLiteral("MissionSteps"));
        auto *target = steps && steps->count()
            ? steps->itemWidget(steps->item(0))->findChild<QComboBox *>(
                  QStringLiteral("MissionStepTarget")) : nullptr;
        QVERIFY(steps && target);
        const int selected = target->findData(QStringLiteral("wp-2"));
        QVERIFY(selected > 0);
        QCOMPARE(target->itemText(selected), QStringLiteral("후면"));
        target->setCurrentIndex(selected);
        auto *name = panel.findChild<QLineEdit *>(QStringLiteral("MissionName"));
        name->setText(QStringLiteral("점검"));
        QSignalSpy saves(&panel, &MissionLibraryPanel::saveRequested);
        button(panel, QStringLiteral("저장"))->click();
        QCOMPARE(saves.size(), 1);
        QCOMPARE(saves.first().first().toMap().value(QStringLiteral("steps")).toList()
                     .first().toMap().value(QStringLiteral("location_id")).toString(),
                 QStringLiteral("wp-2"));
    }

    void savedMissionActionsAreOnEachRowAndEditingIsSeparate()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        QVariantMap first = mission({QVariantMap{{"id", "move-1"}, {"type", "navigate"},
                                                 {"location_id", "wp-1"}}});
        QVariantMap second = first;
        second[QStringLiteral("id")] = QStringLiteral("inspection-2");
        second[QStringLiteral("name")] = QStringLiteral("추가 점검");
        panel.setMissions({first, second});
        auto *pages = panel.findChild<QStackedWidget *>(QStringLiteral("MissionLibraryPages"));
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("MissionList"));
        QVERIFY(pages && list);
        QCOMPARE(pages->currentIndex(), 0);
        QCOMPARE(list->count(), 2);
        auto *row = list->itemWidget(list->item(1));
        QVERIFY(row->findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection-2")));
        QVERIFY(row->findChild<QPushButton *>(QStringLiteral("MissionRun_inspection-2")));
        QVERIFY(row->findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection-2")));
        QVERIFY(!button(panel, QStringLiteral("선택 미션 실행")));
        row->findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection-2"))->click();
        QCOMPARE(pages->currentIndex(), 1);
        QCOMPARE(panel.findChild<QLineEdit *>(QStringLiteral("MissionName"))->text(),
                 QStringLiteral("추가 점검"));
        button(panel, QStringLiteral("취소"))->click();
        QCOMPARE(pages->currentIndex(), 0);
        QSignalSpy archives(&panel, &MissionLibraryPanel::archiveRequested);
        bool dialogShown = false;
        QTimer::singleShot(0, [&dialogShown] {
            for (auto *widget : QApplication::topLevelWidgets()) {
                if (auto *dialog = qobject_cast<QMessageBox *>(widget)) {
                    dialogShown = true;
                    dialog->button(QMessageBox::Yes)->click();
                    return;
                }
            }
        });
        row->findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection-2"))->click();
        QVERIFY(dialogShown);
        QCOMPARE(archives.size(), 1);
        QCOMPARE(archives.first().at(0).toString(), QStringLiteral("inspection-2"));
        QCOMPARE(archives.first().at(1).toULongLong(), 1ULL);
    }

    void stepTargetsUseNamedCatalogsWithoutIdDialog()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setWaypoints({QVariantMap{{"id", "wp-1"}, {"name", "차량 앞쪽"}}});
        panel.setArmPosePresets({QVariantMap{{"id", "pose-1"}, {"name", "왼쪽 점검"}}});
        button(panel, QStringLiteral("새 미션"))->click();
        auto *name = panel.findChild<QLineEdit *>(QStringLiteral("MissionName"));
        auto *steps = panel.findChild<QListWidget *>(QStringLiteral("MissionSteps"));
        QVERIFY(name && steps);
        name->setText(QStringLiteral("점검 미션"));
        const auto targetAt = [steps](int index) {
            return steps->itemWidget(steps->item(index))
                ->findChild<QComboBox *>(QStringLiteral("MissionStepTarget"));
        };
        button(panel, QStringLiteral("단계 추가"))->click();
        auto *target = targetAt(0);
        QCOMPARE(target->itemText(target->findData(QStringLiteral("wp-1"))),
                 QStringLiteral("차량 앞쪽"));
        target->setCurrentIndex(target->findData(QStringLiteral("wp-1")));

        button(panel, QStringLiteral("단계 추가"))->click();
        auto *type = panel.findChild<QComboBox *>(QStringLiteral("MissionStepType_1"));
        QVERIFY(type);
        type->setCurrentIndex(type->findData(QStringLiteral("arm_move")));
        target = targetAt(1);
        QCOMPARE(target->itemText(target->findData(QStringLiteral("pose-1"))),
                 QStringLiteral("왼쪽 점검"));
        target->setCurrentIndex(target->findData(QStringLiteral("pose-1")));

        button(panel, QStringLiteral("단계 추가"))->click();
        type = panel.findChild<QComboBox *>(QStringLiteral("MissionStepType_2"));
        type->setCurrentIndex(type->findData(QStringLiteral("capture")));
        target = targetAt(2);
        QVERIFY(target->isEditable());
        target->setEditText(QStringLiteral("detail"));

        QSignalSpy saves(&panel, &MissionLibraryPanel::saveRequested);
        button(panel, QStringLiteral("저장"))->click();
        QCOMPARE(saves.size(), 1);
        const auto savedSteps = saves.first().first().toMap()
                                    .value(QStringLiteral("steps")).toList();
        QCOMPARE(savedSteps.at(0).toMap().value(QStringLiteral("location_id")).toString(),
                 QStringLiteral("wp-1"));
        QCOMPARE(savedSteps.at(1).toMap().value(QStringLiteral("pose")).toString(),
                 QStringLiteral("pose-1"));
        QCOMPARE(savedSteps.at(2).toMap().value(QStringLiteral("preset")).toString(),
                 QStringLiteral("detail"));
    }

    void rejectedSaveKeepsTheDraftEditable()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-1"));
        panel.setWaypoints({QVariantMap{{"id", "wp-1"}, {"name", "입구"}}});
        button(panel, QStringLiteral("새 미션"))->click();
        panel.findChild<QLineEdit *>(QStringLiteral("MissionName"))
            ->setText(QStringLiteral("다시 저장할 미션"));
        button(panel, QStringLiteral("단계 추가"))->click();
        auto *steps = panel.findChild<QListWidget *>(QStringLiteral("MissionSteps"));
        auto *target = steps->itemWidget(steps->item(0))
                           ->findChild<QComboBox *>(QStringLiteral("MissionStepTarget"));
        target->setCurrentIndex(target->findData(QStringLiteral("wp-1")));
        auto *save = button(panel, QStringLiteral("저장"));
        save->click();
        QVERIFY(!save->isEnabled());
        panel.handleCommandResult(QStringLiteral("cmd/missions/save"), false,
                                  QStringLiteral("BUSY"), QStringLiteral("재시도"));
        QVERIFY(save->isEnabled());
        QCOMPARE(panel.findChild<QStackedWidget *>(QStringLiteral("MissionLibraryPages"))
                     ->currentIndex(), 1);
        QCOMPARE(target->currentData().toString(), QStringLiteral("wp-1"));
    }

    void missionLibraryFitsNarrowContextColumn()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map-with-a-long-name-2026-09-28"));
        auto *newMission = button(panel, QStringLiteral("새 미션"));
        QVERIFY(newMission);
        newMission->click();
        button(panel, QStringLiteral("단계 추가"))->click();
        panel.resize(360, 800);
        panel.show();
        QCoreApplication::processEvents();

        QVERIFY2(panel.minimumSizeHint().width() <= 360,
                 "미션 라이브러리가 주행·미션 패널 너비보다 넓습니다.");
        for (auto *control : panel.findChildren<QPushButton *>()) {
            if (!control->isVisible())
                continue;
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
