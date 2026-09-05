#pragma once

// Main control station window.
//
//   +- top bar: brand, link and mission badges, mode switch, emergency stop -+
//   +-----------+--------------------------------+-------------------------+
//   | status    |  map view (with overlay tools)  |  waypoint sequence      |
//   | manual    |                                 |  arm control            |
//   | event log |                                 |                         |
//   +-----------+--------------------------------+-------------------------+
//
// The window owns the log store and routes panel signals into it, so that
// every operator action and every rejected command lands in the same history
// that gets exported for support.

#include <QMainWindow>

#include "Demo.h"

class QLabel;
class QPushButton;
class QTimer;

namespace gcs::diag {
class LogStore;
}

namespace gcs::map {
class MapView;
}

namespace gcs::ui {

class AlertFrame;
class ArmPanel;
class Badge;
class EStopButton;
class EventLogPanel;
class StatusPanel;
class TeleopPanel;
class WaypointPanel;

/// Map view plus the controls that float on top of it.
class MapCard : public QWidget {
    Q_OBJECT
public:
    explicit MapCard(QWidget *parent = nullptr);

    gcs::map::MapView *view() const { return view_; }
    QPushButton *goalButton() const { return goal_; }
    QPushButton *waypointButton() const { return waypoint_; }
    void setMapLabel(const QString &mapId, const QString &extent);

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    gcs::map::MapView *view_ = nullptr;
    QWidget *toolbar_ = nullptr;
    QPushButton *goal_ = nullptr;
    QPushButton *waypoint_ = nullptr;
    QPushButton *fit_ = nullptr;
    QLabel *mapLabel_ = nullptr;
    QLabel *readout_ = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(bool demoMode = true, QWidget *parent = nullptr);

    /// Rebuilds the stylesheet and repaints everything that draws itself.
    void applyTheme(const QString &name);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

    /// Fits the map on first show. The constructor cannot do it: the viewport
    /// has no final size until the layout has run, so fitting there leaves the
    /// map scaled to a stale rectangle.
    void showEvent(QShowEvent *ev) override;

private:
    QWidget *buildTopBar();
    QWidget *buildLeftColumn();
    QWidget *buildRightColumn();
    void wireSignals();

    void engageEstop();
    void releaseEstop();
    void setMode(const QString &mode);
    void startDemo();
    void demoTick();

    gcs::diag::LogStore *log_ = nullptr;

    MapCard *map_ = nullptr;
    StatusPanel *status_ = nullptr;
    TeleopPanel *teleop_ = nullptr;
    EventLogPanel *events_ = nullptr;
    WaypointPanel *waypoints_ = nullptr;
    ArmPanel *arm_ = nullptr;

    EStopButton *estop_ = nullptr;
    AlertFrame *alert_ = nullptr;
    Badge *linkBadge_ = nullptr;
    Badge *missionBadge_ = nullptr;
    QPushButton *autoBtn_ = nullptr;
    QPushButton *manualBtn_ = nullptr;
    QPushButton *themeBtn_ = nullptr;

    bool demoMode_ = true;
    bool didInitialFit_ = false;
    std::unique_ptr<gcs::demo::Feed> feed_;
    gcs::demo::MapData mapData_;
    QTimer *timer_ = nullptr;
};

}  // namespace gcs::ui
