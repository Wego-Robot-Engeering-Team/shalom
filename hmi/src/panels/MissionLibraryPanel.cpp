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
#include <QEvent>
#include <QFormLayout>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QUuid>

#include <algorithm>
#include <utility>

namespace hmi::ui {

using namespace hmi::theme;

namespace {

class MissionPages final : public QStackedWidget {
public:
    MissionPages()
    {
        layout()->setSizeConstraint(QLayout::SetNoConstraint);
        connect(this, &QStackedWidget::currentChanged, this, [this] { updateGeometry(); });
    }

    QSize sizeHint() const override
    {
        return currentWidget() ? currentWidget()->sizeHint() : QSize();
    }

    QSize minimumSizeHint() const override
    {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QSize();
    }
};

class MissionNameLabel final : public QLabel {
public:
    explicit MissionNameLabel(const QString &name) : QLabel(name), name_(name)
    {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setToolTip(name);
        setAccessibleName(name);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(name_, Qt::ElideRight, contentsRect().width()));
    }

    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            setText(fontMetrics().elidedText(name_, Qt::ElideRight, contentsRect().width()));
    }

private:
    QString name_;
};

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

    pages_ = new MissionPages;
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
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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
    nameForm->setRowWrapPolicy(QFormLayout::WrapAllRows);
    nameForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    name_ = new QLineEdit;
    name_->setObjectName(QStringLiteral("MissionName"));
    name_->setPlaceholderText(QStringLiteral("예: 차량 하부 점검"));
    name_->setMaxLength(120);
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
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
    steps_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    editorLayout->addWidget(steps_);
    stepsEmpty_ = new QLabel(QStringLiteral("단계를 추가하십시오."));
    stepsEmpty_->setObjectName(QStringLiteral("MissionStepsEmpty"));
    editorLayout->addWidget(stepsEmpty_);
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
    status_->setTextFormat(Qt::PlainText);
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
        if (!editing_ || !canEdit() || !pendingSaveId_.isEmpty() || saveResultOutstanding_)
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
            if (step.type != QLatin1String("navigate") && step.type != QLatin1String("capture") &&
                step.type != QLatin1String("arm_move") && step.type != QLatin1String("dock")) {
                setStatus(QStringLiteral("%1번 작업의 종류를 다시 선택하십시오.").arg(i + 1));
                return;
            }
            if (step.type != QLatin1String("dock") && step.reference.trimmed().isEmpty()) {
                setStatus(QStringLiteral("%1번 작업의 대상을 선택하십시오.").arg(i + 1));
                return;
            }
            if (step.type == QLatin1String("navigate") && std::none_of(
                    waypoints_.cbegin(), waypoints_.cend(), [&step](const QVariantMap &point) {
                        return point.value(QStringLiteral("id")).toString() == step.reference &&
                               !point.value(QStringLiteral("archived")).toBool();
                    })) {
                setStatus(QStringLiteral("%1번 작업의 웨이포인트가 없습니다. 다시 선택하십시오.").arg(i + 1));
                return;
            }
            if (step.type == QLatin1String("capture") &&
                !validReferenceId(step.reference.trimmed())) {
                setStatus(QStringLiteral("촬영 프리셋 ID는 영문·숫자·-·_만 사용할 수 있습니다."));
                return;
            }
            if (step.type == QLatin1String("arm_move") && std::none_of(
                    armPosePresets_.cbegin(), armPosePresets_.cend(), [&step](const QVariantMap &pose) {
                        return pose.value(QStringLiteral("id")).toString() == step.reference &&
                               !pose.value(QStringLiteral("archived")).toBool();
                    })) {
                setStatus(QStringLiteral("%1번 작업의 팔 자세가 없습니다. 다시 선택하십시오.").arg(i + 1));
                return;
            }
            if (step.type == QLatin1String("dock") && i + 1 != draftSteps_.size()) {
                setStatus(QStringLiteral("충전소 복귀는 마지막 작업이어야 합니다."));
                return;
            }
        }
        pendingSaveId_ = editingId_;
        pendingMission_ = currentMission();
        pendingSaveAccepted_ = false;
        saveResultOutstanding_ = true;
        const quint64 generation = ++pendingSaveGeneration_;
        updateControls();
        setStatus(QStringLiteral("저장 중…"));
        QTimer::singleShot(7000, this, [this, generation] {
            if (pendingSaveGeneration_ != generation ||
                (pendingSaveId_.isEmpty() && !saveResultOutstanding_))
                return;
            const bool currentContext = !pendingSaveId_.isEmpty();
            pendingSaveId_.clear();
            pendingMission_.clear();
            pendingSaveAccepted_ = false;
            saveResultOutstanding_ = false;
            if (currentContext)
                setStatus(QStringLiteral("저장 결과를 확인하지 못했습니다. 목록을 새로 고친 뒤 확인하십시오."));
            updateControls();
        });
        const QVariantMap submitted = pendingMission_;
        emit saveRequested(submitted, revisionValue_);
    });
    updateControls();
}

