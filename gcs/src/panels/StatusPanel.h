#pragma once

// Status monitoring panel. Statement of work 2.2.7 [5].
//
// Battery level, CPU/GPU temperature, link state, drive mode and AprilTag
// detection, all refreshed well inside the three-second requirement.

#include <QWidget>

class QLabel;

namespace gcs::ui {

class Badge;
class BatteryRing;
class Card;
class StatBar;

class StatusPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatusPanel(QWidget *parent = nullptr);

    void setConnected(bool ok);
    void setBattery(double socPercent, bool charging = false);

    /// estop overrides mode: while engaged, the drive mode is not what the
    /// operator needs to see.
    void setMode(const QString &mode, bool estop);

    void setSystem(double cpu, double mem, double cpuTemp, double gpuTemp, double rttMs);
    void setPose(double x, double y, double thetaDeg);
    void setTagsSeen(int count);

private:
    Card *card_ = nullptr;
    Badge *conn_ = nullptr;
    Badge *mode_ = nullptr;
    Badge *tag_ = nullptr;
    BatteryRing *battery_ = nullptr;
    StatBar *cpu_ = nullptr;
    StatBar *mem_ = nullptr;
    StatBar *cpuTemp_ = nullptr;
    StatBar *gpuTemp_ = nullptr;
    StatBar *rtt_ = nullptr;
    QLabel *pose_ = nullptr;
};

}  // namespace gcs::ui
