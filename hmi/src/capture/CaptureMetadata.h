#pragma once

// Metadata attached to every captured image.
//
// The statement of work (section 1, capture data) fixes both the required
// fields and the file name, and both are acceptance items:
//
//   required : vehicle number (train number + car number), inspection point id,
//              AprilTag id, capture time as YYYY-MM-DD HH:MM:SS, robot pose
//              (x, y, theta) and distance to the subject in millimetres
//   file name: <vehicle>_<car>_<point id>,<timestamp>.<extension>
//
// The contract states these in Korean; the exact wording is in the statement of
// work and the Korean labels the operator sees are in the panel, not here.
//
// Kept as plain logic with no UI so that the rules can be tested directly.
// A capture saved with an incomplete record is one that may be excluded at
// inspection, and the robot has to go back under the train to retake it.

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace hmi::capture {

/// Fields the operator types, plus the ones filled in from telemetry.
struct CaptureMetadata {
    // ---- entered by the operator ----
    QString trainNumber;   ///< train set number
    QString carNumber;     ///< car number within the train
    QString pointId;       ///< inspection point id

    // ---- filled in from telemetry ----
    int tagId = -1;              ///< -1 when no AprilTag was in view
    QDateTime capturedAt;
    double robotX = 0.0;
    double robotY = 0.0;
    double robotTheta = 0.0;     ///< radians
    double distanceMm = 0.0;     ///< distance to the subject

    /// Combined vehicle number: train number, a dot, then car number.
    QString vehicleNumber() const;

    /// Names of the required fields that are still missing. Empty means the
    /// record is complete.
    QStringList missingFields() const;
    bool isComplete() const { return missingFields().isEmpty(); }

    /// File name in the mandated form, with `extension` appended.
    /// Returns an empty string when required fields are missing, so that an
    /// incomplete record cannot quietly produce a plausible-looking file.
    QString fileName(const QString &extension) const;

    /// The record as it is written alongside the image.
    QJsonObject toJson() const;

    /// Timestamp in the mandated display format.
    QString capturedAtText() const;
};

/// Characters that cannot appear in a file name on Windows or Linux, replaced
/// rather than rejected: an operator typing a slash in a car number should get
/// a usable file, not an error they cannot interpret.
QString sanitiseForFileName(const QString &value);

}  // namespace hmi::capture
