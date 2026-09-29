// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/MissionLibraryPanel.h"

#include "net/Channels.h"
#include "robot/RobotTypes.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>

namespace hmi::ui {

using namespace hmi::theme;

MissionLibraryPanel::MissionLibraryPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new Card(QStringLiteral("미션 관리"), this);
    outer->addWidget(card);

    card->body()->addWidget(sectionLabel(QStringLiteral("저장된 미션")));

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("MissionList"));
    list_->setMaximumHeight(110);
    card->body()->addWidget(list_);

    auto *listActions = new QHBoxLayout;
    new_ = new QPushButton(QStringLiteral("새 미션"));
    auto *refresh = new QPushButton(QStringLiteral("새로고침"));
    listActions->addWidget(new_);
    listActions->addWidget(refresh);
    listActions->addStretch(1);
    card->body()->addLayout(listActions);

    id_ = new QLabel(QStringLiteral("ID: —"));
    id_->setObjectName(QStringLiteral("Hint"));
    id_->setWordWrap(true);
    id_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    card->body()->addWidget(id_);
    name_ = new QLineEdit;
    name_->setPlaceholderText(QStringLiteral("미션 이름"));
    card->body()->addWidget(name_);
    revision_ = new QLabel(QStringLiteral("새 미션"));
    revision_->setObjectName(QStringLiteral("Hint"));
    revision_->setWordWrap(true);
    revision_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    card->body()->addWidget(revision_);

    card->body()->addWidget(sectionLabel(QStringLiteral("실행 단계")));

    steps_ = new QTableWidget(0, 3);
    steps_->setObjectName(QStringLiteral("MissionSteps"));
    steps_->setHorizontalHeaderLabels({QStringLiteral("단계 ID"), QStringLiteral("유형"),
                                       QStringLiteral("위치·자세 ID")});
    steps_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    steps_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    steps_->setColumnWidth(0, 95);
    steps_->setColumnWidth(1, 94);
    steps_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    steps_->setMinimumWidth(0);
    steps_->verticalHeader()->setVisible(false);
    steps_->setSelectionBehavior(QAbstractItemView::SelectRows);
    steps_->setMaximumHeight(210);
    card->body()->addWidget(steps_);

    auto *stepActions = new QHBoxLayout;
    addStep_ = new QPushButton(QStringLiteral("단계 추가"));
    removeStep_ = new QPushButton(QStringLiteral("선택 단계 삭제"));
    chooseTarget_ = new QPushButton(QStringLiteral("대상 선택"));
    stepUp_ = new QPushButton(QStringLiteral("위로"));
    stepDown_ = new QPushButton(QStringLiteral("아래로"));
    stepActions->addWidget(addStep_);
    stepActions->addWidget(removeStep_);
    stepActions->addStretch(1);
    card->body()->addLayout(stepActions);

    auto *orderActions = new QHBoxLayout;
    orderActions->addWidget(chooseTarget_);
    orderActions->addWidget(stepUp_);
    orderActions->addWidget(stepDown_);
    orderActions->addStretch(1);
    card->body()->addLayout(orderActions);

    status_ = new QLabel(QStringLiteral("로봇에 연결하면 저장된 미션을 불러옵니다."));
    status_->setWordWrap(true);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_->setObjectName(QStringLiteral("Hint"));
    card->body()->addWidget(status_);

    auto *actions = new QHBoxLayout;
    save_ = new QPushButton(QStringLiteral("저장"));
    archive_ = new QPushButton(QStringLiteral("삭제"));
    run_ = new QPushButton(QStringLiteral("선택 미션 실행"));
    save_->setProperty("variant", "primary");
    run_->setProperty("variant", "primary");
    actions->addWidget(save_);
    actions->addWidget(archive_);
    actions->addWidget(run_);
    card->body()->addLayout(actions);
    card->body()->addStretch(1);

    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { loadSelected(); });
    connect(name_, &QLineEdit::textChanged, this, [this] { updateControls(); });
    connect(steps_, &QTableWidget::itemChanged, this, [this] { updateControls(); });
    connect(steps_, &QTableWidget::itemSelectionChanged, this, [this] { updateControls(); });
    connect(refresh, &QPushButton::clicked, this, &MissionLibraryPanel::missionsRequested);
    connect(new_, &QPushButton::clicked, this, [this] {
        editing_ = true;
        editingId_ = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
        revisionValue_ = 0;
        archived_ = false;
        name_->clear();
        id_->setText(QStringLiteral("ID: %1").arg(editingId_));
        revision_->setText(QStringLiteral("새 미션 · 지도 %1").arg(mapId_.isEmpty() ? QStringLiteral("미선택") : mapId_));
        steps_->setRowCount(0);
        status_->setText(QStringLiteral("단계를 추가하고 저장하십시오."));
        list_->clearSelection();
        updateControls();
    });
    connect(addStep_, &QPushButton::clicked, this, [this] {
        if (!editing_)
            return;
        const int row = steps_->rowCount();
        QSet<QString> existing;
        for (int i = 0; i < row; ++i)
            if (const auto *item = steps_->item(i, 0))
                existing.insert(item->text());
        int sequence = 1;
        while (existing.contains(QStringLiteral("step-%1").arg(sequence)))
            ++sequence;
        steps_->insertRow(row);
        steps_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("step-%1").arg(sequence)));
        auto *type = new QComboBox;
        type->addItem(QStringLiteral("이동"), QStringLiteral("navigate"));
        type->addItem(QStringLiteral("촬영"), QStringLiteral("capture"));
        type->addItem(QStringLiteral("팔 자세"), QStringLiteral("arm_move"));
        type->addItem(QStringLiteral("충전소 복귀"), QStringLiteral("dock"));
        connect(type, &QComboBox::currentIndexChanged, this, [this] { updateControls(); });
        steps_->setCellWidget(row, 1, type);
        steps_->setItem(row, 2, new QTableWidgetItem);
        updateControls();
    });
    connect(removeStep_, &QPushButton::clicked, this, [this] {
        const int row = steps_->currentRow();
        if (row >= 0)
            steps_->removeRow(row);
        updateControls();
    });
    connect(stepUp_, &QPushButton::clicked, this, [this] { moveStep(-1); });
    connect(stepDown_, &QPushButton::clicked, this, [this] { moveStep(1); });
    connect(chooseTarget_, &QPushButton::clicked, this, &MissionLibraryPanel::chooseStepTarget);
    connect(save_, &QPushButton::clicked, this, [this] {
        if (!editing_ || mapId_.isEmpty())
            return;
        if (name_->text().trimmed().isEmpty() || steps_->rowCount() == 0) {
            status_->setText(QStringLiteral("미션 이름과 단계 하나 이상이 필요합니다."));
            return;
        }
        QSet<QString> stepIds;
        for (int row = 0; row < steps_->rowCount(); ++row) {
            const auto *id = steps_->item(row, 0);
            const auto *reference = steps_->item(row, 2);
            const auto *type = qobject_cast<QComboBox *>(steps_->cellWidget(row, 1));
            const QString stepId = id ? id->text().trimmed() : QString{};
            const QString typeId = type ? type->currentData().toString() : QString{};
            const QString ref = reference ? reference->text().trimmed() : QString{};
            if (stepId.isEmpty() || stepIds.contains(stepId)) {
                status_->setText(QStringLiteral("단계 ID가 비었거나 중복되었습니다."));
                return;
            }
            stepIds.insert(stepId);
            if (typeId != QLatin1String("dock") && ref.isEmpty()) {
                status_->setText(QStringLiteral("%1단계의 대상 ID를 지정하십시오.").arg(row + 1));
                return;
            }
            if (typeId == QLatin1String("dock") && row + 1 != steps_->rowCount()) {
                status_->setText(QStringLiteral("충전소 복귀는 마지막 단계여야 합니다."));
                return;
            }
        }
        emit saveRequested(currentMission(), revisionValue_);
        status_->setText(QStringLiteral("로봇에 저장 요청을 보냈습니다…"));
    });
    connect(archive_, &QPushButton::clicked, this, [this] {
        if (!editing_ || revisionValue_ == 0 || archived_)
            return;
        if (QMessageBox::question(this, QStringLiteral("미션 삭제"),
                                  QStringLiteral("‘%1’ 미션을 목록에서 삭제할까요?\n"
                                                 "로봇에는 복구 가능한 보관본이 남습니다.")
                                      .arg(name_->text()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            == QMessageBox::Yes) {
            emit archiveRequested(editingId_, revisionValue_);
            status_->setText(QStringLiteral("삭제 요청을 보냈습니다…"));
        }
    });
    connect(run_, &QPushButton::clicked, this, [this] {
        const QVariantMap saved = list_->currentItem()
            ? list_->currentItem()->data(Qt::UserRole).toMap() : QVariantMap{};
        const QString reason = runBlockReason(saved);
        if (!reason.isEmpty()) {
            status_->setText(reason);
            return;
        }
        emit runRequested(editingId_);
    });
    updateControls();
}

void MissionLibraryPanel::setMapId(const QString &mapId)
{
    if (mapId_ == mapId)
        return;
    mapId_ = mapId;
    editing_ = false;
    editingId_.clear();
    revisionValue_ = 0;
    steps_->setRowCount(0);
    refreshList();
    status_->setText(mapId_.isEmpty() || mapId_ == QLatin1String("live")
        ? QStringLiteral("저장된 지도를 선택해야 미션을 편집할 수 있습니다.")
        : QStringLiteral("지도 %1의 미션을 불러왔습니다.").arg(mapId_));
    updateControls();
}

void MissionLibraryPanel::setMissions(const QList<QVariantMap> &missions)
{
    QVariantMap draft;
    QVariantMap previous;
    if (editing_ && revisionValue_ > 0 && list_->currentItem()) {
        previous = list_->currentItem()->data(Qt::UserRole).toMap();
        if (previous.value(QStringLiteral("id")).toString() == editingId_) {
            previous.remove(QStringLiteral("revision"));
            previous.remove(QStringLiteral("archived"));
            draft = currentMission();
        }
    }
    missions_ = missions;
    if (editing_ && revisionValue_ > 0) {
        const auto stillAvailable = std::any_of(missions_.cbegin(), missions_.cend(),
            [this](const QVariantMap &mission) {
                return mission.value(QStringLiteral("id")).toString() == editingId_ &&
                       !mission.value(QStringLiteral("archived")).toBool();
            });
        if (!stillAvailable) {
            editing_ = false;
            editingId_.clear();
            revisionValue_ = 0;
            archived_ = false;
            name_->clear();
            id_->setText(QStringLiteral("ID: —"));
            revision_->setText(QStringLiteral("미션을 선택하십시오"));
            steps_->setRowCount(0);
            status_->setText(QStringLiteral("미션이 목록에서 삭제되었습니다."));
        }
    }
    QVariantMap updated;
    for (const auto &mission : missions_) {
        if (mission.value(QStringLiteral("id")).toString() == editingId_) {
            updated = mission;
            updated.remove(QStringLiteral("revision"));
            updated.remove(QStringLiteral("archived"));
            break;
        }
    }
    // 새로고침으로 편집 초안을 버리지 않는다. 방금 저장한 내용이 로봇에서
    // 되돌아온 경우에는 새 revision을 반영하도록 다시 불러온다.
    const bool preserveDraft = !draft.isEmpty() && draft != previous && draft != updated;
    refreshList(editingId_, !preserveDraft);
    if (preserveDraft)
        status_->setText(QStringLiteral("수정 중인 내용은 유지했습니다. 저장 전에는 실행할 수 없습니다."));
    updateControls();
}

void MissionLibraryPanel::setWaypoints(const QList<QVariantMap> &waypoints)
{
    waypoints_ = waypoints;
    updateControls();
}

void MissionLibraryPanel::setArmPosePresets(const QList<QVariantMap> &presets)
{
    armPosePresets_ = presets;
    updateControls();
}

void MissionLibraryPanel::setMissionState(hmi::robot::MissionState state)
{
    missionBusy_ = state != hmi::robot::MissionState::Idle &&
                   state != hmi::robot::MissionState::Completed &&
                   state != hmi::robot::MissionState::Failed;
    updateControls();
}

void MissionLibraryPanel::handleCommandResult(const QString &channel, bool ok,
                                              const QString &code, const QString &message)
{
    if (channel == QLatin1String(hmi::ch::kCmdMissionsSave) ||
        channel == QLatin1String(hmi::ch::kCmdMissionsArchive)) {
        status_->setText(ok ? (channel == QLatin1String(hmi::ch::kCmdMissionsArchive)
                                 ? QStringLiteral("미션을 목록에서 삭제했습니다.")
                                 : QStringLiteral("로봇 저장 완료"))
                            : QStringLiteral("저장 거부 · %1 %2").arg(code, message));
    } else if (channel == QLatin1String(hmi::ch::kCmdMissionStart)) {
        status_->setText(ok ? QStringLiteral("미션 실행을 수락했습니다")
                            : QStringLiteral("실행 거부 · %1 %2").arg(code, message));
    }
}

QVariantMap MissionLibraryPanel::currentMission() const
{
    QVariantMap mission;
    if (const auto *selected = list_->currentItem()) {
        const QVariantMap stored = selected->data(Qt::UserRole).toMap();
        if (stored.value(QStringLiteral("id")).toString() == editingId_)
            mission = stored;
    }
    mission.remove(QStringLiteral("revision"));
    mission.remove(QStringLiteral("archived"));
    const QVariantList storedSteps = mission.value(QStringLiteral("steps")).toList();
    QVariantList steps;
    for (int row = 0; row < steps_->rowCount(); ++row) {
        const auto *type = qobject_cast<QComboBox *>(steps_->cellWidget(row, 1));
        const QString typeId = type ? type->currentData().toString() : QString{};
        const QString ref = steps_->item(row, 2) ? steps_->item(row, 2)->text().trimmed() : QString{};
        const QString stepId = steps_->item(row, 0)
            ? steps_->item(row, 0)->text().trimmed() : QString{};
        QVariantMap step;
        for (const auto &value : storedSteps) {
            const QVariantMap original = value.toMap();
            if (original.value(QStringLiteral("id")).toString() == stepId &&
                original.value(QStringLiteral("type")).toString() == typeId) {
                step = original;
                break;
            }
        }
        step[QStringLiteral("id")] = stepId;
        step[QStringLiteral("type")] = typeId;
        step.remove(QStringLiteral("arm_pose_id"));
        step.remove(QStringLiteral("capture_preset_id"));
        if (typeId == QLatin1String("arm_move"))
            step[QStringLiteral("pose")] = ref;
        else if (typeId == QLatin1String("capture"))
            step[QStringLiteral("preset")] = ref;
        else if (typeId == QLatin1String("navigate"))
            step[QStringLiteral("location_id")] = ref;
        steps << step;
    }
    mission[QStringLiteral("id")] = editingId_;
    mission[QStringLiteral("name")] = name_->text().trimmed();
    mission[QStringLiteral("map_id")] = mapId_;
    mission[QStringLiteral("steps")] = steps;
    return mission;
}

void MissionLibraryPanel::refreshList(const QString &keepId, bool reloadEditor)
{
    int selected = -1;
    {
        const QSignalBlocker blocker(list_);
        list_->clear();
        for (const auto &mission : missions_) {
            if (mission.value(QStringLiteral("map_id")).toString() != mapId_ ||
                mission.value(QStringLiteral("archived")).toBool())
                continue;
            auto *item = new QListWidgetItem(
                QStringLiteral("%1   ·   r%2").arg(mission.value(QStringLiteral("name")).toString())
                    .arg(mission.value(QStringLiteral("revision")).toULongLong()), list_);
            item->setData(Qt::UserRole, mission);
            if (mission.value(QStringLiteral("id")).toString() == keepId)
                selected = list_->row(item);
        }
        if (selected >= 0)
            list_->setCurrentRow(selected);
        else if (!editing_ && list_->count() > 0)
            list_->setCurrentRow(0);
    }
    if (reloadEditor && list_->currentItem())
        loadSelected();
}

void MissionLibraryPanel::loadSelected()
{
    const auto *item = list_->currentItem();
    if (!item)
        return;
    const QVariantMap mission = item->data(Qt::UserRole).toMap();
    editing_ = true;
    editingId_ = mission.value(QStringLiteral("id")).toString();
    revisionValue_ = mission.value(QStringLiteral("revision")).toULongLong();
    archived_ = mission.value(QStringLiteral("archived")).toBool();
    name_->setText(mission.value(QStringLiteral("name")).toString());
    id_->setText(QStringLiteral("ID: %1").arg(editingId_));
    revision_->setText(QStringLiteral("revision %1 · 지도 %2")
                           .arg(revisionValue_).arg(mapId_));
    steps_->setRowCount(0);
    const auto stepList = mission.value(QStringLiteral("steps")).toList();
    for (int i = 0; i < stepList.size(); ++i) {
        const QVariantMap step = stepList.at(i).toMap();
        const int row = steps_->rowCount();
        steps_->insertRow(row);
        steps_->setItem(row, 0, new QTableWidgetItem(step.value(QStringLiteral("id")).toString()));
        auto *type = new QComboBox;
        type->addItem(QStringLiteral("이동"), QStringLiteral("navigate"));
        type->addItem(QStringLiteral("촬영"), QStringLiteral("capture"));
        type->addItem(QStringLiteral("팔 자세"), QStringLiteral("arm_move"));
        type->addItem(QStringLiteral("충전소 복귀"), QStringLiteral("dock"));
        const QString typeId = step.value(QStringLiteral("type")).toString();
        const int typeIndex = type->findData(typeId);
        if (typeIndex >= 0) type->setCurrentIndex(typeIndex);
        connect(type, &QComboBox::currentIndexChanged, this, [this] { updateControls(); });
        steps_->setCellWidget(row, 1, type);
        const QString ref = typeId == QLatin1String("arm_move")
            ? step.value(QStringLiteral("pose")).toString()
            : typeId == QLatin1String("capture")
                ? step.value(QStringLiteral("preset")).toString()
                : step.value(QStringLiteral("location_id")).toString();
        steps_->setItem(row, 2, new QTableWidgetItem(ref));
    }
    const QString reason = runBlockReason(mission);
    status_->setText(reason.isEmpty() ? QStringLiteral("저장된 미션입니다. 실행할 수 있습니다.")
                                      : reason);
    updateControls();
}

QString MissionLibraryPanel::runBlockReason(const QVariantMap &mission) const
{
    if (mission.isEmpty() || !editing_ || revisionValue_ == 0 || archived_)
        return QStringLiteral("저장된 미션을 선택하십시오.");
    if (mission.value(QStringLiteral("id")).toString() != editingId_)
        return QStringLiteral("저장된 미션을 선택하십시오.");
    if (mission.value(QStringLiteral("map_id")).toString() != mapId_)
        return QStringLiteral("현재 지도와 미션의 지도가 다릅니다.");

    // The editor may contain unsaved changes while the robot still holds the
    // previous revision. Do not let Run silently execute that older plan.
    QVariantMap saved = mission;
    saved.remove(QStringLiteral("revision"));
    saved.remove(QStringLiteral("archived"));
    if (currentMission() != saved)
        return QStringLiteral("수정한 내용을 먼저 저장하십시오.");

    bool hasNavigate = false;
    const QVariantList steps = mission.value(QStringLiteral("steps")).toList();
    for (int i = 0; i < steps.size(); ++i) {
        const QVariantMap step = steps.at(i).toMap();
        const QString type = step.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("capture") || type == QLatin1String("arm_move"))
            return QStringLiteral("촬영·팔 단계의 로봇 실행기가 아직 연결되지 않았습니다. 저장은 가능하지만 실행할 수 없습니다.");
        if (type == QLatin1String("navigate")) {
            hasNavigate = true;
            if (step.value(QStringLiteral("location_id")).toString().isEmpty())
                return QStringLiteral("이동 단계의 위치 ID가 비어 있습니다.");
        } else if (type == QLatin1String("dock")) {
            if (i + 1 != steps.size())
                return QStringLiteral("충전소 복귀는 마지막 단계여야 합니다.");
        } else {
            return QStringLiteral("지원하지 않는 미션 단계가 있습니다.");
        }
    }
    if (!hasNavigate)
        return QStringLiteral("실행할 이동 단계가 없습니다.");
    return {};
}

