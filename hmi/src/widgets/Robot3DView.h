// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Live 3D pose view of the FR3 arm.
//
// Purpose: let the operator see the arm's actual configuration rather than
// reading six numbers. In a train inspection pit the arm works in a confined
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
// The picture deliberately uses Wego-authored primitive geometry: a six-axis
// arm made from tapered cylinders and joint hubs. It is a pose aid, not a CAD viewer, so no vendor
// mesh, URDF mesh, or converted derivative is embedded in the customer HMI.
// The arm still follows robot::jointFrames(), which is the same kinematic
// chain used by the pose readout and command preview.

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

    /// Highlights the arm when it is close to a singular configuration.
    void setSingularWarning(bool warn);

    /// Draws the arm dimmed, for when the pose is stale.
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
    // Frame the arm base and flange without a surrounding mobile platform.
    double azimuth_ = -0.9;    ///< rad
    double elevation_ = 0.23;  ///< rad
    double distance_ = 1.5;    ///< m
    QVector3D target_{0, 0, 0.30};
    QPoint lastMouse_;
};

}  // namespace hmi::ui
