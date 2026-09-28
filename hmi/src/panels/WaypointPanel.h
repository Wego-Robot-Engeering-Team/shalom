// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Saved waypoint catalog. Visit order belongs to a mission's step list.

#include <QVariantMap>
#include <QWidget>

class QListWidget;
class QPushButton;

namespace hmi::ui {

class Badge;
class Card;

class WaypointPanel : public QWidget {
    Q_OBJECT
public:
    explicit WaypointPanel(QWidget *parent = nullptr);

    void setWaypoints(const QList<QVariantMap> &waypoints);
    QList<QVariantMap> waypoints() const;
    void setStatus(const QString &id, const QString &status);

signals:
    void addRequested();
    void deleteRequested(const QString &id);
    void gotoRequested(const QString &id);
    void waypointSelected(const QString &id);

    /// Emitted whenever the list or any point's status changes, so that
    /// summaries elsewhere cannot drift out of step with this list.
    void waypointsChanged(const QList<QVariantMap> &points);

private:
    Card *card_ = nullptr;
    Badge *count_ = nullptr;
    QListWidget *list_ = nullptr;
};

}  // namespace hmi::ui
