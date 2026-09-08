#pragma once

// Graphics items overlaid on the map.
//
// Every item is positioned in scene coordinates (one pixel per grid cell) but
// draws itself at a constant on-screen size via ItemIgnoresTransformations.
// Without that, zooming out shrinks the robot marker to a dot and the operator
// loses track of it.

#include <QGraphicsItem>

namespace hmi::map {

/// Color for a waypoint status, per statement of work 2.2.7 [1] item 2.
/// Resolved at paint time so a theme switch is picked up immediately.
QString waypointColor(const QString &status);

/// Robot pose marker: body, heading indicator, direction arrow.
class RobotMarker : public QGraphicsItem {
public:
    explicit RobotMarker(double radius = 9.0);

    /// Dims the marker when the pose is stale (link lost). The operator must
    /// be able to tell "the robot is here" from "the robot was here".
    void setStale(bool stale);

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    double r_;
    bool stale_ = false;
};

/// Inspection waypoint: status-colored disc with its sequence number.
class WaypointMarker : public QGraphicsItem {
public:
    WaypointMarker(int index, const QString &id, const QString &status = QStringLiteral("todo"),
                   double radius = 10.0);

    void setStatus(const QString &status);
    QString status() const { return status_; }

    /// Drawn with a ring when the operator has this row picked in the list.
    /// Kept separate from status: a point can be selected in any state, and
    /// borrowing the "current" color would lie about what the robot is doing.
    void setSelected(bool selected);
    QString waypointId() const { return id_; }

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override;

private:
    int index_;
    QString id_;
    QString status_;
    double r_;
    bool hover_ = false;
    bool selected_ = false;
};

/// AprilTag marker position, filled while the tag is currently detected.
class AprilTagMarker : public QGraphicsItem {
public:
    explicit AprilTagMarker(int tagId, double size = 9.0);

    void setSeen(bool seen);

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    int id_;
    double s_;
    bool seen_ = false;
};

/// Charging station or start position.
///
/// Neither is part of the inspection sequence. Drawing them as waypoint discs
/// would put two unnumbered dots among the numbered ones, and the operator
/// would have to work out which is which every time. They get their own
/// silhouettes instead - a roof with a bolt, and a paw print - so they are
/// told apart at a glance.
class StationMarker : public QGraphicsItem {
public:
    enum class Kind { Dock, Home };

    explicit StationMarker(Kind kind, double size = 11.0);

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    Kind kind_;
    double s_;
};

/// Goal pose selected by clicking the map, including the target heading.
class GoalMarker : public QGraphicsItem {
public:
    explicit GoalMarker(double radius = 9.0);

    QRectF boundingRect() const override;
    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override;

private:
    double r_;
};

}  // namespace hmi::map
