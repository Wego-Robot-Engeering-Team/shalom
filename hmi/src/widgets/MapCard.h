// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

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
class IconButton;

class MapCard : public QWidget {
    Q_OBJECT
public:
    explicit MapCard(QWidget *parent = nullptr);

    hmi::map::MapView *view() const { return view_; }
    QPushButton *poseEstimateButton() const { return poseEstimate_; }
    QPushButton *mapButton() const { return mapButton_; }
    IconButton *refreshButton() const { return refreshButton_; }
    void setMapListEnabled(bool enabled);
    MapLegend *legend() const { return legend_; }

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
    QPushButton *poseEstimate_ = nullptr;
    QPushButton *mapButton_ = nullptr;
    IconButton *refreshButton_ = nullptr;
    QWidget *mapControls_ = nullptr;
    QLabel *mapLabel_ = nullptr;
    QLabel *readout_ = nullptr;
    QLabel *hint_ = nullptr;
    MapLegend *legend_ = nullptr;
    void positionMapControls();
};

}  // namespace hmi::ui
