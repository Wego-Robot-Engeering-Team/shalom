#pragma once

// Live 3D pose view of the B2 and the FR3 arm.
//
// Purpose: let the operator see the arm's actual configuration rather than
// reading seven numbers. In a train inspection pit the arm works in a confined
// space, and "the elbow is about to swing into the underbody" is obvious in a
// picture and invisible in a table of angles. It also lets a teach pose be
// checked before it is saved.
//
// RENDERING APPROACH
// ------------------
// Rendered with QPainter using the painter's algorithm (faces sorted back to
// front), not OpenGL. The reasons are practical rather than aesthetic:
//
//   - the delivered machine is an industrial Windows PC whose GPU drivers are
//     an unknown, and a control station must not fail to draw because of one;
//   - QPainter output is captured by QWidget::grab(), so screenshots taken for
//     on-site support actually contain the view;
//   - it needs no shader pipeline and no extra Qt module.
//
// The cost is that it does not scale past a few hundred faces and cannot do
// intersecting geometry correctly. That is acceptable for primitive shapes.
// When the real meshes from franka_description and the Unitree package are
// available, this should be replaced with an OpenGL implementation - the
// kinematics below stay as they are.
//
// The arm geometry uses the published modified Denavit-Hartenberg parameters
// for the FR3, so the joint origins are in the right places even though the
// links are drawn as simple shapes.

#include <QVector3D>
#include <QWidget>

namespace gcs::ui {

class Robot3DView : public QWidget {
    Q_OBJECT
public:
    explicit Robot3DView(QWidget *parent = nullptr);

    /// Seven arm joint angles in radians.
    void setArmJoints(const QList<double> &q);

    /// Highlights the arm when it is close to a singular configuration, using
    /// the same threshold as the manipulability gauge.
    void setSingularWarning(bool warn);

    /// Draws the whole robot dimmed, for when the pose is stale.
    void setStale(bool stale);

    void resetCamera();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    /// Forward kinematics: origin of each joint frame plus the flange, in the
    /// arm base frame.
    QList<QVector3D> jointOrigins() const;

    QList<double> joints_;
    bool singularWarn_ = false;
    bool stale_ = false;

    // orbit camera
    double azimuth_ = -0.9;    ///< rad
    double elevation_ = 0.42;  ///< rad
    double distance_ = 2.4;    ///< m
    QPoint lastMouse_;
};

}  // namespace gcs::ui
