#pragma once

// One quantity on one row: name, travel, commanded value, measured value, and
// a value box that can be typed into.
//
// It replaces a stacked pair - a read-only bar above a slider - that took two
// rows per joint. Seven joints then needed more height than the column has, so
// the arm screen could not be used without scrolling. Drawing the measured
// value onto the same groove says the same thing in half the space, and puts
// the two numbers the operator is comparing on one line instead of two.
//
// The wheel is deliberately dead. A QSlider changes value on wheel events by
// default, so scrolling a panel that happens to have the pointer over a
// control silently commands the arm somewhere new. Nothing on screen would
// say why.
//
// Values are carried in their native unit - radians for a joint, metres for a
// position - and only converted for display, so no caller has to remember
// which unit the widget speaks.

#include <QSlider>

class QLineEdit;

namespace hmi::ui {

class ValueSlider : public QSlider {
    Q_OBJECT
public:
    /// lo and hi are the travel limits in the native unit. displayScale
    /// converts native to what is printed (180/pi for radians shown in
    /// degrees, 1 for metres shown as metres).
    ValueSlider(const QString &name, double lo, double hi, const QString &unit,
                int decimals, double displayScale, QWidget *parent = nullptr);

    /// The value the robot reports. Drawn as a hollow ring on the groove.
    void setActual(double v);
    /// Forgets the measured value, for a quantity the robot does not report.
    void clearActual();

    double command() const;
    /// The comparison value: what the robot reports, or what was last sent.
    double actual() const { return actual_; }
    void setCommand(double v);

    /// Marks a quantity where the two ends of the range are the same value -
    /// an angle over a full turn. Without it, a pose sitting on the seam reads
    /// as the widest possible disagreement between commanded and measured when
    /// the two are in fact identical.
    void setCyclic(bool on) { cyclic_ = on; }

    /// True while the commanded and measured values differ enough to matter -
    /// which is exactly "the operator has dialled something in and not sent
    /// it yet", or "the arm has not got there".
    bool diverged() const;

signals:
    /// Emitted when the operator commits a typed value.
    void valueTyped();

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *) override;

    /// Dragging is handled here rather than by QSlider.
    ///
    /// The row is painted with a name column and a value column, so the groove
    /// is not where the style thinks it is. Leaving hit-testing to the base
    /// class meant clicking the handle did nothing and dragging jumped: the
    /// drawn control and the live one were in different places.
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    double trackLeft() const;
    double trackWidth() const;
    QRect valueRect() const;
    void setFromX(double x);

    /// Turns the value column into a text box. Dragging a slider to an exact
    /// number is guesswork; teaching a point wants the number itself.
    void beginEdit();
    void commitEdit();

    QString name_;
    double lo_;
    double hi_;
    QString unit_;
    int decimals_;
    double scale_;
    double actual_ = 0.0;
    bool hasActual_ = false;
    bool cyclic_ = false;
    QLineEdit *editor_ = nullptr;
};

}  // namespace hmi::ui
