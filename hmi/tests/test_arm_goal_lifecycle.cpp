// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QLabel>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

#include <cmath>
#include <limits>

#include "panels/ArmPanel.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"
#include "widgets/ValueSlider.h"

using namespace hmi;

class TestArmGoalLifecycle : public QObject {
    Q_OBJECT

    static QList<double> actual()
    {
        return {0.0, -0.785, 0.0, -2.356, 0.0, 1.571};
    }

    static void prepare(ui::ArmPanel &panel)
    {
        panel.setControlsEnabled(true);
        panel.setExecutionAvailable(true);
        panel.setArmState(actual(), 1.0, 1.0);
    }

    static ui::ValueSlider *joint(ui::ArmPanel &panel, int index = 1)
    {
        return panel.findChild<ui::ValueSlider *>(QStringLiteral("Joint%1").arg(index));
    }

    static QPushButton *send(ui::ArmPanel &panel, bool endEffector = false)
    {
        const auto text = endEffector ? QStringLiteral("끝단 목표 보내기")
                                     : QStringLiteral("관절 목표 보내기");
        for (auto *button : panel.findChildren<QPushButton *>())
            if (button->text() == text)
                return button;
        return nullptr;
    }

    static QString previewText(ui::ArmPanel &panel)
    {
        return panel.findChild<QLabel *>(QStringLiteral("ArmPreviewStatus"))->text();
    }

    static QList<double> draft(ui::ArmPanel &panel)
    {
        QList<double> positions;
        for (int i = 1; i <= 6; ++i)
            positions << joint(panel, i)->command();
        return positions;
    }

    static void acknowledge(ui::ArmPanel &panel, bool ok = true)
    {
        panel.setCommandResult(QStringLiteral("cmd/arm/joint_goal"), ok,
                               ok ? QString() : QStringLiteral("E_BUSY"), {});
    }

    static void verifyFollows(ui::ArmPanel &panel, const QList<double> &positions)
    {
        for (int i = 1; i <= positions.size(); ++i)
            QVERIFY(std::abs(joint(panel, i)->command() - positions.at(i - 1)) < 0.004);
        QVERIFY(previewText(panel).isEmpty());
        QVERIFY(!send(panel)->isEnabled());
    }

private slots:
    void initialAndUneditedFeedbackTrackActual()
    {
        ui::ArmPanel panel;
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        prepare(panel);
        verifyFollows(panel, actual());
        auto moved = actual();
        moved[0] = 0.7;
        moved[1] = -1.2;
        panel.setArmState(moved, 1.0, 1.0);
        verifyFollows(panel, moved);
        QCOMPARE(goals.size(), 0);
    }

