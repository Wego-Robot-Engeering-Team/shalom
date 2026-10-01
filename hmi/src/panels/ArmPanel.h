// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// FR3 arm control panel. Statement of work 2.2.7 [3].
//
//   (1) end-effector target pose entry plus MoveIt2 execution
//   (2) live joint angles with per-joint manual control
//   (3) operator-defined named postures
//
// How singularities are handled
// -----------------------------
// The panel *displays* the manipulability index; detecting and avoiding
// singular configurations is the robot-side arm node's job (protocol
// section 4). The split matters:
//
//   - Joint-space motion from the sliders is not affected by singularities at
//     all. What is actually dangerous there are the FR3 position, velocity,
//     acceleration and jerk limits: feeding raw slider values straight through
//     trips the controller's own limit check. The panel therefore sends a
//     target posture and lets the robot side generate the trajectory.
//   - For a Cartesian goal, whether an IK solution exists is decided on the
//     robot side, which returns the reason when it does not. This FR3 is
//     six-axis, so it has no null space to reconfigure through: at a wrist or
//     elbow singularity a direction of motion is simply gone, and no amount of
//     re-planning recovers it. That makes the pre-flight warning in
//     robot/PoseCheck.cpp worth more here than it would be on a seven-axis
//     arm - but the decision still belongs to MoveIt2, not to a control panel.
//
// What the operator needs from this screen is simply to see that the arm is
// straining before it stops moving.

#include <QWidget>
#include <QHash>

#include "robot/PoseCheck.h"

class QLabel;
class QPushButton;
class QTabWidget;
class QListWidget;
class QVBoxLayout;

namespace hmi::ui {

class Badge;
class Card;
class ValueSlider;
class Robot3DView;
class CatalogRow;

class ArmPanel : public QWidget {
    Q_OBJECT
public:
    explicit ArmPanel(QWidget *parent = nullptr);

    void setArmState(const QList<double> &positions, double manipulability,
                     double sigmaMin, const QString &moveitState = QStringLiteral("idle"));
    void setControlsEnabled(bool on);
    void setExecutionAvailable(bool available);
    void setFeedbackFresh(bool fresh);
    void clearReportedState();
    void setCommandResult(const QString &channel, bool ok, const QString &code,
                          const QString &message);

    void setPosePresets(const QList<QVariantMap> &presets);

signals:
    void jointGoal(const QList<double> &positions);
    void eeGoal(const QVariantMap &pose);
    void stopRequested();
    void savePosePresetRequested(const QVariantMap &preset);
    void updatePosePresetRequested(const QVariantMap &preset, quint64 expectedRevision);
    void archivePosePresetRequested(const QString &id, quint64 expectedRevision);

private:
    void build3DSection();
    QWidget *buildPoseManagementTab();
    void rebuildPoseList();
    void updatePoseRows();
    void previewSavedPose(const QString &id);
    void applySavedPose(const QString &id);
    void savePose(bool measured);
    /// Keep the two command methods on separate tabs without changing the draft.
    void buildCommandTabs();
    QWidget *buildJointTab();
    QWidget *buildEeTab();
    void onSliderMoved();
    /// Pushes the un-sent slider pose into the 3D view as a ghost. Display
    /// only - the arm is commanded by the send button, never by dragging.
    void refreshPreview();

    /// Keeps the two editors describing the same target.
    ///
    /// Joint edits run forward kinematics into the pose fields; pose edits run
    /// inverse kinematics back into the joints, seeded from where the arm is.
    /// Both editors send the same joint target through the robot's safety gate.
    void syncEeFromJoints();
    void syncJointsFromEe();

    /// The same for the measured side. The robot reports joints, not a pose,
    /// but the pose follows from the joints - so both editors can show where the
    /// arm actually is instead of one of them showing the last thing sent.
    void syncEeActualFromJoints(const QList<double> &joints);

    /// Updates the warning badge, but only when what it says has changed.
    void showPoseWarning(const robot::PoseWarning &warning);
    void syncSlidersToActual();

    Card *card_ = nullptr;
    Badge *state_ = nullptr;
    Robot3DView *view3d_ = nullptr;
    QTabWidget *commandTabs_ = nullptr;
    QTabWidget *sectionTabs_ = nullptr;
    QLabel *advice_ = nullptr;
    QLabel *poseWarning_ = nullptr;
    QLabel *previewStatus_ = nullptr;
    QLabel *commandStatus_ = nullptr;
    QVBoxLayout *controlsLayout_ = nullptr;
    QListWidget *savedPresets_ = nullptr;
    QPushButton *savePose_ = nullptr;
    QPushButton *savePreviewPose_ = nullptr;
    QPushButton *jointSend_ = nullptr;
    QPushButton *eeSend_ = nullptr;

    QList<ValueSlider *> sliders_;
    QHash<QString, ValueSlider *> ee_;
    QList<QPushButton *> commandButtons_;
    QList<double> actual_;
    QList<QVariantMap> posePresets_;
    QVariantMap pendingPose_;
    QString pendingPoseUpdateId_;
    QHash<QString, CatalogRow *> poseRows_;
    QHash<QString, QVariantMap> poseDrafts_;
    QString pendingPoseChannel_;
    bool feedbackFresh_ = false;

    /// Whether the robot has ever reported a pose. The command sliders snap to
    /// the first report: until then they sit on defaults, and showing that as
    /// an unsent edit would mean the screen opens claiming work in progress
    /// that nobody asked for.
    bool hadArmState_ = false;
    /// True after the operator changes either command editor. This is separate
    /// from ValueSlider::diverged(): before the first telemetry arrives there
    /// is no measured value to diverge from, but the 3D preview must still
    /// show an operator's edit.
    bool commandEdited_ = false;
    bool syncing_ = false;
    /// False while the pose fields name a place the arm cannot reach.
    bool eeReachable_ = true;
    bool hasPendingGoal_ = false;
    bool controlsEnabled_ = false;
    bool executionAvailable_ = false;
    void refreshCommandControls();
    robot::PoseWarning lastWarning_;
};

}  // namespace hmi::ui
