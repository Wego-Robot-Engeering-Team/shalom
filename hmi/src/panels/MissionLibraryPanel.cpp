// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/MissionLibraryPanel.h"

#include "net/Channels.h"
#include "robot/RobotTypes.h"
#include "theme/Tokens.h"
#include "widgets/IconButton.h"
#include "widgets/Primitives.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace hmi::ui {

using namespace hmi::theme;

namespace {

QString stepReference(const QVariantMap &step)
{
    const QString type = step.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("navigate"))
        return step.value(QStringLiteral("location_id")).toString();
    if (type == QLatin1String("arm_move"))
        return step.value(QStringLiteral("pose")).toString();
    if (type == QLatin1String("capture"))
        return step.value(QStringLiteral("preset")).toString();
    return {};
}

bool validReferenceId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9_-]{1,96}$"));
    return pattern.match(id).hasMatch();
}

}  // namespace

MissionLibraryPanel::MissionLibraryPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new Card(QStringLiteral("미션 관리"), this);
    outer->addWidget(card, 0, Qt::AlignTop);
    outer->addStretch(1);

    pages_ = new QStackedWidget;
    pages_->setObjectName(QStringLiteral("MissionLibraryPages"));
    card->body()->addWidget(pages_);

    // 목록에서는 미션을 만들거나, 저장된 미션 한 줄에서 관리한다.
    auto *listPage = new QWidget;
    auto *listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(metrics::s2);
    auto *listHeader = new QHBoxLayout;
    listHeader->addWidget(sectionLabel(QStringLiteral("저장된 미션")));
    listHeader->addStretch(1);
    new_ = new QPushButton(QStringLiteral("새 미션"));
    new_->setProperty("variant", "primary");
    new_->setProperty("size", "sm");
    listHeader->addWidget(new_);
    auto *refresh = new IconButton(IconButton::Glyph::Refresh);
    refresh->setObjectName(QStringLiteral("MissionRefresh"));
    refresh->setAccessibleName(QStringLiteral("미션 목록 새로고침"));
    refresh->setToolTip(QStringLiteral("미션 목록 새로고침"));
    listHeader->addWidget(refresh);
    listLayout->addLayout(listHeader);
    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("MissionList"));
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listLayout->addWidget(list_);
    empty_ = new QLabel(QStringLiteral("저장된 미션 없음"));
    empty_->setObjectName(QStringLiteral("MissionEmpty"));
    listLayout->addWidget(empty_);
    listLayout->addStretch(1);
    pages_->addWidget(listPage);

    // 작성과 수정은 목록과 별도 화면이다. 목록 선택만으로 편집을 시작하지 않는다.
    auto *editorPage = new QWidget;
    editorPage->setObjectName(QStringLiteral("MissionEditor"));
    auto *editorLayout = new QVBoxLayout(editorPage);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    editorLayout->setSpacing(metrics::s2);
    editorTitle_ = sectionLabel(QStringLiteral("새 미션"));
    editorTitle_->setObjectName(QStringLiteral("MissionEditorTitle"));
    editorLayout->addWidget(editorTitle_);
    auto *nameForm = new QFormLayout;
    name_ = new QLineEdit;
    name_->setObjectName(QStringLiteral("MissionName"));
    name_->setPlaceholderText(QStringLiteral("예: 차량 하부 점검"));
    name_->setMaxLength(120);
    nameForm->addRow(QStringLiteral("미션 이름"), name_);
    editorLayout->addLayout(nameForm);

    auto *stepsHeader = new QHBoxLayout;
    stepsHeader->addWidget(sectionLabel(QStringLiteral("작업 순서")));
    stepsHeader->addStretch(1);
    addStep_ = new QPushButton(QStringLiteral("단계 추가"));
    addStep_->setProperty("size", "sm");
    stepsHeader->addWidget(addStep_);
    editorLayout->addLayout(stepsHeader);
    steps_ = new QListWidget;
    steps_->setObjectName(QStringLiteral("MissionSteps"));
    steps_->setSelectionMode(QAbstractItemView::NoSelection);
    steps_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    editorLayout->addWidget(steps_);
    auto *editorActions = new QHBoxLayout;
    save_ = new QPushButton(QStringLiteral("저장"));
    save_->setProperty("variant", "primary");
    cancel_ = new QPushButton(QStringLiteral("취소"));
    editorActions->addWidget(save_, 1);
    editorActions->addWidget(cancel_);
    editorLayout->addLayout(editorActions);
    editorLayout->addStretch(1);
    pages_->addWidget(editorPage);

    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("MissionLibraryStatus"));
    status_->setWordWrap(true);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    status_->hide();
    card->body()->addWidget(status_);

    connect(refresh, &QPushButton::clicked, this, &MissionLibraryPanel::missionsRequested);
    connect(new_, &QPushButton::clicked, this, &MissionLibraryPanel::startNew);
    connect(cancel_, &QPushButton::clicked, this, [this] {
        closeEditor();
        setStatus({});
    });
    connect(addStep_, &QPushButton::clicked, this, [this] {
        if (!editing_ || !canEdit())
            return;
        QSet<QString> ids;
        for (const auto &step : std::as_const(draftSteps_))
            ids.insert(step.id);
        int next = 1;
        while (ids.contains(QStringLiteral("step-%1").arg(next)))
            ++next;
        draftSteps_.append({QStringLiteral("step-%1").arg(next),
                            QStringLiteral("navigate"), {}, {}});
        rebuildSteps();
    });
    connect(save_, &QPushButton::clicked, this, [this] {
        if (!editing_ || !canEdit() || !pendingSaveId_.isEmpty())
            return;
        if (name_->text().trimmed().isEmpty() || draftSteps_.isEmpty()) {
            setStatus(QStringLiteral("미션 이름과 작업을 하나 이상 입력하십시오."));
            return;
        }
        if (name_->text().trimmed().toUtf8().size() > 120) {
            setStatus(QStringLiteral("미션 이름이 너무 깁니다."));
            return;
        }
        QSet<QString> ids;
        for (int i = 0; i < draftSteps_.size(); ++i) {
            const auto &step = draftSteps_.at(i);
            if (step.id.isEmpty() || ids.contains(step.id)) {
                setStatus(QStringLiteral("작업 ID가 비었거나 중복되었습니다."));
                return;
            }
            ids.insert(step.id);
            if (step.type != QLatin1String("dock") && step.reference.trimmed().isEmpty()) {
                setStatus(QStringLiteral("%1번 작업의 대상을 선택하십시오.").arg(i + 1));
                return;
            }
            if (step.type == QLatin1String("capture") &&
                !validReferenceId(step.reference.trimmed())) {
                setStatus(QStringLiteral("촬영 프리셋 ID는 영문·숫자·-·_만 사용할 수 있습니다."));
                return;
            }
            if (step.type == QLatin1String("dock") && i + 1 != draftSteps_.size()) {
                setStatus(QStringLiteral("충전소 복귀는 마지막 작업이어야 합니다."));
                return;
            }
        }
        pendingSaveId_ = editingId_;
        updateControls();
        setStatus(QStringLiteral("저장 중…"));
        emit saveRequested(currentMission(), revisionValue_);
    });
    updateControls();
}