void MissionLibraryPanel::moveStep(int delta)
{
    const int row = steps_->currentRow();
    const int other = row + delta;
    if (row < 0 || other < 0 || other >= steps_->rowCount())
        return;
    const auto exchange = [this, row, other](int column) {
        auto *a = steps_->item(row, column);
        auto *b = steps_->item(other, column);
        const QString value = a->text();
        a->setText(b->text());
        b->setText(value);
    };
    exchange(0);
    exchange(2);
    auto *a = qobject_cast<QComboBox *>(steps_->cellWidget(row, 1));
    auto *b = qobject_cast<QComboBox *>(steps_->cellWidget(other, 1));
    const int type = a->currentIndex();
    a->setCurrentIndex(b->currentIndex());
    b->setCurrentIndex(type);
    steps_->selectRow(other);
    updateControls();
}

void MissionLibraryPanel::chooseStepTarget()
{
    const int row = steps_->currentRow();
    if (row < 0 || !editing_)
        return;
    auto *type = qobject_cast<QComboBox *>(steps_->cellWidget(row, 1));
    auto *reference = steps_->item(row, 2);
    if (!type || !reference)
        return;
    const QString typeId = type->currentData().toString();
    const QList<QVariantMap> *assets = typeId == QLatin1String("navigate") ? &waypoints_
                                    : typeId == QLatin1String("arm_move") ? &armPosePresets_
                                    : nullptr;
    if (!assets)
        return;
    QStringList labels;
    QList<QString> ids;
    for (const auto &asset : *assets) {
        if (asset.value(QStringLiteral("archived")).toBool())
            continue;
        const QString id = asset.value(QStringLiteral("id")).toString();
        if (id.isEmpty())
            continue;
        ids << id;
        labels << QStringLiteral("%1  (%2)")
                      .arg(asset.value(QStringLiteral("name"), id).toString(), id);
    }
    if (labels.isEmpty()) {
        status_->setText(typeId == QLatin1String("navigate")
                             ? QStringLiteral("이 지도에 저장된 웨이포인트가 없습니다.")
                             : QStringLiteral("로봇에 저장된 팔 자세가 없습니다."));
        return;
    }
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(
        this, QStringLiteral("단계 대상 선택"),
        typeId == QLatin1String("navigate") ? QStringLiteral("웨이포인트")
                                             : QStringLiteral("팔 자세"),
        labels, static_cast<int>(std::max<qsizetype>(0, ids.indexOf(reference->text()))),
        false, &accepted);
    if (!accepted)
        return;
    const int selected = labels.indexOf(chosen);
    if (selected >= 0) {
        reference->setText(ids.at(selected));
        reference->setToolTip(chosen);
    }
}

