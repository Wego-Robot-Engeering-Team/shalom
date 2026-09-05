#pragma once

// OccupancyGrid to QImage conversion.
//
// The bridge sends the map as a PNG (protocol section 2.2), but a raw grid
// still has to be colored when loading a local map file or running against
// simulated data, so the coloring lives here rather than at the call site.
//
// ROS OccupancyGrid values: -1 unknown, 0 free, 100 fully occupied.

#include <QImage>
#include <QList>

namespace gcs::map {

/// grid is row-major with row 0 at the *top* of the image, matching what the
/// bridge encodes. Returns an image that owns its pixels.
QImage occupancyToImage(const QList<qint8> &grid, int width, int height,
                        int occupiedThreshold = 65);

}  // namespace gcs::map