bool MissionLibraryPanel::canEdit() const
{
    return !missionBusy_ && !mapId_.isEmpty() && mapId_ != QLatin1String("live");
}

void MissionLibraryPanel::startNew()
{
    if (!canEdit())
        return;
    editing_ = true;
    editingOriginal_.clear();
    editingId_ = QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
    revisionValue_ = 0;
    draftSteps_.clear();
    name_->clear();
    editorTitle_->setText(QStringLiteral("새 미션"));
    rebuildSteps();
    pages_->setCurrentIndex(1);
    setStatus({});
    updateControls();
    name_->setFocus();
}

void MissionLibraryPanel::startEdit(const QVariantMap &mission)
{
    if (!canEdit() || !pendingSaveId_.isEmpty())
        return;
    editing_ = true;
    editingOriginal_ = mission;
    editingId_ = mission.value(QStringLiteral("id")).toString();
    revisionValue_ = mission.value(QStringLiteral("revision")).toULongLong();
    name_->setText(mission.value(QStringLiteral("name")).toString());
    editorTitle_->setText(QStringLiteral("미션 수정"));
    draftSteps_.clear();
    for (const auto &value : mission.value(QStringLiteral("steps")).toList()) {
        const QVariantMap step = value.toMap();
        draftSteps_.append({step.value(QStringLiteral("id")).toString(),
                            step.value(QStringLiteral("type")).toString(),
                            stepReference(step), step});
    }
    rebuildSteps();
    pages_->setCurrentIndex(1);
    setStatus({});
    updateControls();
}

