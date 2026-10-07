// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTest>
#include <QTimer>

#include "panels/MissionLibraryPanel.h"
#include "panels/MissionPanel.h"
#include "theme/Style.h"

using namespace hmi::ui;

namespace {

QPushButton *button(QWidget &panel, const QString &text)
{
    for (auto *candidate : panel.findChildren<QPushButton *>())
        if (candidate->text() == text)
            return candidate;
    return nullptr;
}

QVariantMap storedMission(const QString &type = QStringLiteral("navigate"),
                          const QString &referenceKey = QStringLiteral("location_id"))
{
    return {{"id", "inspection"}, {"name", "점검"}, {"revision", 1},
        {"steps", QVariantList{QVariantMap{{"id", "step-1"}, {"type", type},
                                          {referenceKey, "first"}}}}};
}

QComboBox *targetFor(MissionLibraryPanel &panel)
{
    auto *steps = panel.findChild<QListWidget *>(QStringLiteral("MissionSteps"));
    return steps->itemWidget(steps->item(0))
        ->findChild<QComboBox *>(QStringLiteral("MissionStepTarget"));
}

void acceptConfirmation()
{
    for (auto *widget : QApplication::topLevelWidgets())
        if (auto *dialog = qobject_cast<QMessageBox *>(widget); dialog && dialog->isVisible()) {
            dialog->button(QMessageBox::Yes)->click();
            return;
        }
}

}  // namespace

class MissionUxTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qApp->setStyleSheet(hmi::theme::buildQss());
    }

    void duplicateTargetNamesTrackTheSelectedId_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<QString>("key");
        QTest::newRow("waypoint") << QStringLiteral("navigate") << QStringLiteral("location_id");
        QTest::newRow("arm-pose") << QStringLiteral("arm_move") << QStringLiteral("pose");
    }

    void duplicateTargetNamesTrackTheSelectedId()
    {
        QFETCH(QString, type);
        QFETCH(QString, key);
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        const QList<QVariantMap> targets{{{"id", "first"}, {"name", "같은 이름"}},
                                         {{"id", "second"}, {"name", "같은 이름"}}};
        panel.setWaypoints(targets);
        panel.setArmPosePresets(targets);
        panel.setMissions({storedMission(type, key)});
        panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection"))->click();
        auto *target = targetFor(panel);
        QCOMPARE(target->currentData().toString(), QStringLiteral("first"));
        target->setCurrentIndex(target->findData(QStringLiteral("second")));
        QSignalSpy saves(&panel, &MissionLibraryPanel::saveRequested);
        button(panel, QStringLiteral("저장"))->click();
        QCOMPARE(saves.size(), 1);
        QCOMPARE(saves.first().first().toMap().value(QStringLiteral("steps")).toList()
                     .first().toMap().value(key).toString(), QStringLiteral("second"));
    }

    void archivedWaypointAndUnknownStepCannotBeSaved_data()
    {
        QTest::addColumn<bool>("unknown");
        QTest::newRow("archived-waypoint") << false;
        QTest::newRow("unknown-step") << true;
    }

    void unchangedCatalogRefreshPreservesEditorControls()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        const QList<QVariantMap> targets{{{"id", "first"}, {"name", "입구"}}};
        panel.setWaypoints(targets);
        panel.setArmPosePresets(targets);
        panel.setMissions({storedMission()});
        panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection"))->click();
        const QPointer<QComboBox> originalTarget(targetFor(panel));
        panel.setWaypoints(targets);
        panel.setArmPosePresets(targets);
        QVERIFY(originalTarget);
        QCOMPARE(originalTarget.data(), targetFor(panel));
    }

    void archivedWaypointAndUnknownStepCannotBeSaved()
    {
        QFETCH(bool, unknown);
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        panel.setWaypoints({QVariantMap{{"id", "first"}, {"name", "입구"},
                                        {"archived", !unknown}}});
        panel.setMissions({storedMission(unknown ? QStringLiteral("future-task")
                                                : QStringLiteral("navigate"))});
        panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection"))->click();
        QSignalSpy saves(&panel, &MissionLibraryPanel::saveRequested);
        button(panel, QStringLiteral("저장"))->click();
        QCOMPARE(saves.size(), 0);
        const auto *status = panel.findChild<QLabel *>(QStringLiteral("MissionLibraryStatus"));
        QVERIFY(status->text().contains(unknown ? QStringLiteral("종류")
                                                : QStringLiteral("웨이포인트")));
    }

    void emptyEditorGuidesCreationAndListUsesOnlyItsOwnHeight()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        button(panel, QStringLiteral("새 미션"))->click();
        auto *hint = panel.findChild<QLabel *>(QStringLiteral("MissionStepsEmpty"));
        QVERIFY(!hint->isHidden());
        for (int i = 0; i < 5; ++i)
            button(panel, QStringLiteral("단계 추가"))->click();
        QVERIFY(hint->isHidden());
        auto *pages = panel.findChild<QStackedWidget *>(QStringLiteral("MissionLibraryPages"));
        const int editorHeight = pages->sizeHint().height();
        button(panel, QStringLiteral("취소"))->click();
        QCOMPARE(pages->currentIndex(), 0);
        QVERIFY(pages->sizeHint().height() < editorHeight);
    }

    void stepTypeChangesUpdateRowHeight()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        button(panel, QStringLiteral("새 미션"))->click();
        button(panel, QStringLiteral("단계 추가"))->click();
        panel.resize(280, 700);
        panel.show();
        QCoreApplication::processEvents();
        auto *steps = panel.findChild<QListWidget *>(QStringLiteral("MissionSteps"));
        auto *type = panel.findChild<QComboBox *>(QStringLiteral("MissionStepType_0"));
        const int targetHeight = steps->item(0)->sizeHint().height();
        type->setCurrentIndex(type->findData(QStringLiteral("dock")));
        QVERIFY(targetFor(panel)->isHidden());
        QVERIFY(steps->item(0)->sizeHint().height() < targetHeight);
        type->setCurrentIndex(type->findData(QStringLiteral("navigate")));
        QVERIFY(!targetFor(panel)->isHidden());
        QCOMPARE(steps->item(0)->sizeHint().height(), targetHeight);
        for (const auto *control : panel.findChildren<QPushButton *>()) {
            if (!control->isVisible())
                continue;
            QVERIFY(control->mapTo(&panel, control->rect().topRight()).x() < panel.width());
        }
        const QString artifactDir = qEnvironmentVariable("HMI_UX_SCREENSHOT_DIR");
        if (!artifactDir.isEmpty())
            QVERIFY(panel.grab().save(QDir(artifactDir).filePath(QStringLiteral("mission-editor-narrow.png"))));
    }

    void longSavedNamesArePlainAndElidedWithActionsVisible()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        auto mission = storedMission();
        const QString fullName = QStringLiteral("<b>아주 긴 점검 미션 이름</b>").repeated(6);
        mission[QStringLiteral("name")] = fullName;
        panel.setMissions({mission});
        panel.resize(280, 500);
        panel.show();
        QCoreApplication::processEvents();
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("MissionList"));
        auto *name = list->itemWidget(list->item(0))
            ->findChild<QLabel *>(QStringLiteral("MissionSavedName"));
        QCOMPARE(name->textFormat(), Qt::PlainText);
        QCOMPARE(name->toolTip(), fullName);
        QCOMPARE(name->accessibleName(), fullName);
        QVERIFY(name->text().endsWith(QChar(0x2026)));
        QVERIFY(name->fontMetrics().horizontalAdvance(name->text()) <= name->width());
        for (const auto *control : list->itemWidget(list->item(0))->findChildren<QPushButton *>())
            QVERIFY(control->mapTo(list->viewport(), control->rect().topRight()).x()
                    < list->viewport()->width());
        QVERIFY(panel.minimumSizeHint().width() <= 280);
        const QString artifactDir = qEnvironmentVariable("HMI_UX_SCREENSHOT_DIR");
        if (!artifactDir.isEmpty())
            QVERIFY(panel.grab().save(QDir(artifactDir).filePath(QStringLiteral("mission-list-narrow.png"))));
    }

    void deletionWaitsForAcknowledgementAndCatalog_data()
    {
        QTest::addColumn<bool>("catalogFirst");
        QTest::newRow("ack-first") << false;
        QTest::newRow("catalog-first") << true;
    }

    void deletionWaitsForAcknowledgementAndCatalog()
    {
        QFETCH(bool, catalogFirst);
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        panel.setMissions({storedMission()});
        QSignalSpy archives(&panel, &MissionLibraryPanel::archiveRequested);
        QTimer::singleShot(0, acceptConfirmation);
        panel.findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection"))->click();
        QCOMPARE(archives.size(), 1);
        QVERIFY(!button(panel, QStringLiteral("새 미션"))->isEnabled());
        if (catalogFirst) {
            panel.setMissions({});
            QVERIFY(!button(panel, QStringLiteral("새 미션"))->isEnabled());
        }
        panel.handleCommandResult(QStringLiteral("cmd/missions/archive"), true, {}, {});
        const auto *status = panel.findChild<QLabel *>(QStringLiteral("MissionLibraryStatus"));
        if (!catalogFirst) {
            QCOMPARE(status->text(), QStringLiteral("삭제 확인 중…"));
            auto *remove = panel.findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection"));
            QVERIFY(!remove->isEnabled());
            remove->click();
            QCOMPARE(archives.size(), 1);
            panel.setMissions({});
        }
        QCOMPARE(status->text(), QStringLiteral("미션 삭제됨"));
        QVERIFY(button(panel, QStringLiteral("새 미션"))->isEnabled());
    }

    void rejectedDeletionRestoresManagementActions()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("map"));
        panel.setMissions({storedMission()});
        QTimer::singleShot(0, acceptConfirmation);
        panel.findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection"))->click();
        panel.handleCommandResult(QStringLiteral("cmd/missions/archive"), false,
                                  QStringLiteral("E_BUSY"), QStringLiteral("<b>다른 변경</b>"));
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("MissionList"));
        QCOMPARE(list->count(), 1);
        auto *row = list->itemWidget(list->item(0));
        QVERIFY(row->findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection"))->isEnabled());
        QVERIFY(row->findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection"))->isEnabled());
        QVERIFY(button(panel, QStringLiteral("새 미션"))->isEnabled());
        auto *status = panel.findChild<QLabel *>(QStringLiteral("MissionLibraryStatus"));
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("<b>다른 변경</b>")));
    }

    void cancelledDeleteContextIgnoresItsLateResult()
    {
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("first-map"));
        panel.setMissions({storedMission()});
        QTimer::singleShot(0, acceptConfirmation);
        panel.findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection"))->click();
        panel.setMapId(QStringLiteral("second-map"));
        panel.handleCommandResult(QStringLiteral("cmd/missions/archive"), false,
                                  QStringLiteral("E_BUSY"), QStringLiteral("old result"));
        QVERIFY(panel.findChild<QLabel *>(QStringLiteral("MissionLibraryStatus"))->text().isEmpty());
        QVERIFY(button(panel, QStringLiteral("새 미션"))->isEnabled());
    }

    void crossMapArchiveResultsCannotAffectTheNextDelete_data()
    {
        QTest::addColumn<bool>("oldOk");
        QTest::addColumn<QString>("oldCode");
        QTest::newRow("accepted") << true << QString();
        QTest::newRow("rejected") << false << QStringLiteral("E_BUSY");
        QTest::newRow("timeout") << false << QStringLiteral("E_TIMEOUT");
    }

    void crossMapArchiveResultsCannotAffectTheNextDelete()
    {
        QFETCH(bool, oldOk);
        QFETCH(QString, oldCode);
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("first-map"));
        panel.setMissions({storedMission()});
        QSignalSpy archives(&panel, &MissionLibraryPanel::archiveRequested);
        QTimer::singleShot(0, acceptConfirmation);
        panel.findChild<QPushButton *>(QStringLiteral("MissionDelete_inspection"))->click();
        panel.setMapId(QStringLiteral("second-map"));
        auto second = storedMission();
        second[QStringLiteral("id")] = QStringLiteral("second");
        panel.setMissions({second});
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("MissionList"));
        auto *remove = list->itemWidget(list->item(0))
            ->findChild<QPushButton *>(QStringLiteral("MissionDelete_second"));
        QVERIFY(!remove->isEnabled());
        QVERIFY(panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_second"))->isEnabled());
        QVERIFY(button(panel, QStringLiteral("새 미션"))->isEnabled());
        remove->click();
        QCOMPARE(archives.size(), 1);
        panel.handleCommandResult(QStringLiteral("cmd/missions/archive"), oldOk, oldCode, QStringLiteral("old"));
        QVERIFY(panel.findChild<QLabel *>(QStringLiteral("MissionLibraryStatus"))->text().isEmpty());
        remove = list->itemWidget(list->item(0))
            ->findChild<QPushButton *>(QStringLiteral("MissionDelete_second"));
        QVERIFY(remove->isEnabled());
        QTimer::singleShot(0, acceptConfirmation);
        remove->click();
        QCOMPARE(archives.size(), 2);
        QCOMPARE(archives.last().first().toString(), QStringLiteral("second"));
        panel.handleCommandResult(QStringLiteral("cmd/missions/archive"), true, {}, {});
        panel.setMissions({});
        QCOMPARE(panel.findChild<QLabel *>(QStringLiteral("MissionLibraryStatus"))->text(),
                 QStringLiteral("미션 삭제됨"));
    }

    void crossMapSaveResultsCannotAffectTheNextDraft_data()
    {
        crossMapArchiveResultsCannotAffectTheNextDelete_data();
    }

    void crossMapSaveResultsCannotAffectTheNextDraft()
    {
        QFETCH(bool, oldOk);
        QFETCH(QString, oldCode);
        MissionLibraryPanel panel;
        panel.setMapId(QStringLiteral("first-map"));
        panel.setWaypoints({QVariantMap{{"id", "first"}, {"name", "입구"}}});
        panel.setMissions({storedMission()});
        panel.findChild<QPushButton *>(QStringLiteral("MissionEdit_inspection"))->click();
        QSignalSpy saves(&panel, &MissionLibraryPanel::saveRequested);
        auto *save = button(panel, QStringLiteral("저장"));
        save->click();
        QCOMPARE(saves.size(), 1);
        panel.setMapId(QStringLiteral("second-map"));
        button(panel, QStringLiteral("새 미션"))->click();
        auto *name = panel.findChild<QLineEdit *>(QStringLiteral("MissionName"));
        name->setText(QStringLiteral("새 지도 초안"));
        button(panel, QStringLiteral("단계 추가"))->click();
        targetFor(panel)->setCurrentIndex(targetFor(panel)->findData(QStringLiteral("first")));
        QVERIFY(name->isEnabled());
        QVERIFY(button(panel, QStringLiteral("단계 추가"))->isEnabled());
        QVERIFY(!save->isEnabled());
        save->click();
        QCOMPARE(saves.size(), 1);
        panel.handleCommandResult(QStringLiteral("cmd/missions/save"), oldOk, oldCode, QStringLiteral("old"));
        QVERIFY(save->isEnabled());
        QCOMPARE(name->text(), QStringLiteral("새 지도 초안"));
        QVERIFY(panel.findChild<QLabel *>(QStringLiteral("MissionLibraryStatus"))->text().isEmpty());
        save->click();
        QCOMPARE(saves.size(), 2);
        QCOMPARE(saves.last().at(1).toULongLong(), 0ULL);
        panel.handleCommandResult(QStringLiteral("cmd/missions/save"), true, {}, {});
        auto saved = saves.last().first().toMap();
        saved[QStringLiteral("revision")] = 1;
        panel.setMissions({saved});
        QCOMPARE(panel.findChild<QStackedWidget *>(QStringLiteral("MissionLibraryPages"))->currentIndex(), 0);
    }

    void cancellationConfirmationRevalidatesMission_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("same-mission") << 0;
        QTest::newRow("replacement") << 1;
        QTest::newRow("state-round-trip") << 2;
    }

    void cancellationConfirmationRevalidatesMission()
    {
        QFETCH(int, change);
        MissionPanel panel;
        panel.setProgress(QStringLiteral("원래 미션"), 1, 3, {});
        panel.setMissionState(QStringLiteral("running"));
        QSignalSpy stops(&panel, &MissionPanel::missionStop);
        QTimer::singleShot(0, &panel, [&] {
            if (change == 1)
                panel.setProgress(QStringLiteral("다음 미션"), 0, 3, {});
            if (change == 2) {
                panel.setMissionState(QStringLiteral("idle"));
                panel.setMissionState(QStringLiteral("running"));
            }
            acceptConfirmation();
        });
        panel.findChild<QPushButton *>(QStringLiteral("MissionCancelButton"))->click();
        QCOMPARE(stops.size(), change == 0 ? 1 : 0);
    }

    void robotFaultDetailAndStepLabelsArePlainText()
    {
        MissionPanel panel;
        panel.setProgress(QStringLiteral("<b>미션 이름</b>"), 0, 1,
                          {QStringLiteral("<b>작업 이름</b>")});
        panel.setMissionState(QStringLiteral("running"));
        auto *name = panel.findChild<QLabel *>(QStringLiteral("CurrentMissionName"));
        auto *step = panel.findChild<QLabel *>(QStringLiteral("MissionCurrentStep"));
        QCOMPARE(name->textFormat(), Qt::PlainText);
        QCOMPARE(step->textFormat(), Qt::PlainText);
        QCOMPARE(name->text(), QStringLiteral("<b>미션 이름</b>"));
        panel.setMissionState(QStringLiteral("emergency_stopped"));
        panel.setMissionDetails(QStringLiteral("SAFETY_STOP"), QStringLiteral("<b>안전 정지 원인</b>"));
        auto *reason = panel.findChild<QLabel *>(QStringLiteral("MissionReason"));
        QCOMPARE(reason->textFormat(), Qt::PlainText);
        QCOMPARE(reason->text(), QStringLiteral("<b>안전 정지 원인</b>"));
        QVERIFY(!reason->isHidden());
    }
};

QTEST_MAIN(MissionUxTest)
#include "test_mission_ux.moc"
