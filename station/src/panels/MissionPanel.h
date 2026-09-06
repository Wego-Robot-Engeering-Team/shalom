#pragma once

// Inspection progress summary for the drive view. Statement of work 2.2.7 [3].
//
// Starting and stopping a run belongs here, not on the locations view. The two
// screens do different jobs: locations is where the point list is decided, drive is
// where a run is carried out and watched. Putting "start autonomous driving" in
// the editing screen meant leaving the map to begin, and leaving it again to
// watch - and start sat next to delete, which is not a neighbour it should have.
//
// It shows where the run is, not the whole list. The full list belongs to the
// locations view, which is where points are added, reordered and deleted;
// repeating all 64 rows here said nothing extra and pushed the run controls
// off the bottom. During a run the operator is watching the map and wants one
// answer: which point now, and which next.

#include <QVariantMap>
#include <QList>
#include <QWidget>

class QLabel;
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

    /// "idle" | "running" | "paused". Drives the header badge and what the run
    /// button says.
    void setMissionState(const QString &state);

    /// Whether the charging station has a known pose. Without one there is
    /// nowhere to send the robot, and a button that quietly does nothing is
    /// worse than one that is visibly unavailable.
    void setDockKnown(bool known);

signals:
    void missionStart();

    /// Pause keeps the run. The robot stops where it is, the point list keeps
    /// its statuses, and resume carries on from the same point.
    void missionPause();
    void missionResume();

    /// Stop ends the run. Progress is discarded, so a later start begins at the
    /// first point again. On screen the two look like siblings and the
    /// difference only shows up afterwards, so the panel asks first.
    void missionStop();

    /// Send the robot back to the charging station. Offered only when no run is
    /// under way: during one the robot already returns on its own when the
    /// points are done or the battery gets low.
    void returnToDock();

private:
    void refresh();

    /// The run button acts on whatever state the panel is in.
    void onRunClicked();

    /// Cancelling is not undoable and a run can be an hour of driving. Asked
    /// only when there is progress to lose; the emergency stop is the control
    /// for stopping in a hurry and it never asks.
    void confirmStop();

    Card *card_ = nullptr;
    Badge *state_ = nullptr;
    QProgressBar *bar_ = nullptr;
    QLabel *count_ = nullptr;
    QLabel *current_ = nullptr;
    QLabel *next_ = nullptr;

    /// One button for the run itself: start, then pause, then resume. Three
    /// labels on one control rather than three controls of which two are
    /// always greyed out.
    QPushButton *run_ = nullptr;

    /// Shown whenever a run is under way, paused or not.
    QPushButton *stop_ = nullptr;

    /// Shown only when idle - it takes the place cancel occupies during a run.
    QPushButton *dock_ = nullptr;

    QList<QVariantMap> points_;
    QString missionState_ = QStringLiteral("idle");
    bool dockKnown_ = false;
};

}  // namespace gcs::ui
