#pragma once

// Inspection waypoint sequence panel. Statement of work 2.2.7 [2] item 1.
//
// Add, delete, reorder, and run the autonomous sequence. Array order is the
// visit order; reordering is done by dragging a row.
//
// Resume is deliberately a separate action from start: the statement of work
// prohibits automatic resumption after a stop, so the panel never issues one
// on the operator's behalf (2.2.5).

#include <QVariantMap>
#include <QWidget>

class QListWidget;
class QPushButton;

namespace gcs::ui {

class Badge;
class Card;

class WaypointPanel : public QWidget {
    Q_OBJECT
public:
    explicit WaypointPanel(QWidget *parent = nullptr);

    void setWaypoints(const QList<QVariantMap> &waypoints);
    QList<QVariantMap> waypoints() const;
    void setStatus(const QString &id, const QString &status);

    /// Drives which of the run controls are available.
    void setMissionState(bool running, bool paused);

signals:
    void addRequested();
    void deleteRequested(const QString &id);
    void orderChanged(const QStringList &ids);
    void waypointSelected(const QString &id);
    void missionStart();
    void missionPause();
    void missionResume();
    void missionStop();

private:
    void move(int delta);
    void emitOrder();

    Card *card_ = nullptr;
    Badge *count_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *start_ = nullptr;
    QPushButton *pause_ = nullptr;
    QPushButton *resume_ = nullptr;
    QPushButton *stop_ = nullptr;
};

}  // namespace gcs::ui