void MissionLibraryPanel::closeEditor()
{
    editing_ = false;
    editingId_.clear();
    editingOriginal_.clear();
    draftSteps_.clear();
    revisionValue_ = 0;
    pendingSaveId_.clear();
    name_->clear();
    steps_->clear();
    pages_->setCurrentIndex(0);
    updateControls();
}

void MissionLibraryPanel::setMapId(const QString &mapId)
{
    if (mapId_ == mapId)
        return;
    mapId_ = mapId;
    missions_.clear();
    closeEditor();
    refreshList();
    setStatus(mapId_.isEmpty() || mapId_ == QLatin1String("live")
        ? QStringLiteral("저장된 지도를 선택하십시오.") : QString());
}

void MissionLibraryPanel::setMissions(const QList<QVariantMap> &missions)
{
    missions_ = missions;
    if (editing_ && !pendingSaveId_.isEmpty()) {
        const auto saved = std::find_if(missions_.cbegin(), missions_.cend(), [this](const QVariantMap &m) {
            return m.value(QStringLiteral("id")).toString() == pendingSaveId_ &&
                   !m.value(QStringLiteral("archived")).toBool() &&
                   m.value(QStringLiteral("revision")).toULongLong() > revisionValue_;
        });
        if (saved != missions_.cend()) {
            closeEditor();
            setStatus(QStringLiteral("로봇 저장 완료"));
        }
    } else if (editing_ && revisionValue_ > 0) {
        const bool exists = std::any_of(missions_.cbegin(), missions_.cend(), [this](const QVariantMap &m) {
            return m.value(QStringLiteral("id")).toString() == editingId_ &&
                   !m.value(QStringLiteral("archived")).toBool();
        });
        if (!exists) {
            closeEditor();
            setStatus(QStringLiteral("미션이 목록에서 삭제되었습니다."));
        }
    }
    // 새로고침은 작성 중인 초안을 지우지 않는다.
    refreshList();
}

void MissionLibraryPanel::setWaypoints(const QList<QVariantMap> &waypoints)
{
    waypoints_ = waypoints;
    if (editing_)
        rebuildSteps();
}

void MissionLibraryPanel::setArmPosePresets(const QList<QVariantMap> &presets)
{
    armPosePresets_ = presets;
    if (editing_)
        rebuildSteps();
}

void MissionLibraryPanel::setMissionState(hmi::robot::MissionState state)
{
    missionBusy_ = state != hmi::robot::MissionState::Idle &&
                   state != hmi::robot::MissionState::Completed &&
                   state != hmi::robot::MissionState::Failed;
    updateControls();
    refreshList();
}

void MissionLibraryPanel::setStatus(const QString &message)
{
    status_->setText(message);
    status_->setVisible(!message.isEmpty());
}

void MissionLibraryPanel::handleCommandResult(const QString &channel, bool ok,
                                              const QString &code, const QString &message)
{
    if (channel == QLatin1String(hmi::ch::kCmdMissionsSave)) {
        if (!ok)
            pendingSaveId_.clear();
        setStatus(ok ? QStringLiteral("로봇 저장 완료")
                     : QStringLiteral("저장 실패 · %1 %2").arg(code, message));
        if (ok)
            emit missionsRequested();
        updateControls();
    } else if (channel == QLatin1String(hmi::ch::kCmdMissionsArchive)) {
        setStatus(ok ? QStringLiteral("미션 삭제됨")
                     : QStringLiteral("삭제 실패 · %1 %2").arg(code, message));
        if (ok)
            emit missionsRequested();
    } else if (channel == QLatin1String(hmi::ch::kCmdMissionStart)) {
        setStatus(ok ? QStringLiteral("미션 실행 접수")
                     : QStringLiteral("실행 실패 · %1 %2").arg(code, message));
    }
}

QVariantMap MissionLibraryPanel::currentMission() const
{
    QVariantMap mission = editingOriginal_;
    mission.remove(QStringLiteral("revision"));
    mission.remove(QStringLiteral("archived"));
    QVariantList steps;
    for (const auto &draft : draftSteps_) {
        QVariantMap step = draft.original;
        if (step.value(QStringLiteral("type")).toString() != draft.type)
            step.clear();
        step[QStringLiteral("id")] = draft.id;
        step[QStringLiteral("type")] = draft.type;
        step.remove(QStringLiteral("location_id"));
        step.remove(QStringLiteral("pose"));
        step.remove(QStringLiteral("preset"));
        step.remove(QStringLiteral("arm_pose_id"));
        step.remove(QStringLiteral("capture_preset_id"));
        if (draft.type == QLatin1String("navigate"))
            step[QStringLiteral("location_id")] = draft.reference;
        else if (draft.type == QLatin1String("arm_move"))
            step[QStringLiteral("pose")] = draft.reference;
        else if (draft.type == QLatin1String("capture"))
            step[QStringLiteral("preset")] = draft.reference.trimmed();
        steps << step;
    }
    mission[QStringLiteral("id")] = editingId_;
    mission[QStringLiteral("name")] = name_->text().trimmed();
    mission.remove(QStringLiteral("map_id"));
    mission[QStringLiteral("steps")] = steps;
    return mission;
}

