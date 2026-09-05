#pragma once

// Synthetic data source for development.
//
// Lets the interface run and be reviewed before the bridge exists. This is a
// development tool, not a deliverable: the release build excludes it and the
// real BridgeClient takes its place.
//
// The generated map is a simplified GTX-A depot: an inspection pit running
// down the middle with walkways either side and a charging station in one
// corner.

#include <QList>
#include <QPointF>
#include <QSet>
#include <QVariantMap>

#include "mapview/MapInfo.h"

namespace gcs::demo {

struct MapData {
    gcs::map::MapInfo info;
    QList<qint8> grid;
};

MapData buildMap();
QList<QVariantMap> buildWaypoints();
QList<QVariantMap> buildTags(const QList<QVariantMap> &waypoints);

/// One frame of simulated telemetry.
struct Frame {
    double x = 0, y = 0, theta = 0;
    QList<QPointF> trail;
    QList<QPointF> plan;
    double soc = 0;
    QList<double> joints;
    double manipulability = 0;
    double sigmaMin = 0;
    QSet<int> seenTags;
    double cpu = 0, mem = 0, cpuTemp = 0, gpuTemp = 0, rtt = 0;
};

/// Drives the synthetic robot. Waypoint statuses are updated in place on the
/// list passed to the constructor.
class Feed {
public:
    explicit Feed(QList<QVariantMap> waypoints);

    Frame step(double dt);
    const QList<QVariantMap> &waypoints() const { return waypoints_; }

private:
    double t_ = 0.0;
    QList<QVariantMap> waypoints_;
    QList<QPointF> trail_;
    double soc_ = 87.0;
};

}  // namespace gcs::demo