    void unsentDraftIsNotCompletedByMatchingFeedbackOrUnrelatedAck()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        panel.setArmState(target, 1.0, 1.0);
        acknowledge(panel);
        auto moved = target;
        moved[0] = 0.8;
        panel.setArmState(moved, 1.0, 1.0);
        QCOMPARE(draft(panel), target);
        QVERIFY(send(panel)->isEnabled());
        QCOMPARE(previewText(panel), QStringLiteral("목표 미리보기"));
    }

    void acceptedGoalCompletesAndThenTracksExternalMotion()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        send(panel)->click();
        QCOMPARE(goals.size(), 1);
        QVERIFY(!send(panel)->isEnabled());
        acknowledge(panel);
        auto notReached = target;
        notReached[0] -= 0.1;
        panel.setArmState(notReached, 1.0, 1.0);
        QCOMPARE(draft(panel), target);
        auto reached = target;
        reached[0] += 0.0005;
        panel.setArmState(reached, 1.0, 1.0);
        verifyFollows(panel, reached);
        auto external = target;
        external[0] = 1.0;
        external[1] = -1.3;
        panel.setArmState(external, 1.0, 1.0);
        verifyFollows(panel, external);
        QCOMPARE(goals.size(), 1);
    }

    void feedbackBeforeAckCompletesOnlyAfterAcceptance()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        send(panel)->click();
        panel.setArmState(target, 1.0, 1.0);
        acknowledge(panel);
        auto external = target;
        external[0] = 0.8;
        panel.setArmState(external, 1.0, 1.0);
        verifyFollows(panel, external);
        QCOMPARE(goals.size(), 1);
    }

    void crossingTargetBeforeAckDoesNotCompleteUsingOldFeedback()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        send(panel)->click();
        panel.setArmState(target, 1.0, 1.0);
        auto movedAway = target;
        movedAway[0] += 0.3;
        panel.setArmState(movedAway, 1.0, 1.0);
        acknowledge(panel);
        QCOMPARE(draft(panel), target);
        QVERIFY(!previewText(panel).isEmpty());
        panel.setArmState(target, 1.0, 1.0);
        verifyFollows(panel, target);
    }

    void rejectedRequestPreservesDraft()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        send(panel)->click();
        panel.setArmState(target, 1.0, 1.0);
        acknowledge(panel, false);
        auto moved = target;
        moved[0] = 0.8;
        panel.setArmState(moved, 1.0, 1.0);
        QCOMPARE(draft(panel), target);
        QVERIFY(send(panel)->isEnabled());
        QVERIFY(!previewText(panel).isEmpty());
    }

    void editsDuringAnOlderGoalArePreserved_data()
    {
        QTest::addColumn<bool>("editBeforeAck");
        QTest::newRow("edit-before-ack") << true;
        QTest::newRow("edit-after-ack") << false;
    }

    void editsDuringAnOlderGoalArePreserved()
    {
        QFETCH(bool, editBeforeAck);
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto olderTarget = draft(panel);
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        send(panel)->click();
        if (!editBeforeAck)
            acknowledge(panel);
        joint(panel)->setCommand(0.8);
        joint(panel, 2)->setCommand(-1.4);
        const auto newerDraft = draft(panel);
        if (editBeforeAck)
            acknowledge(panel);
        panel.setArmState(olderTarget, 1.0, 1.0);
        auto external = olderTarget;
        external[0] = 1.1;
        panel.setArmState(external, 1.0, 1.0);
        QCOMPARE(draft(panel), newerDraft);
        QVERIFY(send(panel)->isEnabled());
        QCOMPARE(goals.size(), 1);
        send(panel)->click();
        QCOMPARE(goals.size(), 2);
        QCOMPARE(qvariant_cast<QList<double>>(goals.last().first()), newerDraft);
    }

    void everyJointMustBeWithinCompletionTolerance()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        send(panel)->click();
        acknowledge(panel);
        auto notReached = target;
        notReached[5] += 0.004;
        panel.setArmState(notReached, 1.0, 1.0);
        QCOMPARE(draft(panel), target);
        QVERIFY(!previewText(panel).isEmpty());
        panel.setArmState(target, 1.0, 1.0);
        verifyFollows(panel, target);
    }

    void poseManagementDraftIsPreservedWhenOlderGoalCompletes()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto olderTarget = draft(panel);
        send(panel)->click();
        panel.setPosePresets({{{"id", "preset"}, {"name", "Preset"}, {"revision", 1},
            {"positions", QVariantList{0.8, -1.2, 0.0, -2.0, 0.0, 1.5}}}});
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        auto *row = dynamic_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row);
        row->editButton()->click();
        auto *editor = row->findChild<QDoubleSpinBox *>(QStringLiteral("PoseRowJoint1"));
        QVERIFY(editor);
        editor->setValue(30.0);
        const auto poseDraft = draft(panel);
        acknowledge(panel);
        panel.setArmState(olderTarget, 1.0, 1.0);
        QCOMPARE(draft(panel), poseDraft);
        QCOMPARE(editor->value(), 30.0);
        QVERIFY(row->isEditing());
        QVERIFY(send(panel)->isEnabled());
    }

    void pendingAckBlocksRepeatedAndSavedPoseSubmissions()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        panel.setPosePresets({{{"id", "preset"}, {"name", "Preset"}, {"revision", 1},
            {"positions", QVariantList{0.8, -1.2, 0.0, -2.0, 0.0, 1.5}}}});
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        auto *row = dynamic_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row);
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        send(panel)->click();
        send(panel)->click();
        send(panel, true)->click();
        row->applyButton()->click();
        QCOMPARE(goals.size(), 1);
        QVERIFY(joint(panel)->isEnabled());
        QVERIFY(panel.findChild<QPushButton *>(QStringLiteral("ArmSavePreviewPose"))->isEnabled());
        QVERIFY(!row->applyButton()->isEnabled());
        acknowledge(panel);
        QVERIFY(row->applyButton()->isEnabled());
    }

    void staleFeedbackCannotCompleteGoalOnAck()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        send(panel)->click();
        panel.setArmState(target, 1.0, 1.0);
        panel.setFeedbackFresh(false);
        acknowledge(panel);
        auto moved = target;
        moved[0] = 0.8;
        panel.setArmState(moved, 1.0, 1.0);
        QCOMPARE(draft(panel), target);
        QVERIFY(!previewText(panel).isEmpty());
        panel.setArmState(target, 1.0, 1.0);
        verifyFollows(panel, target);
    }

    void invalidFeedbackCannotCompleteGoal()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        send(panel)->click();
        acknowledge(panel);
        auto invalid = target;
        invalid[5] = std::numeric_limits<double>::quiet_NaN();
        panel.setArmState(invalid, 1.0, 1.0);
        QCOMPARE(draft(panel), target);
        QVERIFY(!previewText(panel).isEmpty());
        panel.setArmState(target, 1.0, 1.0);
        verifyFollows(panel, target);
    }

    void endEffectorSubmissionUsesTheSameJointLifecycle()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        const auto target = draft(panel);
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        send(panel, true)->click();
        QCOMPARE(goals.size(), 1);
        QCOMPARE(qvariant_cast<QList<double>>(goals.first().first()), target);
        acknowledge(panel);
        panel.setArmState(target, 1.0, 1.0);
        auto external = target;
        external[0] = 0.9;
        panel.setArmState(external, 1.0, 1.0);
        verifyFollows(panel, external);
    }

    void savedPoseSubmissionKeepsItsExactTarget()
    {
        ui::ArmPanel panel;
        prepare(panel);
        const QList<double> target{0.3333333, -1.2222222, 0.5555555, -2.3333333, 0.0, 1.5};
        QVariantList positions;
        for (const double value : target)
            positions << value;
        panel.setPosePresets({{{"id", "preset"}, {"name", "Preset"}, {"revision", 1},
                              {"positions", positions}}});
        auto *list = panel.findChild<QListWidget *>(QStringLiteral("SavedArmPosePresets"));
        auto *row = dynamic_cast<ui::CatalogRow *>(list->itemWidget(list->item(0)));
        QVERIFY(row);
        QSignalSpy goals(&panel, &ui::ArmPanel::jointGoal);
        row->applyButton()->click();
        QCOMPARE(goals.size(), 1);
        QCOMPARE(qvariant_cast<QList<double>>(goals.first().first()), target);
        acknowledge(panel);
        panel.setArmState(target, 1.0, 1.0);
        auto external = target;
        external[0] = 0.9;
        panel.setArmState(external, 1.0, 1.0);
        verifyFollows(panel, external);
    }

    void clearReportedStateDoesNotLetOldAckCompleteNewDraft()
    {
        ui::ArmPanel panel;
        prepare(panel);
        joint(panel)->setCommand(0.3);
        send(panel)->click();
        panel.clearReportedState();
        joint(panel)->setCommand(0.8);
        const auto newDraft = draft(panel);
        acknowledge(panel);
        panel.setArmState(newDraft, 1.0, 1.0);
        auto external = newDraft;
        external[0] = 1.1;
        panel.setArmState(external, 1.0, 1.0);
        QCOMPARE(draft(panel), newDraft);
        QVERIFY(!previewText(panel).isEmpty());
    }
};

QTEST_MAIN(TestArmGoalLifecycle)
#include "test_arm_goal_lifecycle.moc"
