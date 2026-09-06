#pragma once

// One captured inspection image and its record.
//
// The statement of work requires the control station to browse and download
// previously captured images and results over the internal network. The images
// themselves are written by the robot straight to the NAS, so this side only
// reads: it walks the share, parses the file names and the sidecar metadata,
// and presents them.
//
// Reading rather than owning matters. If this application were the thing that
// filed the evidence, an operator's laptop being off would mean an inspection
// that produced nothing.

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace hmi::data {

struct InspectionRecord {
    QString filePath;      ///< absolute path on the mounted share
    QString fileName;
    qint64 fileSize = 0;

    QString vehicleNumber; ///< train number and car number joined
    QString carNumber;
    QString pointId;
    QDateTime capturedAt;
    int tagId = -1;

    /// The sidecar metadata, when one was found next to the image. Absent
    /// means the record was reconstructed from the file name alone.
    std::optional<QJsonObject> sidecar;

    bool hasSidecar() const { return sidecar.has_value(); }

    /// True when everything the statement of work requires is present. A
    /// record missing fields is still listed - hiding it would make a gap in
    /// the evidence invisible - but it is marked.
    bool isComplete() const;
    QStringList missingFields() const;
};

/// Reconstructs a record from a file name in the mandated form
/// `<vehicle>_<car>_<point id>,<timestamp>.<ext>`.
///
/// Returns nullopt when the name does not match. Files that do not match are
/// reported separately rather than skipped: a stray name usually means
/// something wrote to the share that should not have.
std::optional<InspectionRecord> parseFileName(const QString &fileName);

/// Result of scanning a directory.
struct ScanResult {
    QList<InspectionRecord> records;

    /// Files that are not recognisable inspection images. Surfaced so an
    /// operator can see that the share contains something unexpected.
    QStringList unrecognised;

    QString error;   ///< non-empty when the directory could not be read
};

/// Walks `directory` recursively for inspection images.
ScanResult scanDirectory(const QString &directory, int maxFiles = 20000);

}  // namespace hmi::data
