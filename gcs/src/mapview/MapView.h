#pragma once

// 2D SLAM map view.
//
// Covers statement of work 2.2.7 [1]:
//   (1) live base_link position and heading, drawn as an icon plus arrow
//   (2) waypoint and AprilTag overlays, colored by status
//   (3) Nav2 planned path (solid blue) and travelled path (dashed grey),
//       with pan and zoom
//   (4) click the map to choose a goal
//
// Goal selection mirrors RViz's 2D Nav Goal: press and drag to set position
// and heading in one gesture. A click without a drag leaves the heading at
// zero rather than guessing.

#include <QGraphicsView>

#include "mapview/MapInfo.h"

class QGraphicsPathItem;
class QGraphicsPixmapItem;

class QGraphicsScene;

namespace gcs::map {

class AprilTagMarker;
class GoalMarker;
class RobotMarker;
class WaypointMarker;

/// Interaction mode for the left mouse button. Panning is always available on
/// the middle and right buttons regardless of mode.
enum class MapMode { View, SetGoal, AddWaypoint };

class MapView : public QGraphicsView {
    Q_OBJECT
public:
    explicit MapView(QWidget *parent = nullptr);
    ~MapView() override;

    void setMap(const MapInfo &info, const QImage &image);
    const MapInfo *mapInfo() const;
    void fitMap();

    void setMode(MapMode mode);
    MapMode mode() const { return mode_; }

    void setRobotPose(double x, double y, double theta, bool stale = false);
    void setPlan(const QList<QPointF> &worldPoints);
    void setTrail(const QList<QPointF> &worldPoints);

    /// Replaces the whole overlay. Each entry needs id, x, y and status.
    void setWaypoints(const QList<QVariantMap> &waypoints);
    void setWaypointStatus(const QString &id, const QString &status);

    void setTags(const QList<QVariantMap> &tags);
    void setTagsSeen(const QSet<int> &seenIds);

    void setGoal(double x, double y, double theta);
    void clearGoal();

    /// Re-applies theme colors to scene items. Pens are stored on the items,
    /// so unlike stylesheet-driven widgets they do not follow a theme switch
    /// on their own.
    void retheme();

signals:
    void goalRequested(double x, double y, double theta);
    void waypointPlaced(double x, double y, double theta);
    void waypointClicked(const QString &id);
    void cursorMoved(double x, double y);

protected:
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void drawForeground(QPainter *p, const QRectF &rect) override;

private:
    QPainterPath pathFromWorld(const QList<QPointF> &pts) const;
    void drawScaleBar(QPainter *p);

    static constexpr double kMinScale = 0.15;
    static constexpr double kMaxScale = 24.0;
    /// Drag shorter than this many pixels counts as "no heading given".
    static constexpr double kHeadingDragThreshold = 8.0;

    QGraphicsScene *scene_ = nullptr;
    QGraphicsPixmapItem *mapItem_ = nullptr;
    QGraphicsPathItem *trailItem_ = nullptr;
    QGraphicsPathItem *planItem_ = nullptr;
    RobotMarker *robot_ = nullptr;
    GoalMarker *goal_ = nullptr;
    QHash<QString, WaypointMarker *> waypoints_;
    QHash<int, AprilTagMarker *> tags_;

    std::optional<MapInfo> info_;
    MapMode mode_ = MapMode::View;

    bool dragging_ = false;
    QPointF dragOrigin_;
    QPointF dragCurrent_;
    bool panning_ = false;
    QPointF panAnchor_;
};

}  // namespace gcs::map
