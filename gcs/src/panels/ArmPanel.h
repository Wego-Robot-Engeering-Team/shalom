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
//     makes libfranka trip its reflex. The panel therefore sends a target
//     posture and lets the robot side generate the trajectory.
//   - For a Cartesian goal, whether an IK solution exists is decided on the
//     robot side, which returns the reason when it does not. The FR3 is
//     seven-axis, so it has a null space and can often reconfigure around
//     shoulder, elbow and wrist singularities - but that decision belongs to
//     MoveIt2, not to a control panel.
//
// What the operator needs from this screen is simply to see that the arm is
// straining before it stops moving.

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace gcs::ui {

class ArcGauge;
class Badge;
class Card;
class JointBar;

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
    void buildJointSection();
    void buildEeSection();
    void buildPresetSection();
    void onSliderMoved();
    void syncSlidersToActual();

    Card *card_ = nullptr;
    Badge *state_ = nullptr;
    Badge *singular_ = nullptr;
    ArcGauge *manip_ = nullptr;
    QLabel *sigma_ = nullptr;

    QList<QSlider *> sliders_;
    QList<JointBar *> bars_;
    QHash<QString, QDoubleSpinBox *> ee_;
    QList<QPushButton *> commandButtons_;
    QList<double> actual_;
    bool syncing_ = false;
};

}  // namespace gcs::ui
