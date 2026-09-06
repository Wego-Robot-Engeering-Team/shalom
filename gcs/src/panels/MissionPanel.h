#pragma once

// Inspection progress summary for the drive view. Statement of work 2.2.7 [3].
//
// The drive view is where an operator sits during a run, but the point list
// and its controls live on the locations view. Without this card the operator
// has to leave the map to answer "how far along are we?" - so this shows the
// answer where they already are, read-only.

#include <QVariantMap>
#include <QList>
#include <QWidget>

class QLabel;
class QProgressBar;

namespace gcs::ui {

class Badge;
class Card;

class MissionPanel : public QWidget {
    Q_OBJECT
public:
    explicit MissionPanel(QWidget *parent = nullptr);

    /// Waypoints in visit order. Each entry carries "name" and "status",
    /// where status is one of: todo, current, done, error - the same
    /// vocabulary WaypointPanel uses, so both views agree.
    void setWaypoints(const QList<QVariantMap> &points);

    /// "idle" | "running" | "paused". Drives the header badge.
    void setMissionState(const QString &state);

private:
    void refresh();

    Card *card_ = nullptr;
    Badge *state_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *current_ = nullptr;
    QLabel *next_ = nullptr;

    QList<QVariantMap> points_;
    QString missionState_ = QStringLiteral("idle");
};

}  // namespace gcs::ui
