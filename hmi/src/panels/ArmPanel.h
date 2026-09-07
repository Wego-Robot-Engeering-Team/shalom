#pragma once

// FR3 arm control panel. Statement of work 2.2.7 [3].
//
//   (1) end-effector target pose entry plus MoveIt2 execution
//   (2) live joint angles with per-joint manual control
//   (3) named postures (home, standby, stow)
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

#include "robot/PoseCheck.h"

class QLabel;
class QPushButton;
class QTabWidget;

namespace hmi::ui {

class Badge;
class Card;
class ValueSlider;
class Robot3DView;

class ArmPanel : public QWidget {
    Q_OBJECT
public:
    explicit ArmPanel(QWidget *parent = nullptr);

    void setArmState(const QList<double> &positions, double manipulability,
                     double sigmaMin, const QString &moveitState = QStringLiteral("idle"));
    void setControlsEnabled(bool on);

    /// Moves the sliders to a named posture without commanding the arm; the
    /// operator still has to press send. Loading and sending in one step would
    /// make a mis-click move the arm.
    void applyPresetToSliders(const QString &name);


signals:
    void jointGoal(const QList<double> &positions);
    void eeGoal(const QVariantMap &pose);
    void presetRequested(const QString &name);
    void stopRequested();

private:
    void build3DSection();
    void buildPresetSection();
    /// The joint and end-effector controls, on tabs.
    ///
    /// Both were stacked before, which alone made the column taller than any
    /// screen. They are also alternatives - a pose is commanded one way or the
    /// other - so showing both at once buys nothing.
    void buildCommandTabs();
    QWidget *buildJointTab();
    QWidget *buildEeTab();
    void onSliderMoved();
    /// Pushes the un-sent slider pose into the 3D view as a ghost. Display
    /// only - the arm is commanded by the send button, never by dragging.
    void refreshPreview();

    /// Keeps the two tabs describing the same target.
    ///
    /// Joint edits run forward kinematics into the pose fields; pose edits run
    /// inverse kinematics back into the joints, seeded from where the arm is.
    /// The robot still plans the move - this only stops the operator reading a
    /// number that is not what will be sent.
    void syncEeFromJoints();
    void syncJointsFromEe();

    /// The same for the measured side. The robot reports joints, not a pose,
    /// but the pose follows from the joints - so both tabs can show where the
    /// arm actually is instead of one of them showing the last thing sent.
    void syncEeActualFromJoints(const QList<double> &joints);

    /// Updates the warning badge, but only when what it says has changed.
    void showPoseWarning(const robot::PoseWarning &warning);
    void syncSlidersToActual();

    Card *card_ = nullptr;
    Badge *state_ = nullptr;
    Robot3DView *view3d_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QLabel *advice_ = nullptr;
    QLabel *poseWarning_ = nullptr;

    QList<ValueSlider *> sliders_;
    QHash<QString, ValueSlider *> ee_;
    QList<QPushButton *> commandButtons_;
    QList<double> actual_;

    /// Whether the robot has ever reported a pose. The command sliders snap to
    /// the first report: until then they sit on defaults, and showing that as
    /// an unsent edit would mean the screen opens claiming work in progress
    /// that nobody asked for.
    bool hadArmState_ = false;
    bool syncing_ = false;
    /// False while the pose fields name a place the arm cannot reach.
    bool eeReachable_ = true;
    robot::PoseWarning lastWarning_;
};

}  // namespace hmi::ui
