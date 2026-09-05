#pragma once

// Conversion between the map frame (metres) and QGraphicsScene coordinates
// (pixels).
//
// Coordinate frames
// -----------------
// world : the ROS `map` frame. X right, Y up, theta measured counter-clockwise
//         from +X in radians.
// scene : the Qt scene. X right, Y down. One pixel equals one grid cell, and
//         scene origin (0,0) is the top-left corner of the map image.
//
// An OccupancyGrid origin is the world position of the *bottom-left* corner of
// cell (0,0), while image row 0 is the *top* row: the bridge flips the grid
// vertically when it encodes the PNG (protocol section 2.2). The Y axis is
// therefore inverted:
//
//     sx = (x - ox) / r
//     sy = H - (y - oy) / r          // H = grid height in cells
//
// and back:
//
//     x = ox + sx * r
//     y = oy + (H - sy) * r
//
// Rotation sign
// -------------
// A world heading theta has direction vector (cos t, sin t). Because scene Y
// points down, the same direction appears in the scene as (cos t, -sin t).
// Qt's setRotation(a) maps the local +X axis to (cos a, sin a), so
//     (cos a, sin a) = (cos t, -sin t)  =>  a = -t
// which is what thetaToItemRotation() returns. Do not "fix" this sign: an
// inverted heading still looks plausible on screen, but a goal pose derived
// from it sends the robot the wrong way.

#include <QPointF>
#include <QString>

#include <optional>

namespace gcs::map {

struct MapInfo {
    int width = 0;            ///< grid width in cells
    int height = 0;           ///< grid height in cells
    double resolution = 0.0;  ///< metres per cell
    double originX = 0.0;     ///< world X of the grid's bottom-left corner
    double originY = 0.0;     ///< world Y of the grid's bottom-left corner
    double originTheta = 0.0; ///< must be zero; see create()
    QString mapId;

    /// Validating factory. Returns nullopt on invalid input and, when err is
    /// non-null, stores the reason there.
    ///
    /// A non-zero origin rotation is rejected: protocol v1 does not support
    /// rotated maps, and the bridge is required to absorb the rotation before
    /// publishing.
    static std::optional<MapInfo> create(int width, int height, double resolution,
                                         double originX, double originY,
                                         double originTheta = 0.0,
                                         const QString &mapId = {},
                                         QString *err = nullptr);

    QPointF toScene(double x, double y) const
    {
        return {(x - originX) / resolution, height - (y - originY) / resolution};
    }

    void toWorld(double sx, double sy, double &x, double &y) const
    {
        x = originX + sx * resolution;
        y = originY + (height - sy) * resolution;
    }

    /// Converts a world heading in radians to the argument of
    /// QGraphicsItem::setRotation(), in degrees.
    static double thetaToItemRotation(double theta);
    static double itemRotationToTheta(double deg);

    double sceneWidth() const { return double(width); }
    double sceneHeight() const { return double(height); }
    double metersToPx(double m) const { return m / resolution; }
    double extentXMeters() const { return width * resolution; }
    double extentYMeters() const { return height * resolution; }
};

/// Wraps an angle into (-pi, pi].
///
/// std::atan2 has range [-pi, pi] and can return -pi at the boundary, so the
/// -pi case is folded to +pi to keep the documented half-open range true.
double normalizeAngle(double a);

}  // namespace gcs::map
