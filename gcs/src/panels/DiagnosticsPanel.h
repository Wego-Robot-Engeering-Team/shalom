#pragma once

// Sensor and link health panel.
//
// Answers one question: is the robot's perception and its link healthy enough
// to trust what the rest of the screen is showing?
//
// The staleness rule matters more than it looks. A fixed timeout cannot judge
// a 10 Hz LiDAR and a 1 Hz battery report with the same threshold, so each
// sensor declares its expected rate and is judged against that. The bridge
// makes the call and sends the verdict (protocol section 9.2); this panel only
// renders it, because duplicating the rule on both sides guarantees the two
// will eventually disagree.
//
// The link section carries two counters that exist because the framing layer
// is hand-written (protocol section 1.1): decode errors and sequence gaps.
// A non-zero decode error count means the stream itself is suspect.

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

class QLabel;

namespace gcs::ui {

class Badge;
class Card;
class HealthRow;

/// One sensor's reported health.
struct SensorHealth {
    QString id;
    QString name;
    double expectedHz = 0.0;
    double actualHz = 0.0;
    qint64 lastSeenMs = 0;
    QString state;    ///< "ok" | "degraded" | "lost" | "fault"
    QString detail;   ///< optional short note, e.g. point count or fault text
};

/// Transport-level counters.
struct LinkHealth {
    double rttMs = 0.0;
    double rssiDbm = 0.0;
    double rxBytesPerS = 0.0;
    double txBytesPerS = 0.0;
    int seqGaps = 0;
    int decodeErrors = 0;
    int reconnects = 0;
    bool connected = false;
};

class DiagnosticsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(QWidget *parent = nullptr);

    void setSensors(const QList<SensorHealth> &sensors);
    void setLink(const LinkHealth &link);
    void setStorage(bool nasOnline, int pendingUploads, double spoolFreeMb);

    /// Worst sensor state present, for the summary badge and the nav badge.
    /// Returns one of the state strings used by SensorHealth.
    static QString worstState(const QList<SensorHealth> &sensors);

private:
    Card *card_ = nullptr;
    Badge *summary_ = nullptr;
    QWidget *sensorHost_ = nullptr;
    QHash<QString, HealthRow *> rows_;

    QLabel *rtt_ = nullptr;
    QLabel *rssi_ = nullptr;
    QLabel *throughput_ = nullptr;
    QLabel *gaps_ = nullptr;
    QLabel *decodeErrors_ = nullptr;
    QLabel *reconnects_ = nullptr;
    QLabel *nas_ = nullptr;
    QLabel *spool_ = nullptr;
};

}  // namespace gcs::ui
