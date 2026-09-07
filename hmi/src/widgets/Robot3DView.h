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
// Rendered in software with a depth buffer, not OpenGL. The reason is not
// aesthetic:
//
//   - the delivered machine is an industrial Windows PC whose GPU drivers are
//     an unknown, and a control station must not fail to draw because of one;
//   - QPainter output is captured by QWidget::grab(), so screenshots taken for
//     on-site support actually contain the view;
//   - it needs no shader pipeline and no extra Qt module.
//
// This used to sort faces back to front and let QPainter fill them - the
// painter's algorithm. That cannot draw geometry that interpenetrates, so
// every link had to be reduced to its convex hull, and the robot on screen was
// a set of blocks rather than the machine. Testing depth per pixel removes the
// constraint, so the view now draws the vendor meshes themselves
// (widgets/RobotMesh.h), reduced only enough to keep the resource small.
//
// It is also no slower: about 7.6 ms a frame at 640x480 in the delivery build,
// against 7.9 ms for the blocks, because nothing is sorted any more.
//
// The arm geometry comes from FAIRINO's own URDF by way of
// robot::jointOrigins(), so the joint origins are in the right places even
// though the links are drawn as simple shapes.

#include <QVector3D>
#include <QWidget>

namespace hmi::ui {

class Robot3DView : public QWidget {
    Q_OBJECT
public:
    explicit Robot3DView(QWidget *parent = nullptr);

    /// Six arm joint angles in radians, as the arm reports them.
    void setArmJoints(const QList<double> &q);

    /// Six angles the operator is dialling in but has not sent yet.
    ///
    /// The arm is drawn at these angles instead of the reported ones, so the
    /// pose can be checked before committing to it, and the view says it is a
    /// preview. Overlaying a second translucent arm was tried first and read
    /// as two robots. Passing an empty list goes back to the reported pose.
    ///
    /// This never leaves the screen: the robot only moves when the operator
    /// presses send.
    void setPreviewJoints(const QList<double> &q);

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
    void enterEvent(QEnterEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    /// Angles the arm reports, and the un-sent pose being dialled in.
    QList<double> joints_;
    QList<double> preview_;

    /// What is actually drawn. It eases toward the target instead of jumping.
    ///
    /// A preset click sets six angles at once; snapping there reads as a
    /// glitch rather than a movement, and gives no sense of the path the arm
    /// will take. Easing costs one timer and makes the change legible.
    QList<double> shown_;
    QTimer *ease_ = nullptr;
    bool hovered_ = false;
    bool singularWarn_ = false;
    bool stale_ = false;

    // orbit camera
    //
    // The camera looks at target_, which the operator can slide sideways. With
    // the pivot pinned to the base, zooming in on the gripper was impossible:
    // the interesting end of the arm swung off screen as soon as it reached.
    // Framed for the real robot, not the block figure that stood here before:
    // B2 is 1.1 m long and stands 0.54 m at the body, and the arm reaches
    // another 0.6 m above that when it is up. Aiming at the deck and pulling
    // back to 3.2 m keeps both the feet and a raised gripper on screen.
    double azimuth_ = -0.9;    ///< rad
    double elevation_ = 0.30;  ///< rad
    double distance_ = 3.2;    ///< m
    QVector3D target_{0, 0, 0.75};
    QPoint lastMouse_;
};

}  // namespace hmi::ui
