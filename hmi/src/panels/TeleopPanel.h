// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#pragma once

// Manual jog panel. Statement of work 2.2.7 [2] item 2.
//
// Direction buttons plus linear and angular speed sliders, publishing
// cmd/cmd_vel at 20 Hz.
//
// Safety design:
//   - Publishing happens only while a button is held. These are momentary
//     controls, not toggles. That holds in autonomous mode too: the operator
//     takes over for as long as they hold a control and the robot returns to
//     the mission when they let go.
//   - When publishing stops the bridge latches zero after 300 ms, so a frozen
//     control station or a dropped link cannot leave the robot driving.
//   - The controls are disabled while the link is down or the emergency stop
//     is engaged. Drive mode does not gate them - what is safe to do is the
//     robot's ruling, and it arbitrates manual against autonomous itself.
//   - The keyboard drives the same press/release path as the buttons, so the
//     hold-to-move rule cannot be bypassed by using keys instead. Auto-repeat
//     is ignored: without that, the platform's repeat stream looks like a
//     rapid press/release cycle and the robot stutters.

#include <QHash>
#include <QWidget>

class QKeyEvent;
class QLabel;
class QPushButton;
class QSlider;
class QTimer;

namespace hmi::ui {

class Card;

class TeleopPanel : public QWidget {
    Q_OBJECT
public:
    explicit TeleopPanel(QWidget *parent = nullptr);

    /// Enables the jog controls. The stop button stays live either way.
    void setJogEnabled(bool on);

    /// Shows the posture the robot reported. The UI does not guess: pressing
    /// a button changes nothing until state/base comes back.
    void setBasePosture(const QString &posture);

protected:
    /// Watches the whole window so the operator does not have to click the
    /// panel first. Keys are ignored while a text field has focus, otherwise
    /// typing a vehicle number would drive the robot.
    bool eventFilter(QObject *watched, QEvent *ev) override;

signals:
    void cmdVel(double vx, double vy, double wz);

    /// Base posture request. confirm is true for damp.
    void basePosture(const QString &posture, bool confirm);

private:
    QWidget *buildPad();

    /// The posture button row. Sit and stand go straight out; damp goes
    /// through a confirmation dialog first.
    QWidget *buildPostureRow();

    QHash<QString, QPushButton *> postureButtons_;
    QLabel *postureLabel_ = nullptr;

    /// Direction key for this event, or an empty string if it is not one.
    static QString keyFor(const QKeyEvent *ev);
    /// True while a text field, spin box or editable combo has focus.
    static bool typingSomewhere();


    /// Adds a labelled speed slider. The slider always works in SI units;
    /// dispScale and decimals only change how the number is written, so that
    /// rotation can be shown in degrees without radians leaking into the
    /// command path.
    QSlider *addSpeedRow(const QString &label, double vmax, double def,
                         const QString &unit, double caution,
                         double dispScale = 1.0, int decimals = 2);
    void press(const QString &key);
    void release();
    void publish();

    Card *card_ = nullptr;
    QSlider *linear_ = nullptr;
    QSlider *angular_ = nullptr;
    QHash<QString, QPushButton *> buttons_;
    QTimer *timer_ = nullptr;

    double vx_ = 0.0;
    double vy_ = 0.0;
    double wz_ = 0.0;
    bool enabled_ = false;
    /// Direction key currently held, so a release for a stale key is ignored.
    QString heldKey_;
};

}  // namespace hmi::ui
