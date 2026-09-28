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
#include <QSizePolicy>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QUuid>

namespace hmi::ui {

using namespace hmi::theme;

MissionLibraryPanel::MissionLibraryPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new Card(QStringLiteral("미션 라이브러리"), this);
    outer->addWidget(card);
    auto *intro = new QLabel(QStringLiteral(
        "미션은 로봇에 저장됩니다. 단계에서 위치·팔 자세·촬영 프리셋을 선택합니다."));
    intro->setWordWrap(true);
    intro->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    card->body()->addWidget(intro);

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

    steps_ = new QTableWidget(0, 3);
    steps_->setObjectName(QStringLiteral("MissionSteps"));
    steps_->setHorizontalHeaderLabels({QStringLiteral("단계 ID"), QStringLiteral("유형"),
                                       QStringLiteral("자산 ID")});
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
    stepUp_ = new QPushButton(QStringLiteral("위로"));
    stepDown_ = new QPushButton(QStringLiteral("아래로"));
    stepActions->addWidget(addStep_);
    stepActions->addWidget(removeStep_);
    stepActions->addStretch(1);
    card->body()->addLayout(stepActions);

    auto *orderActions = new QHBoxLayout;
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
    archive_ = new QPushButton(QStringLiteral("보관"));
    run_ = new QPushButton(QStringLiteral("실행"));
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
        steps_->insertRow(row);
        steps_->setItem(row, 0, new QTableWidgetItem(QStringLiteral("step-%1").arg(row + 1)));
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
    connect(save_, &QPushButton::clicked, this, [this] {
        if (!editing_ || mapId_.isEmpty())
            return;
        if (name_->text().trimmed().isEmpty() || steps_->rowCount() == 0) {
            status_->setText(QStringLiteral("미션 이름과 단계 하나 이상이 필요합니다."));
            return;
        }
        emit saveRequested(currentMission(), revisionValue_);
        status_->setText(QStringLiteral("로봇에 저장 요청을 보냈습니다…"));
    });
    connect(archive_, &QPushButton::clicked, this, [this] {
        if (!editing_ || revisionValue_ == 0 || archived_)
            return;
        if (QMessageBox::question(this, QStringLiteral("미션 보관"),
                                  QStringLiteral("이 미션을 목록에서 보관할까요?"))
            == QMessageBox::Yes) {
            emit archiveRequested(editingId_, revisionValue_);
            status_->setText(QStringLiteral("보관 요청을 보냈습니다…"));
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
    missions_ = missions;
    refreshList(editingId_);
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
        status_->setText(ok ? QStringLiteral("로봇 저장 완료")
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

void MissionLibraryPanel::refreshList(const QString &keepId)
{
    list_->clear();
    int selected = -1;
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

void MissionLibraryPanel::updateControls()
{
    const bool canEdit = !missionBusy_ && !mapId_.isEmpty() && mapId_ != QLatin1String("live");
    new_->setEnabled(canEdit);
    addStep_->setEnabled(canEdit && editing_);
    removeStep_->setEnabled(canEdit && editing_ && steps_->currentRow() >= 0);
    stepUp_->setEnabled(canEdit && editing_ && steps_->currentRow() > 0);
    stepDown_->setEnabled(canEdit && editing_ && steps_->currentRow() >= 0 &&
                          steps_->currentRow() + 1 < steps_->rowCount());
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
