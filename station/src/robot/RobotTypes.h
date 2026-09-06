#pragma once

// Types shared between the control station and whatever is driving the robot.
//
// These are deliberately transport-agnostic: the same structures are produced
// by the built-in simulator and by the real bridge client, so the panels never
// learn which one they are talking to.

#include <QList>
#include <QMetaType>
#include <QPointF>
#include <QSet>
#include <QString>

#include "panels/DiagnosticsPanel.h"

namespace gcs::robot {

enum class DriveMode { Auto, Manual };
enum class MissionState { Idle, Running, Paused };

/// One telemetry snapshot. Everything the interface renders comes from here.
///
/// Assembled rather than streamed per field: the bridge delivers each channel
/// separately, but the panels want a coherent picture, and mixing a fresh pose
/// with a stale arm state produces displays that contradict each other.
struct Telemetry {
    double x = 0, y = 0, theta = 0;
    double speed = 0;                 ///< m/s, magnitude
    QList<QPointF> trail;
    QList<QPointF> plan;
    double soc = 0;
    QList<double> joints;
    double manipulability = 0;
    double sigmaMin = 0;

    /// "idle" | "planning" | "executing" | "error".
    /// Reported by the arm controller rather than inferred here: guessing from
    /// base velocity got it backwards once already, and the operator cannot
    /// tell a wrong badge from a right one.
    QString armState = QStringLiteral("idle");
    QSet<int> seenTags;
    /// Controller load and temperature, in percent and degrees Celsius.
    /// GPU load matters here: inference runs on it, so a pegged GPU explains a
    /// slow capture in a way a busy CPU does not.
    double cpu = 0, gpu = 0, mem = 0, cpuTemp = 0, gpuTemp = 0, rtt = 0;
    bool estop = false;
    QString navStatus;                ///< "idle" | "driving" | "arrived" | "blocked"

    /// False once the link has been quiet long enough that the pose can no
    /// longer be trusted. Everything that acts on position must check this.
    bool poseFresh = false;
    bool localizationOk = true;

    QList<gcs::ui::SensorHealth> sensors;
    gcs::ui::LinkHealth link;
    bool nasOnline = false;
    int pendingUploads = 0;
    double spoolFreeMb = 0;
};

}  // namespace gcs::robot

Q_DECLARE_METATYPE(gcs::robot::Telemetry)
Q_DECLARE_METATYPE(gcs::robot::MissionState)
Q_DECLARE_METATYPE(gcs::robot::DriveMode)