void MissionLibraryPanel::updateControls()
{
    const bool canEdit = !missionBusy_ && !mapId_.isEmpty() && mapId_ != QLatin1String("live");
    new_->setEnabled(canEdit);
    addStep_->setEnabled(canEdit && editing_);
    removeStep_->setEnabled(canEdit && editing_ && steps_->currentRow() >= 0);
    stepUp_->setEnabled(canEdit && editing_ && steps_->currentRow() > 0);
    stepDown_->setEnabled(canEdit && editing_ && steps_->currentRow() >= 0 &&
                          steps_->currentRow() + 1 < steps_->rowCount());
    const auto *selectedType = steps_->currentRow() >= 0
        ? qobject_cast<QComboBox *>(steps_->cellWidget(steps_->currentRow(), 1)) : nullptr;
    const QString typeId = selectedType ? selectedType->currentData().toString() : QString{};
    chooseTarget_->setEnabled(canEdit && editing_ &&
        (typeId == QLatin1String("navigate") || typeId == QLatin1String("arm_move")));
    name_->setEnabled(canEdit && editing_);
    steps_->setEnabled(canEdit && editing_);
    save_->setEnabled(canEdit && editing_);
    archive_->setEnabled(canEdit && revisionValue_ > 0 && !archived_);
    const QVariantMap saved = list_->currentItem()
        ? list_->currentItem()->data(Qt::UserRole).toMap() : QVariantMap{};
    const QString runReason = runBlockReason(saved);
    run_->setEnabled(!missionBusy_ && runReason.isEmpty());
    run_->setToolTip(missionBusy_ ? QStringLiteral("미션이 실행 중입니다.") : runReason);
}

}  // namespace hmi::ui
