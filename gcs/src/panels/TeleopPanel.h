#pragma once

// Manual jog panel. Statement of work 2.2.7 [2] item 2.
//
// Direction buttons plus linear and angular speed sliders, publishing
// cmd/cmd_vel at 20 Hz.
//
// Safety design:
//   - Publishing happens only while a button is held. These are momentary
//     controls, not toggles.
//   - When publishing stops the bridge latches zero after 300 ms, so a frozen
//     control station or a dropped link cannot leave the robot driving.
//   - The controls are disabled unless the system is in manual mode.

#include <QHash>
#include <QWidget>

class QPushButton;
class QSlider;
class QTimer;

namespace gcs::ui {

class Card;

class TeleopPanel : public QWidget {
    Q_OBJECT
public:
    explicit TeleopPanel(QWidget *parent = nullptr);

    /// Enables the jog controls. The stop button stays live either way.
    void setJogEnabled(bool on);

signals:
    void cmdVel(double vx, double vy, double wz);

private:
    QWidget *buildPad();
    QSlider *addSpeedRow(const QString &label, double vmax, double def,
                         const QString &unit, double caution);
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
};

}  // namespace gcs::ui