bool MissionLibraryPanel::canEdit() const
{
    return editingEnabled_ && !missionBusy_ && !mapId_.isEmpty() && mapId_ != QLatin1String("live");
}

void MissionLibraryPanel::startNew()
{
    if (!canEdit() || !pendingSaveId_.isEmpty() || !pendingArchiveId_.isEmpty())
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
    if (!canEdit() || !pendingSaveId_.isEmpty() || !pendingArchiveId_.isEmpty())
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
    pendingMission_.clear();
    pendingSaveAccepted_ = false;
    // A map switch can close the editor while the save reply is still in flight.
    // Keep saveResultOutstanding_ until that reply or its timeout is consumed.
    name_->clear();
    steps_->clear();
    pages_->setCurrentIndex(0);
    updateControls();
}

void MissionLibraryPanel::setMapId(const QString &mapId)
{
    if (mapId_ == mapId)
        return;
    ++contextGeneration_;
    pendingArchiveId_.clear();
    pendingArchiveAccepted_ = false;
    // Results carry a channel only. Keep the old request reserved until its
    // reply arrives, so it cannot be mistaken for a delete on the new map.
    if (!archiveResultOutstanding_)
        ++pendingArchiveGeneration_;
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
    confirmPendingArchive();
    if (editing_ && !pendingSaveId_.isEmpty()) {
        confirmPendingSave();
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
    // Repeated catalog reports must not replace the focused draft controls.
    if (waypoints_ == waypoints)
        return;
    waypoints_ = waypoints;
    if (editing_)
        rebuildSteps();
}

void MissionLibraryPanel::setArmPosePresets(const QList<QVariantMap> &presets)
{
    if (armPosePresets_ == presets)
        return;
    armPosePresets_ = presets;
    if (editing_)
        rebuildSteps();
}

void MissionLibraryPanel::setMissionState(hmi::robot::MissionState state)
{
    const bool busy = state != hmi::robot::MissionState::Idle &&
                      state != hmi::robot::MissionState::Completed &&
                      state != hmi::robot::MissionState::Failed;
    if (missionBusy_ == busy)
        return;
    missionBusy_ = busy;
    updateControls();
    refreshList();
}

void MissionLibraryPanel::setEditingEnabled(bool enabled)
{
    if (editingEnabled_ == enabled)
        return;
    if (!enabled)
        ++contextGeneration_;
    editingEnabled_ = enabled;
    updateControls();
    refreshList();
}

void MissionLibraryPanel::setExecutionEnabled(bool enabled, const QString &reason)
{
    if (executionEnabled_ == enabled && executionReason_ == reason)
        return;
    executionEnabled_ = enabled;
    executionReason_ = reason;
    refreshList();
}

void MissionLibraryPanel::setStatus(const QString &message)
{
    status_->setText(message);
    status_->setVisible(!message.isEmpty());
}

bool MissionLibraryPanel::confirmPendingSave()
{
    if (!editing_ || pendingSaveId_.isEmpty() || !pendingSaveAccepted_)
        return false;
    const auto saved = std::find_if(missions_.cbegin(), missions_.cend(), [this](QVariantMap mission) {
        if (mission.value(QStringLiteral("id")).toString() != pendingSaveId_ ||
            mission.value(QStringLiteral("archived")).toBool() ||
            mission.value(QStringLiteral("revision")).toULongLong() != revisionValue_ + 1)
            return false;
        mission.remove(QStringLiteral("revision"));
        mission.remove(QStringLiteral("archived"));
        mission.remove(QStringLiteral("map_id"));
        return mission == pendingMission_;
    });
    if (saved == missions_.cend())
        return false;
    closeEditor();
    setStatus(QStringLiteral("로봇 저장 완료"));
    return true;
}

bool MissionLibraryPanel::confirmPendingArchive()
{
    if (pendingArchiveId_.isEmpty() || !pendingArchiveAccepted_)
        return false;
    const bool stillPresent = std::any_of(missions_.cbegin(), missions_.cend(), [this](const QVariantMap &mission) {
        return mission.value(QStringLiteral("id")).toString() == pendingArchiveId_ &&
               !mission.value(QStringLiteral("archived")).toBool();
    });
    if (stillPresent)
        return false;
    pendingArchiveId_.clear();
    pendingArchiveAccepted_ = false;
    setStatus(QStringLiteral("미션 삭제됨"));
    updateControls();
    return true;
}

void MissionLibraryPanel::handleCommandResult(const QString &channel, bool ok,
                                              const QString &code, const QString &message)
{
    if (channel == QLatin1String(hmi::ch::kCmdMissionsSave)) {
        if (!saveResultOutstanding_)
            return;
        saveResultOutstanding_ = false;
        if (pendingSaveId_.isEmpty()) {
            updateControls();
            return;
        }
        if (!ok) {
            pendingSaveId_.clear();
            pendingMission_.clear();
            pendingSaveAccepted_ = false;
            setStatus(QStringLiteral("저장 실패 · %1 %2").arg(code, message));
        } else {
            pendingSaveAccepted_ = true;
            if (confirmPendingSave())
                refreshList();
            else
                setStatus(QStringLiteral("저장 확인 중…"));
            emit missionsRequested();
        }
        updateControls();
    } else if (channel == QLatin1String(hmi::ch::kCmdMissionsArchive)) {
        if (!archiveResultOutstanding_)
            return;
        archiveResultOutstanding_ = false;
        if (pendingArchiveId_.isEmpty()) {
            refreshList();
            return;
        }
        if (ok) {
            pendingArchiveAccepted_ = true;
            if (!confirmPendingArchive())
                setStatus(QStringLiteral("삭제 확인 중…"));
            emit missionsRequested();
        } else {
            pendingArchiveId_.clear();
            pendingArchiveAccepted_ = false;
            setStatus(QStringLiteral("삭제 실패 · %1 %2").arg(code, message).trimmed());
        }
        updateControls();
        refreshList();
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
        auto *title = new MissionNameLabel(name);
        title->setObjectName(QStringLiteral("MissionSavedName"));
        title->setToolTip(name);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        top->addWidget(title, 1);
        auto *edit = new IconButton(IconButton::Glyph::Edit);
        edit->setObjectName(QStringLiteral("MissionEdit_%1").arg(id));
        edit->setAccessibleName(QStringLiteral("%1 수정").arg(name));
        edit->setToolTip(QStringLiteral("미션 수정"));
        edit->setFixedSize(28, 28);
        edit->setEnabled(canEdit() && pendingArchiveId_.isEmpty() && pendingSaveId_.isEmpty());
        top->addWidget(edit);
        auto *run = new QPushButton(QStringLiteral("실행"));
        run->setObjectName(QStringLiteral("MissionRun_%1").arg(id));
        run->setProperty("size", "sm");
        const QString reason = runBlockReason(mission);
        run->setEnabled(executionEnabled_ && !missionBusy_ && reason.isEmpty() &&
                        pendingArchiveId_.isEmpty() && pendingSaveId_.isEmpty());
        run->setToolTip(!executionEnabled_ ? executionReason_
                       : missionBusy_ ? QStringLiteral("미션 실행 중") : reason);
        top->addWidget(run);
        auto *remove = new IconButton(IconButton::Glyph::Trash);
        remove->setObjectName(QStringLiteral("MissionDelete_%1").arg(id));
        remove->setAccessibleName(QStringLiteral("%1 삭제").arg(name));
        remove->setToolTip(QStringLiteral("미션 삭제"));
        remove->setFixedSize(28, 28);
        remove->setEnabled(canEdit() && pendingArchiveId_.isEmpty() && pendingSaveId_.isEmpty() &&
                           !archiveResultOutstanding_);
        if (archiveResultOutstanding_ && pendingArchiveId_.isEmpty())
            remove->setToolTip(QStringLiteral("이전 삭제 결과 확인 중"));
        top->addWidget(remove);
        layout->addLayout(top);
        auto *summary = new QLabel(QStringLiteral("%1단계%2")
            .arg(mission.value(QStringLiteral("steps")).toList().size())
            .arg(reason.isEmpty() ? QString() : QStringLiteral(" · 실행 불가")));
        summary->setObjectName(QStringLiteral("MissionSavedSummary"));
        summary->setTextFormat(Qt::PlainText);
        summary->setToolTip(reason);
        layout->addWidget(summary);
        item->setSizeHint(row->sizeHint());
        list_->setItemWidget(item, row);
        listHeight += item->sizeHint().height();
        connect(edit, &QPushButton::clicked, this, [this, mission] { startEdit(mission); });
        connect(run, &QPushButton::clicked, this, [this, mission] {
            if (!executionEnabled_ || missionBusy_ || !runBlockReason(mission).isEmpty() ||
                !pendingArchiveId_.isEmpty() || !pendingSaveId_.isEmpty())
                return;
            emit runRequested(mission.value(QStringLiteral("id")).toString());
        });
        connect(remove, &QPushButton::clicked, this, [this, id, name, mission] {
            if (!canEdit() || !pendingArchiveId_.isEmpty() || !pendingSaveId_.isEmpty() ||
                archiveResultOutstanding_)
                return;
            const QPointer<MissionLibraryPanel> panel(this);
            const quint64 generation = contextGeneration_;
            const QVariantMap target = mission;
            const QString targetId = id;
            auto *confirmation = new QMessageBox(QMessageBox::Question, QStringLiteral("미션 삭제"),
                QStringLiteral("'%1' 미션을 삭제하시겠습니까?").arg(name),
                QMessageBox::Yes | QMessageBox::No, this);
            confirmation->setTextFormat(Qt::PlainText);
            confirmation->setDefaultButton(QMessageBox::No);
            const QPointer<QMessageBox> dialog(confirmation);
            const int answer = confirmation->exec();
            if (dialog)
                delete dialog.data();
            if (answer != QMessageBox::Yes)
                return;
            if (!panel || generation != contextGeneration_ || !canEdit() ||
                !pendingArchiveId_.isEmpty() || !pendingSaveId_.isEmpty() || archiveResultOutstanding_)
                return;
            const auto current = std::find_if(missions_.cbegin(), missions_.cend(),
                [&targetId](const QVariantMap &entry) { return entry.value("id").toString() == targetId; });
            if (current == missions_.cend() || *current != target)
                return;
            pendingArchiveId_ = targetId;
            pendingArchiveAccepted_ = false;
            archiveResultOutstanding_ = true;
            const quint64 archiveGeneration = ++pendingArchiveGeneration_;
            setStatus(QStringLiteral("삭제 중…"));
            updateControls();
            for (auto *action : list_->findChildren<QPushButton *>())
                action->setEnabled(false);
            QTimer::singleShot(7000, this, [this, archiveGeneration] {
                if (pendingArchiveGeneration_ != archiveGeneration ||
                    (pendingArchiveId_.isEmpty() && !archiveResultOutstanding_))
                    return;
                const bool currentContext = !pendingArchiveId_.isEmpty();
                pendingArchiveId_.clear();
                pendingArchiveAccepted_ = false;
                archiveResultOutstanding_ = false;
                if (currentContext)
                    setStatus(QStringLiteral("삭제 결과를 확인하지 못했습니다. 목록을 새로 고쳐 확인하십시오."));
                updateControls();
                refreshList();
            });
            emit archiveRequested(targetId, target.value(QStringLiteral("revision")).toULongLong());
        });
    }
    list_->setVisible(list_->count() > 0);
    empty_->setVisible(list_->count() == 0);
    if (list_->count() > 0)
        list_->setFixedHeight(std::min(280, listHeight));
}

void MissionLibraryPanel::updateStepTarget(int index, const QString &type)
{
    auto *row = index >= 0 && index < steps_->count() ? steps_->itemWidget(steps_->item(index)) : nullptr;
    auto *target = row ? row->findChild<QComboBox *>(QStringLiteral("MissionStepTarget")) : nullptr;
    auto *none = row ? row->findChild<QLabel *>(QStringLiteral("MissionStepNoTarget")) : nullptr;
    if (!target || !none || index < 0 || index >= draftSteps_.size())
        return;
    const QSignalBlocker blocker(target);
    target->clear();
    target->setEditable(type == QLatin1String("capture"));
    target->setVisible(type != QLatin1String("dock"));
    none->setVisible(type == QLatin1String("dock"));
    target->setToolTip({});
    if (type == QLatin1String("dock"))
        return;
    const QString reference = draftSteps_.at(index).reference;
    if (type == QLatin1String("capture")) {
        target->lineEdit()->setPlaceholderText(QStringLiteral("촬영 프리셋 ID"));
        target->lineEdit()->setMaxLength(96);
        target->setCurrentText(reference);
        target->setToolTip(reference);
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
        if (!id.isEmpty()) {
            target->addItem(asset.value(QStringLiteral("name"), id).toString(), id);
            target->setItemData(target->count() - 1,
                asset.value(QStringLiteral("name"), id).toString(), Qt::ToolTipRole);
        }
    }
    int selected = target->findData(reference);
    if (selected < 0 && !reference.isEmpty()) {
        target->addItem(QStringLiteral("없어진 대상 · %1").arg(reference), reference);
        selected = target->count() - 1;
    }
    target->setCurrentIndex(std::max(0, selected));
    target->setToolTip(target->currentText());
}

void MissionLibraryPanel::rebuildSteps()
{
    const QSignalBlocker blocker(steps_);
    steps_->clear();
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
        type->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
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
            button->setProperty("variant", "ghost");
            button->setProperty("compact", true);
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
        target->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        layout->addWidget(target);
        auto *none = new QLabel(QStringLiteral("대상 선택 없음"));
        none->setObjectName(QStringLiteral("MissionStepNoTarget"));
        layout->addWidget(none);
        item->setSizeHint(row->sizeHint());
        steps_->setItemWidget(item, row);
        updateStepTarget(index, draft.type);
        item->setSizeHint(row->sizeHint());
        connect(type, &QComboBox::currentIndexChanged, this, [this, index, type] {
            if (!editing_ || !canEdit() || !pendingSaveId_.isEmpty() || index >= draftSteps_.size())
                return;
            draftSteps_[index].type = type->currentData().toString();
            draftSteps_[index].reference.clear();
            updateStepTarget(index, draftSteps_[index].type);
            resizeStepList();
        });
        connect(target, &QComboBox::currentIndexChanged, this, [this, index, target] {
            if (!editing_ || !canEdit() || !pendingSaveId_.isEmpty() || index >= draftSteps_.size())
                return;
            const QString type = draftSteps_[index].type;
            if (type == QLatin1String("navigate") || type == QLatin1String("arm_move"))
                draftSteps_[index].reference = target->currentData().toString();
            target->setToolTip(target->currentText());
        });
        connect(target, &QComboBox::editTextChanged, this, [this, index, target](const QString &text) {
            if (!editing_ || !canEdit() || !pendingSaveId_.isEmpty() || index >= draftSteps_.size())
                return;
            if (draftSteps_[index].type == QLatin1String("capture"))
                draftSteps_[index].reference = text;
            target->setToolTip(text);
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
    stepsEmpty_->setVisible(draftSteps_.isEmpty());
    resizeStepList();
    updateControls();
}

void MissionLibraryPanel::resizeStepList()
{
    int height = 4;
    for (int index = 0; index < steps_->count(); ++index) {
        auto *item = steps_->item(index);
        auto *row = steps_->itemWidget(item);
        if (!row)
            continue;
        row->layout()->invalidate();
        item->setSizeHint(row->sizeHint());
        height += item->sizeHint().height();
    }
    if (steps_->count() > 0)
        steps_->setFixedHeight(std::min(360, height));
    pages_->updateGeometry();
}

void MissionLibraryPanel::moveStep(int index, int delta)
{
    const int next = index + delta;
    if (!canEdit() || !pendingSaveId_.isEmpty() || index < 0 || next < 0 || next >= draftSteps_.size())
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
    const bool writable = canEdit() && pendingSaveId_.isEmpty() && pendingArchiveId_.isEmpty();
    new_->setEnabled(writable);
    addStep_->setEnabled(editing_ && writable);
    name_->setEnabled(editing_ && writable);
    steps_->setEnabled(editing_ && writable);
    save_->setEnabled(editing_ && writable && !saveResultOutstanding_);
    save_->setToolTip(saveResultOutstanding_ && pendingSaveId_.isEmpty()
        ? QStringLiteral("이전 저장 결과 확인 중")
        : !pendingSaveId_.isEmpty() ? QStringLiteral("로봇 저장 확인 중") : QString());
    cancel_->setEnabled(editing_ && pendingSaveId_.isEmpty());
}

}  // namespace hmi::ui
