#pragma once

// Inspection progress summary for the drive view. Statement of work 2.2.7 [3].
//
// Starting and stopping a run belongs here, not on the locations view. The two
// screens do different jobs: locations is where the point list is decided, drive is
// where a run is carried out and watched. Putting "start autonomous driving" in
// the editing screen meant leaving the map to begin, and leaving it again to
// watch - and start sat next to delete, which is not a neighbour it should have.
//
// Rows are drawn by the shared WaypointDelegate, so a point cannot read
// differently on the two views.

#include <QVariantMap>
#include <QList>
#include <QWidget>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;

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

    /// "idle" | "running" | "paused". Drives the header badge and which of the
    /// run controls are available.
    void setMissionState(const QString &state);

signals:
    void missionStart();
    void missionPause();
    void missionResume();
    void missionStop();

private:
    void refresh();

    Card *card_ = nullptr;
    Badge *state_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *count_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *pause_ = nullptr;
    QPushButton *resume_ = nullptr;
    QPushButton *stop_ = nullptr;

    QList<QVariantMap> points_;
    QString missionState_ = QStringLiteral("idle");
};

}  // namespace gcs::ui
