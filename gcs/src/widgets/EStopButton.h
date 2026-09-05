#pragma once

// Emergency stop control and the full-window alert frame.
//
// Statement of work 2.2.7 [5]: always visible, engages on a single click, and
// the whole screen is outlined in red while engaged.
//
// The shape deliberately echoes a physical mushroom-head emergency switch. The
// control has to read as hardware so that the operator does not hesitate.
//
// IMPORTANT: this widget requests a stop, it does not perform one. The
// one-second stopping guarantee comes from the robot-side safety node cutting
// /cmd_vel and calling the SDK2 emergency stop, and the primary means of
// stopping must be a physical or wireless hardware button. See protocol
// section 4.

#include <QWidget>

// NOTE: this must be declared at global scope. Writing `class QPropertyAnimation
// *anim_;` inside the namespace would declare a *new* type
// gcs::ui::QPropertyAnimation instead of referring to Qt's, and the member then
// fails to resolve against the real class in the implementation file.
class QPropertyAnimation;

namespace gcs::ui {

class EStopButton : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double pulse READ pulse WRITE setPulse)
public:
    explicit EStopButton(QWidget *parent = nullptr, int size = 64);

    bool isEngaged() const { return engaged_; }
    void setEngaged(bool engaged);

    double pulse() const { return pulse_; }
    void setPulse(double v);

signals:
    /// Emitted immediately on click; no confirmation is asked for.
    void engageRequested();

    /// Emitted on click while engaged. The caller must obtain explicit operator
    /// confirmation before releasing: automatic release is prohibited.
    void releaseRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    int size_;
    bool engaged_ = false;
    bool hover_ = false;
    double pulse_ = 0.0;
    QPropertyAnimation *anim_ = nullptr;
};

/// Red border drawn around the whole window while the emergency stop is
/// engaged. Mouse events pass through so it never blocks a control.
class AlertFrame : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double pulse READ pulse WRITE setPulse)
public:
    explicit AlertFrame(QWidget *parent = nullptr);

    void setActive(bool active);
    double pulse() const { return pulse_; }
    void setPulse(double v);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    bool active_ = false;
    double pulse_ = 0.0;
    QPropertyAnimation *anim_ = nullptr;
};

}  // namespace gcs::ui
