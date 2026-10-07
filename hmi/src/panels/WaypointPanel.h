// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

// Saved waypoint catalog. Visit order belongs to a mission's step list.

#include <QVariantMap>
#include <QWidget>

class QListWidget;
class QPushButton;
class QLabel;

namespace hmi::ui {

class Badge;
class Card;

class WaypointPanel : public QWidget {
    Q_OBJECT
public:
    explicit WaypointPanel(QWidget *parent = nullptr);

    void setWaypoints(const QList<QVariantMap> &waypoints);
    void setEditingEnabled(bool enabled);
    void setRobotPoseAvailable(bool available, const QString &reason = {});
    void setSaveStatus(const QString &message, bool error = false);
    QList<QVariantMap> waypoints() const;
    void setStatus(const QString &id, const QString &status);

signals:
    void addRequested();
    void captureFromRobotRequested();
    void updateRequested(const QString &id, const QVariantMap &point);
    void deleteRequested(const QString &id);
    void gotoRequested(const QString &id);
    void waypointSelected(const QString &id);

    /// Emitted whenever the list or any point's status changes, so that
    /// summaries elsewhere cannot drift out of step with this list.
    void waypointsChanged(const QList<QVariantMap> &points);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void updateActionButtons();
    void updateListItemSizes();

    Card *card_ = nullptr;
    Badge *count_ = nullptr;
    QListWidget *list_ = nullptr;
    QPushButton *add_ = nullptr;
    QPushButton *fromRobot_ = nullptr;
    QLabel *saveStatus_ = nullptr;
    bool editingEnabled_ = false;
    bool robotPoseAvailable_ = false;
    quint64 catalogGeneration_ = 0;
};

}  // namespace hmi::ui
