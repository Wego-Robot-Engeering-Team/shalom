// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#pragma once

#include <QList>
#include <QVariantMap>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace hmi::robot { enum class MissionState; }

namespace hmi::ui {

/// Edits mission definitions stored by the selected robot and map.
class MissionLibraryPanel final : public QWidget {
    Q_OBJECT
public:
    explicit MissionLibraryPanel(QWidget *parent = nullptr);

    void setMissions(const QList<QVariantMap> &missions);
    void setWaypoints(const QList<QVariantMap> &waypoints);
    void setArmPosePresets(const QList<QVariantMap> &presets);
    void setMapId(const QString &mapId);
    void setMissionState(hmi::robot::MissionState state);
    void setEditingEnabled(bool enabled);
    void setExecutionEnabled(bool enabled, const QString &reason = {});
    void handleCommandResult(const QString &channel, bool ok,
                             const QString &code, const QString &message);

signals:
    void missionsRequested();
    void saveRequested(const QVariantMap &mission, quint64 expectedRevision);
    void archiveRequested(const QString &id, quint64 expectedRevision);
    void runRequested(const QString &id);

private:
    struct StepDraft {
        QString id;
        QString type;
        QString reference;
        QVariantMap original;
    };

    void startNew();
    void startEdit(const QVariantMap &mission);
    void closeEditor();
    void refreshList();
    void rebuildSteps();
    void resizeStepList();
    void moveStep(int index, int delta);
    void updateStepTarget(int index, const QString &type);
    void updateControls();
    void setStatus(const QString &message);
    bool confirmPendingSave();
    bool confirmPendingArchive();
    QVariantMap currentMission() const;
    QString runBlockReason(const QVariantMap &mission) const;
    bool canEdit() const;

    QStackedWidget *pages_ = nullptr;
    QListWidget *list_ = nullptr;
    QLineEdit *name_ = nullptr;
    QLabel *editorTitle_ = nullptr;
    QLabel *empty_ = nullptr;
    QLabel *stepsEmpty_ = nullptr;
    QLabel *status_ = nullptr;
    QListWidget *steps_ = nullptr;
    QPushButton *addStep_ = nullptr;
    QPushButton *new_ = nullptr;
    QPushButton *save_ = nullptr;
    QPushButton *cancel_ = nullptr;
    QList<QVariantMap> missions_;
    QList<QVariantMap> waypoints_;
    QList<QVariantMap> armPosePresets_;
    QList<StepDraft> draftSteps_;
    QVariantMap editingOriginal_;
    QString mapId_;
    QString editingId_;
    QString pendingSaveId_;
    QVariantMap pendingMission_;
    quint64 pendingSaveGeneration_ = 0;
    bool pendingSaveAccepted_ = false;
    bool saveResultOutstanding_ = false;
    QString pendingArchiveId_;
    quint64 pendingArchiveGeneration_ = 0;
    bool pendingArchiveAccepted_ = false;
    bool archiveResultOutstanding_ = false;
    quint64 revisionValue_ = 0;
    quint64 contextGeneration_ = 0;
    bool editing_ = false;
    bool missionBusy_ = false;
    bool editingEnabled_ = true;
    bool executionEnabled_ = true;
    QString executionReason_;
};

}  // namespace hmi::ui
