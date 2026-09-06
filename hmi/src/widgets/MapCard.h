#pragma once

// Map view plus the controls that float on top of it.
//
// The map is present in every mode, so this card is created once by the main
// window and never swapped out. Overlay widgets are positioned directly rather
// than laid out, so they cost no map area.

#include <QWidget>

class QLabel;
class QPushButton;

namespace hmi::map {
class MapView;
}

namespace hmi::ui {

class MapLegend;

class MapCard : public QWidget {
    Q_OBJECT
public:
    explicit MapCard(QWidget *parent = nullptr);

    hmi::map::MapView *view() const { return view_; }
    QPushButton *goalButton() const { return goal_; }
    MapLegend *legend() const { return legend_; }

    /// Puts the drive-mode buttons at the left of the map's floating toolbar.
    ///
    /// Mode belongs on the surface it governs, not in the window chrome next
    /// to the theme toggle. The toolbar floats over the map, so it stays
    /// reachable from every view without costing map area.
    void addModeButtons(QWidget *autoBtn, QWidget *manualBtn);

    void setMapLabel(const QString &mapId, const QString &extent);

    /// Banner shown while the map is waiting for a click. Without it the only
    /// cue is the cursor shape, and it is easy to forget what was being placed.
    void setPlacementHint(const QString &text);

protected:
    void resizeEvent(QResizeEvent *) override;

    /// Hides the coordinate readout when the cursor leaves the map. An empty
    /// box floating over the map reads as a broken widget.
    void leaveEvent(QEvent *) override;

private:
    hmi::map::MapView *view_ = nullptr;
    QWidget *toolbar_ = nullptr;
    QWidget *toolbarRow_ = nullptr;
    QPushButton *goal_ = nullptr;
    QLabel *mapLabel_ = nullptr;
    QLabel *readout_ = nullptr;
    QLabel *hint_ = nullptr;
    MapLegend *legend_ = nullptr;
};

}  // namespace hmi::ui
