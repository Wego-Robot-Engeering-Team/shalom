#pragma once

// One joint on one row: name, travel, commanded angle, measured angle, value.
//
// It replaces a stacked pair - a read-only bar above a slider - that took two
// rows per joint. Seven joints then needed more height than the column has, so
// the arm screen could not be used without scrolling. Drawing the measured
// angle onto the slider's own groove says the same thing in half the space,
// and puts the two numbers the operator is comparing on the same line instead
// of one above the other.
//
// The wheel is deliberately dead. A QSlider changes value on wheel events by
// default, so scrolling a panel that happens to have the pointer over a joint
// silently commands the arm to a new angle. Nothing on screen would say why.

#include <QSlider>

namespace gcs::ui {

class JointSlider : public QSlider {
    Q_OBJECT
public:
    /// lo and hi are the travel limits in radians.
    JointSlider(const QString &name, double lo, double hi, QWidget *parent = nullptr);

    /// The angle the arm reports, in radians. Drawn as a tick on the groove.
    void setActual(double rad);

    double command() const;
    void setCommand(double rad);

    /// True while the commanded and measured angles differ enough to be worth
    /// showing - a planning failure or a controller reflex looks like this.
    bool diverged() const;

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

private:
    /// Left edge and width of the painted groove, in widget coordinates.
    double trackLeft() const;
    double trackWidth() const;
    /// Value under this x position, clamped to the travel limits.
    void setFromX(double x);

    QString name_;
    double lo_;
    double hi_;
    double actual_ = 0.0;
    bool hasActual_ = false;
};

}  // namespace gcs::ui
