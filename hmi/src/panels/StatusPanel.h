#pragma once

// What the robot is doing right now. Statement of work 2.2.7 [5].
//
// The split is by question, not by data source. The top bar answers "is the
// equipment alive" (battery, link), the diagnostics view answers "is anything
// unhealthy" (load, temperature, sensor rates), and this card answers "what is
// the robot doing and where is it". Nothing appears in two of them at once.

#include <QWidget>

class QLabel;

namespace hmi::ui {

class Badge;
class Card;

class StatusPanel : public QWidget {
    Q_OBJECT
public:
    explicit StatusPanel(QWidget *parent = nullptr);

    void setConnected(bool ok);

    /// estop overrides mode: while engaged, the drive mode is not what the
    /// operator needs to see.
    void setMode(const QString &mode, bool estop);

    /// Base movement, in m/s. Shown as words first, number second - "is it
    /// moving" is the question, the speed is the detail.
    void setMotion(double speedMps);

    /// "idle" | "planning" | "executing" | "error", as reported by the arm.
    void setArmState(const QString &state);

    void setTagsSeen(int count);

    /// Map-frame pose. Degrees, so the operator never sees radians.
    void setPose(double x, double y, double thetaDeg);

private:
    /// One "label ....... value" row. Returns the value label to update.
    QLabel *addRow(const QString &label);

    Card *card_ = nullptr;
    Badge *conn_ = nullptr;
    Badge *mode_ = nullptr;
    QLabel *motion_ = nullptr;
    QLabel *arm_ = nullptr;
    QLabel *tag_ = nullptr;
    QLabel *pose_ = nullptr;
};

}  // namespace hmi::ui
