// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include <QList>
#include <QVariantMap>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QTableWidget;
class QPushButton;

namespace hmi::robot { enum class MissionState; }

namespace hmi::ui {

/// Edits mission definitions stored by the selected robot and map.
class MissionLibraryPanel final : public QWidget {
    Q_OBJECT
public:
    explicit MissionLibraryPanel(QWidget *parent = nullptr);

    void setMissions(const QList<QVariantMap> &missions);
    void setMapId(const QString &mapId);
    void setMissionState(hmi::robot::MissionState state);
    void handleCommandResult(const QString &channel, bool ok,
                             const QString &code, const QString &message);

signals:
    void missionsRequested();
    void saveRequested(const QVariantMap &mission, quint64 expectedRevision);
    void archiveRequested(const QString &id, quint64 expectedRevision);
    void runRequested(const QString &id);

private:
    void loadSelected();
    void refreshList(const QString &keepId = {});
    void updateControls();
    void moveStep(int delta);
    QVariantMap currentMission() const;
    QString runBlockReason(const QVariantMap &mission) const;

    QListWidget *list_ = nullptr;
    QLineEdit *name_ = nullptr;
    QLabel *id_ = nullptr;
    QLabel *revision_ = nullptr;
    QLabel *status_ = nullptr;
    QTableWidget *steps_ = nullptr;
    QPushButton *addStep_ = nullptr;
    QPushButton *removeStep_ = nullptr;
    QPushButton *stepUp_ = nullptr;
    QPushButton *stepDown_ = nullptr;
    QPushButton *new_ = nullptr;
    QPushButton *save_ = nullptr;
    QPushButton *archive_ = nullptr;
    QPushButton *run_ = nullptr;
    QList<QVariantMap> missions_;
    QString mapId_;
    QString editingId_;
    quint64 revisionValue_ = 0;
    bool archived_ = false;
    bool editing_ = false;
    bool missionBusy_ = false;
};

}  // namespace hmi::ui
