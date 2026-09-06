#pragma once

// Reads a map in the ROS map_server format: a PGM image plus a YAML sidecar.
//
// This is the format SLAM Toolbox writes, so a map produced on site can be
// dropped straight into the control station without a conversion step. The
// alternative - only ever accepting the map over the bridge - means the
// operator cannot look at a site before the robot is on the network.
//
// Only the keys map_server itself defines are read. A general YAML parser is
// deliberately not pulled in: the file is six flat keys, and an extra
// dependency has to be justified to the escrow inventory and rebuilt in an
// air-gapped environment.

#include <QList>
#include <QString>
#include <optional>

#include "mapview/MapInfo.h"

namespace gcs::map {

/// A map ready to hand to MapView.
///
/// grid is in image order: index 0 is the top-left cell, which is the highest
/// world Y. That matches both the PGM row order and what occupancyToImage
/// expects, so nothing is flipped on the way through.
struct LoadedMap {
    MapInfo info;
    QList<qint8> grid;   ///< -1 unknown, 0 free, 100 occupied
};

/// Loads the map named by a map_server YAML file. The image path inside it is
/// resolved relative to the YAML's own directory, as map_server does.
///
/// Returns nullopt on any problem and, when err is non-null, puts an
/// operator-readable reason there.
std::optional<LoadedMap> loadRosMap(const QString &yamlPath, QString *err = nullptr);

/// Parsed contents of the YAML sidecar. Exposed for testing; loadRosMap is
/// what callers want.
struct MapMeta {
    QString image;
    double resolution = 0.0;
    double originX = 0.0;
    double originY = 0.0;
    double originTheta = 0.0;
    bool negate = false;
    double occupiedThresh = 0.65;
    double freeThresh = 0.196;
};

std::optional<MapMeta> parseMapYaml(const QString &text, QString *err = nullptr);

}  // namespace gcs::map
