#pragma once

// Map view plus the controls that float on top of it.
//
// The map is present in every mode, so this card is created once by the main
// window and never swapped out. Overlay widgets are positioned directly rather
// than laid out, so they cost no map area.

#include <QWidget>

class QLabel;
class QPushButton;

namespace gcs::map {
class MapView;
}

namespace gcs::ui {

class MapCard : public QWidget {
    Q_OBJECT
public:
    explicit MapCard(QWidget *parent = nullptr);

    gcs::map::MapView *view() const { return view_; }
    QPushButton *goalButton() const { return goal_; }

    void setMapLabel(const QString &mapId, const QString &extent);

    /// Banner shown while the map is waiting for a click. Without it the only
    /// cue is the cursor shape, and it is easy to forget what was being placed.
    void setPlacementHint(const QString &text);

protected:
    void resizeEvent(QResizeEvent *) override;

private:
    gcs::map::MapView *view_ = nullptr;
    QWidget *toolbar_ = nullptr;
    QPushButton *goal_ = nullptr;
    QPushButton *fit_ = nullptr;
    QLabel *mapLabel_ = nullptr;
    QLabel *readout_ = nullptr;
    QLabel *hint_ = nullptr;
};

}  // namespace gcs::ui