void MissionLibraryPanel::refreshList()
{
    const QSignalBlocker blocker(list_);
    list_->clear();
    int listHeight = 4;
    for (const auto &mission : missions_) {
        if (mission.value(QStringLiteral("archived")).toBool())
            continue;
        const QString id = mission.value(QStringLiteral("id")).toString();
        const QString name = mission.value(QStringLiteral("name")).toString();
        auto *item = new QListWidgetItem(list_);
        item->setData(Qt::UserRole, mission);
        auto *row = new QWidget(list_);
        row->setObjectName(QStringLiteral("MissionSavedRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(metrics::s2, metrics::s1, metrics::s2, metrics::s1);
        layout->setSpacing(0);
        auto *top = new QHBoxLayout;
        top->setSpacing(metrics::s1);
        auto *title = new QLabel(name);
        title->setObjectName(QStringLiteral("MissionSavedName"));
        title->setToolTip(name);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        top->addWidget(title, 1);
        auto *edit = new IconButton(IconButton::Glyph::Edit);
        edit->setObjectName(QStringLiteral("MissionEdit_%1").arg(id));
        edit->setAccessibleName(QStringLiteral("%1 수정").arg(name));
        edit->setToolTip(QStringLiteral("미션 수정"));
        edit->setFixedSize(28, 28);
        edit->setEnabled(canEdit());
        top->addWidget(edit);
        auto *run = new QPushButton(QStringLiteral("실행"));
        run->setObjectName(QStringLiteral("MissionRun_%1").arg(id));
        run->setProperty("size", "sm");
        const QString reason = runBlockReason(mission);
        run->setEnabled(!missionBusy_ && reason.isEmpty());
        run->setToolTip(missionBusy_ ? QStringLiteral("미션 실행 중") : reason);
        top->addWidget(run);
        auto *remove = new IconButton(IconButton::Glyph::Trash);
        remove->setObjectName(QStringLiteral("MissionDelete_%1").arg(id));
        remove->setAccessibleName(QStringLiteral("%1 삭제").arg(name));
        remove->setToolTip(QStringLiteral("미션 삭제"));
        remove->setFixedSize(28, 28);
        remove->setEnabled(canEdit());
        top->addWidget(remove);
        layout->addLayout(top);
        auto *summary = new QLabel(QStringLiteral("%1단계%2")
            .arg(mission.value(QStringLiteral("steps")).toList().size())
            .arg(reason.isEmpty() ? QString() : QStringLiteral(" · 실행 불가")));
        summary->setObjectName(QStringLiteral("MissionSavedSummary"));
        summary->setToolTip(reason);
        layout->addWidget(summary);
        item->setSizeHint(row->sizeHint());
        list_->setItemWidget(item, row);
        listHeight += item->sizeHint().height();
        connect(edit, &QPushButton::clicked, this, [this, mission] { startEdit(mission); });
        connect(run, &QPushButton::clicked, this, [this, mission] {
            if (missionBusy_ || !runBlockReason(mission).isEmpty())
                return;
            emit runRequested(mission.value(QStringLiteral("id")).toString());
        });
        connect(remove, &QPushButton::clicked, this, [this, id, name, mission] {
            if (!canEdit())
                return;
            if (QMessageBox::question(this, QStringLiteral("미션 삭제"),
                    QStringLiteral("'%1' 미션을 삭제하시겠습니까?").arg(name),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            setStatus(QStringLiteral("삭제 중…"));
            emit archiveRequested(id, mission.value(QStringLiteral("revision")).toULongLong());
        });
    }
    list_->setVisible(list_->count() > 0);
    empty_->setVisible(list_->count() == 0);
    if (list_->count() > 0)
        list_->setFixedHeight(std::min(280, listHeight));
}

void MissionLibraryPanel::updateStepTarget(int index, const QString &type)
{
    auto *row = index < steps_->count() ? steps_->itemWidget(steps_->item(index)) : nullptr;
    auto *target = row ? row->findChild<QComboBox *>(QStringLiteral("MissionStepTarget")) : nullptr;
    auto *none = row ? row->findChild<QLabel *>(QStringLiteral("MissionStepNoTarget")) : nullptr;
    if (!target || !none || index >= draftSteps_.size())
        return;
    const QSignalBlocker blocker(target);
    target->clear();
    target->setEditable(type == QLatin1String("capture"));
    target->setVisible(type != QLatin1String("dock"));
    none->setVisible(type == QLatin1String("dock"));
    if (type == QLatin1String("dock"))
        return;
    const QString reference = draftSteps_.at(index).reference;
    if (type == QLatin1String("capture")) {
        target->lineEdit()->setPlaceholderText(QStringLiteral("촬영 프리셋 ID"));
        target->setCurrentText(reference);
        return;
    }
    const QList<QVariantMap> *assets = type == QLatin1String("navigate") ? &waypoints_
                                    : type == QLatin1String("arm_move") ? &armPosePresets_
                                    : nullptr;
    if (!assets) {
        target->setEditable(true);
        target->setCurrentText(reference);
        return;
    }
    target->addItem(type == QLatin1String("navigate")
        ? QStringLiteral("웨이포인트 선택") : QStringLiteral("로봇팔 자세 선택"), QString());
    for (const auto &asset : *assets) {
        if (asset.value(QStringLiteral("archived")).toBool())
            continue;
        const QString id = asset.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            target->addItem(asset.value(QStringLiteral("name"), id).toString(), id);
    }
    int selected = target->findData(reference);
    if (selected < 0 && !reference.isEmpty()) {
        target->addItem(QStringLiteral("없어진 대상 · %1").arg(reference), reference);
        selected = target->count() - 1;
    }
    target->setCurrentIndex(std::max(0, selected));
}

void MissionLibraryPanel::rebuildSteps()
{
    const QSignalBlocker blocker(steps_);
    steps_->clear();
    int listHeight = 4;
    for (int index = 0; index < draftSteps_.size(); ++index) {
        const auto &draft = draftSteps_.at(index);
        auto *item = new QListWidgetItem(steps_);
        auto *row = new QWidget(steps_);
        row->setObjectName(QStringLiteral("MissionStepRow"));
        row->setAttribute(Qt::WA_StyledBackground, true);
        auto *layout = new QVBoxLayout(row);
        layout->setContentsMargins(metrics::s2, metrics::s1, metrics::s2, metrics::s1);
        layout->setSpacing(metrics::s1);
        auto *top = new QHBoxLayout;
        top->setSpacing(metrics::s1);
        auto *number = new QLabel(QString::number(index + 1));
        number->setObjectName(QStringLiteral("MissionStepNumber"));
        number->setFixedWidth(18);
        top->addWidget(number);
        auto *type = new QComboBox;
        type->setObjectName(QStringLiteral("MissionStepType_%1").arg(index));
        type->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        type->addItem(QStringLiteral("주행"), QStringLiteral("navigate"));
        type->addItem(QStringLiteral("촬영"), QStringLiteral("capture"));
        type->addItem(QStringLiteral("로봇팔"), QStringLiteral("arm_move"));
        type->addItem(QStringLiteral("충전소 복귀"), QStringLiteral("dock"));
        int typeIndex = type->findData(draft.type);
        if (typeIndex < 0) {
            type->addItem(QStringLiteral("알 수 없는 작업"), draft.type);
            typeIndex = type->count() - 1;
        }
        type->setCurrentIndex(typeIndex);
        top->addWidget(type, 1);
        const auto smallButton = [row](const QString &text, const QString &hint,
                                       const QString &objectName) {
            auto *button = new QPushButton(text, row);
            button->setFixedSize(28, 28);
            button->setToolTip(hint);
            button->setAccessibleName(hint);
            button->setObjectName(objectName);
            return button;
        };
        auto *up = smallButton(QStringLiteral("↑"), QStringLiteral("위로 이동"),
                               QStringLiteral("MissionStepUp_%1").arg(index));
        auto *down = smallButton(QStringLiteral("↓"), QStringLiteral("아래로 이동"),
                                 QStringLiteral("MissionStepDown_%1").arg(index));
        auto *remove = new IconButton(IconButton::Glyph::Trash, row);
        remove->setFixedSize(28, 28);
        remove->setToolTip(QStringLiteral("작업 삭제"));
        remove->setAccessibleName(QStringLiteral("%1번 작업 삭제").arg(index + 1));
        remove->setObjectName(QStringLiteral("MissionStepDelete_%1").arg(index));
        up->setEnabled(index > 0 && canEdit());
        down->setEnabled(index + 1 < draftSteps_.size() && canEdit());
        remove->setEnabled(canEdit());
        top->addWidget(up);
        top->addWidget(down);
        top->addWidget(remove);
        layout->addLayout(top);
        auto *target = new QComboBox;
        target->setObjectName(QStringLiteral("MissionStepTarget"));
        target->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        layout->addWidget(target);
        auto *none = new QLabel(QStringLiteral("대상 선택 없음"));
        none->setObjectName(QStringLiteral("MissionStepNoTarget"));
        layout->addWidget(none);
        item->setSizeHint(row->sizeHint());
        steps_->setItemWidget(item, row);
        updateStepTarget(index, draft.type);
        item->setSizeHint(row->sizeHint());
        listHeight += item->sizeHint().height();
        connect(type, &QComboBox::currentIndexChanged, this, [this, index, type] {
            draftSteps_[index].type = type->currentData().toString();
            draftSteps_[index].reference.clear();
            updateStepTarget(index, draftSteps_[index].type);
        });
        connect(target, &QComboBox::currentTextChanged, this, [this, index, target] {
            if (index >= draftSteps_.size())
                return;
            const QString type = draftSteps_[index].type;
            draftSteps_[index].reference = type == QLatin1String("capture") ||
                (type != QLatin1String("navigate") && type != QLatin1String("arm_move") &&
                 type != QLatin1String("dock"))
                ? target->currentText() : target->currentData().toString();
        });
        connect(up, &QPushButton::clicked, this, [this, index] { moveStep(index, -1); });
        connect(down, &QPushButton::clicked, this, [this, index] { moveStep(index, 1); });
        connect(remove, &QPushButton::clicked, this, [this, index] {
            if (!canEdit() || index >= draftSteps_.size())
                return;
            draftSteps_.removeAt(index);
            rebuildSteps();
        });
    }
    steps_->setVisible(!draftSteps_.isEmpty());
    if (!draftSteps_.isEmpty())
        steps_->setFixedHeight(std::min(360, listHeight));
    updateControls();
}

void MissionLibraryPanel::moveStep(int index, int delta)
{
    const int next = index + delta;
    if (!canEdit() || index < 0 || next < 0 || next >= draftSteps_.size())
        return;
    std::swap(draftSteps_[index], draftSteps_[next]);
    rebuildSteps();
}

QString MissionLibraryPanel::runBlockReason(const QVariantMap &mission) const
{
    if (mission.isEmpty() || mission.value(QStringLiteral("archived")).toBool() ||
        mission.value(QStringLiteral("revision")).toULongLong() == 0)
        return QStringLiteral("저장된 미션만 실행할 수 있습니다.");
    bool hasNavigate = false;
    const auto steps = mission.value(QStringLiteral("steps")).toList();
    for (int i = 0; i < steps.size(); ++i) {
        const auto step = steps.at(i).toMap();
        const QString type = step.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("capture") || type == QLatin1String("arm_move"))
            return QStringLiteral("촬영·팔 작업 실행기는 아직 연결되지 않았습니다.");
        if (type == QLatin1String("navigate")) {
            hasNavigate = true;
            if (step.value(QStringLiteral("location_id")).toString().isEmpty())
                return QStringLiteral("주행할 웨이포인트가 지정되지 않았습니다.");
        } else if (type == QLatin1String("dock")) {
            if (i + 1 != steps.size())
                return QStringLiteral("충전소 복귀는 마지막 작업이어야 합니다.");
        } else {
            return QStringLiteral("지원하지 않는 작업이 있습니다.");
        }
    }
    return hasNavigate ? QString() : QStringLiteral("실행할 주행 작업이 없습니다.");
}

void MissionLibraryPanel::updateControls()
{
    new_->setEnabled(canEdit());
    addStep_->setEnabled(editing_ && canEdit() && pendingSaveId_.isEmpty());
    name_->setEnabled(editing_ && canEdit() && pendingSaveId_.isEmpty());
    steps_->setEnabled(editing_ && canEdit() && pendingSaveId_.isEmpty());
    save_->setEnabled(editing_ && canEdit() && pendingSaveId_.isEmpty());
    cancel_->setEnabled(editing_ && pendingSaveId_.isEmpty());
}

}  // namespace hmi::ui
