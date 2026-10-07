// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/MissionPanel.h"

#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;

MissionPanel::MissionPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("미션 현황"), this);
    state_ = new Badge(QStringLiteral("대기"), QStringLiteral("neutral"));
    card_->addHeaderWidget(state_);
    outer->addWidget(card_);
    reason_ = new QLabel;
    reason_->setObjectName(QStringLiteral("MissionReason"));
    reason_->setTextFormat(Qt::PlainText);
    reason_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    reason_->setWordWrap(true);
    card_->body()->addWidget(reason_);

    card_->body()->addWidget(sectionLabel(QStringLiteral("현재 미션")));
    missionNameLabel_ = new QLabel(QStringLiteral("실행 중인 미션 없음"));
    missionNameLabel_->setObjectName(QStringLiteral("CurrentMissionName"));
    missionNameLabel_->setTextFormat(Qt::PlainText);
    missionNameLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    missionNameLabel_->setWordWrap(true);
    card_->body()->addWidget(missionNameLabel_);

    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    bar_->setValue(0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(metrics::s2);
    card_->body()->addWidget(bar_);

    count_ = readout(QStringLiteral("—"));
    card_->body()->addWidget(count_);

    card_->body()->addSpacing(metrics::s2);

    // 전체 단계 목록은 아래 미션 편집기에 있다. 여기서는 지금과 다음만 말한다.
    card_->body()->addWidget(sectionLabel(QStringLiteral("현재 작업")));
    current_ = new QLabel(QStringLiteral("—"));
    current_->setObjectName(QStringLiteral("MissionCurrentStep"));
    current_->setTextFormat(Qt::PlainText);
    current_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    current_->setWordWrap(true);
    card_->body()->addWidget(current_);

    card_->body()->addSpacing(metrics::s1);
    card_->body()->addWidget(sectionLabel(QStringLiteral("다음 작업")));
    next_ = new QLabel(QStringLiteral("—"));
    next_->setObjectName(QStringLiteral("Hint"));
    next_->setTextFormat(Qt::PlainText);
    next_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    next_->setWordWrap(true);
    card_->body()->addWidget(next_);

    card_->body()->addSpacing(metrics::s1);
    card_->body()->addWidget(sectionLabel(QStringLiteral("다음 미션")));
    nextMission_ = new QLabel(QStringLiteral("예약된 미션 없음"));
    nextMission_->setObjectName(QStringLiteral("NextMissionName"));
    nextMission_->setTextFormat(Qt::PlainText);
    nextMission_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    nextMission_->setWordWrap(true);
    card_->body()->addWidget(nextMission_);

    // 조작은 읽은 것 바로 밑에 둔다. 예전에는 카드 바닥까지 밀어 두었는데,
    // 목록을 빼고 나니 읽을 것과 누를 것 사이가 한 뼘 넘게 비었다.
    card_->body()->addSpacing(metrics::s3);
    auto *run = new QHBoxLayout;
    run->setSpacing(metrics::s2);

    // 실행 중 조작인 일시정지·재개만 이 카드에 둔다. 새 미션을 고르고
    // 시작하는 일은 바로 아래 미션 관리에서 한다.
    //
    // 일시정지·재개가 한 줄, 그 밖의 두 가지가 아랫줄이다.
    // 셋을 한 줄에 늘어놓으면 글자가 상자에 걸리고, 무엇이 주된 조작인지도
    // 흐려진다.
    //
    // 아랫줄 둘은 자리를 바꾸지 않는다. 앞서 취소와 복귀가 상태에 따라
    // 같은 칸을 번갈아 쓰게 해 봤더니, 규칙을 설명할 수가 없었다. 둘은
    // 서로의 대체재가 아니다 — 하나는 점검을 끝내는 일이고, 하나는 로봇을
    // 집으로 보내는 일이다.
    run_ = new QPushButton;
    run_->setObjectName(QStringLiteral("MissionPauseResumeButton"));
    run_->setProperty("variant", "primary");
    run_->setProperty("size", "sm");
    run->addWidget(run_);
    card_->body()->addLayout(run);

    auto *aux = new QHBoxLayout;
    aux->setSpacing(metrics::s2);
    stop_ = new QPushButton(QStringLiteral("미션 취소"));
    stop_->setObjectName(QStringLiteral("MissionCancelButton"));
    dock_ = new QPushButton(QStringLiteral("충전소 복귀"));
    dock_->setObjectName(QStringLiteral("MissionDockButton"));
    for (auto *b : {stop_, dock_}) {
        b->setProperty("size", "sm");
        aux->addWidget(b);
    }
    card_->body()->addSpacing(metrics::s2);
    card_->body()->addLayout(aux);

    connect(run_, &QPushButton::clicked, this, &MissionPanel::onRunClicked);
    connect(stop_, &QPushButton::clicked, this, &MissionPanel::confirmStop);
    connect(dock_, &QPushButton::clicked, this, &MissionPanel::returnToDock);

    refresh();
}

void MissionPanel::setProgress(const QString &missionName, int index, int total,
                               const QStringList &stepLabels, const QString &missionId)
{
    if (missionId_ != missionId || missionName_ != missionName || total_ != total ||
        stepLabels_ != stepLabels || index < index_)
        ++missionGeneration_;
    missionId_ = missionId;
    missionName_ = missionName;
    index_ = index;
    total_ = total;
    stepLabels_ = stepLabels;
    refresh();
}

void MissionPanel::setMissionState(const QString &state)
{
    if (missionState_ != state) {
        ++missionGeneration_;
        commandPending_ = false;
    }
    missionState_ = state;
    refresh();
}

void MissionPanel::setMissionDetails(const QString &reason, const QString &detail)
{
    reasonCode_ = reason;
    detail_ = detail;
    refresh();
}

void MissionPanel::setControlAvailability(bool ready, const QString &reason)
{
    controlsReady_ = ready;
    controlReason_ = reason;
    refresh();
}

void MissionPanel::setCommandPending(bool pending)
{
    commandPending_ = pending;
    refresh();
}

void MissionPanel::setDockKnown(bool known)
{
    dockKnown_ = known;
    refresh();
}

void MissionPanel::refresh()
{
    const bool disconnected = missionState_ == QLatin1String("disconnected");
    const bool blocked = missionState_ == QLatin1String("blocked") ||
                         missionState_ == QLatin1String("fault") ||
                         missionState_ == QLatin1String("emergency_stopped");
    const bool failed = missionState_ == QLatin1String("failed");
    const bool returning = missionState_ == QLatin1String("returning");
    const bool active = missionState_ == QLatin1String("ready") ||
                        missionState_ == QLatin1String("running") ||
                        missionState_ == QLatin1String("pausing") ||
                        missionState_ == QLatin1String("paused") ||
                        missionState_ == QLatin1String("recovering") || returning;
    const bool paused = missionState_ == QLatin1String("paused");
    const bool retry = missionState_ == QLatin1String("ready") &&
        reasonCode_ == QLatin1String("MISSION_START_CANCELLED_BY_SAFETY");

    if (disconnected)
        state_->set(QStringLiteral("연결 없음"), QStringLiteral("warn"));
    else if (missionState_ == QLatin1String("emergency_stopped"))
        state_->set(QStringLiteral("비상정지"), QStringLiteral("danger"));
    else if (blocked)
        state_->set(QStringLiteral("오류"), QStringLiteral("danger"));
    else if (failed)
        state_->set(QStringLiteral("실패"), QStringLiteral("danger"));
    else if (paused)
        state_->set(QStringLiteral("일시정지"), QStringLiteral("warn"));
    else if (missionState_ == QLatin1String("ready"))
        state_->set(retry ? QStringLiteral("시작 취소됨") : QStringLiteral("시작 준비"),
                    retry ? QStringLiteral("warn") : QStringLiteral("info"));
    else if (missionState_ == QLatin1String("pausing"))
        state_->set(QStringLiteral("정지 확인 중"), QStringLiteral("warn"));
    else if (missionState_ == QLatin1String("recovering"))
        state_->set(QStringLiteral("재개 확인 중"), QStringLiteral("info"));
    else if (returning)
        state_->set(QStringLiteral("복귀 중"), QStringLiteral("info"));
    else if (missionState_ == QLatin1String("completed"))
        state_->set(QStringLiteral("완료"), QStringLiteral("ok"));
    else if (active)
        state_->set(QStringLiteral("점검 중"), QStringLiteral("info"));
    else
        state_->set(QStringLiteral("대기"), QStringLiteral("neutral"));

    run_->setText(retry ? QStringLiteral("미션 다시 시작")
                        : paused ? QStringLiteral("미션 재개") : QStringLiteral("일시정지"));
    run_->setVisible(active);
    run_->setEnabled(active && !commandPending_ &&
        (missionState_ == QLatin1String("running") || ((paused || retry) && controlsReady_)));
    const QString detail = retry
        ? QStringLiteral("시작 요청 취소 · %1").arg(detail_.isEmpty()
            ? QStringLiteral("안전 상태를 확인한 뒤 다시 시작하십시오.") : detail_)
        : blocked || failed || reasonCode_.startsWith(QLatin1String("E_"))
            ? (detail_.isEmpty() ? reasonCode_ : detail_) : QString();
    reason_->setText(!controlReason_.isEmpty() && active ? controlReason_ : detail);
    reason_->setToolTip(detail_);
    reason_->setVisible(!reason_->text().isEmpty());

    // 취소할 점검이 없을 때도 자리는 지킨다. 버튼이 사라졌다 나타나면
    // 손이 자리를 외우지 못한다.
    stop_->setVisible(active);
    stop_->setEnabled(active);
    dock_->setEnabled(dockKnown_ && !disconnected && !blocked && controlsReady_ && !commandPending_ &&
        (!active || paused || missionState_ == QLatin1String("running")));
    dock_->setToolTip(
        !dockKnown_ ? QStringLiteral("충전 스테이션 위치가 등록되어 있지 않습니다.")
        : active    ? QStringLiteral("점검을 일시정지하고 충전 스테이션으로 돌아갑니다.\n"
                                     "진행 상황은 그대로 두므로 충전 뒤에 이어서 할 수 있습니다.")
                    : QStringLiteral("충전 스테이션까지 자율 주행으로 돌아갑니다."));

    const int total = std::max(0, total_);
    const int done = missionState_ == QLatin1String("completed") || (returning && index_ < 0) ? total
                     : index_ < 0 ? 0 : std::clamp(index_, 0, total);
    bar_->setValue(total > 0 ? done * 100 / total : 0);
    missionNameLabel_->setText(disconnected ? QStringLiteral("로봇 연결 필요")
        : missionName_.isEmpty() ? QStringLiteral("실행 중인 미션 없음") : missionName_);
    nextMission_->setText(disconnected ? QStringLiteral("로봇 연결 필요")
                                      : QStringLiteral("예약된 미션 없음"));
    count_->setText(total > 0 ? QStringLiteral("%1 / %2 단계 완료").arg(done).arg(total)
                              : QStringLiteral("진행 단계 없음"));

    const auto labelAt = [this](int i) {
        return i < stepLabels_.size() ? stepLabels_.at(i)
                                     : QStringLiteral("단계 %1").arg(i + 1);
    };
    if (missionState_ == QLatin1String("completed")) {
        current_->setText(QStringLiteral("미션 완료"));
        next_->setText(QStringLiteral("—"));
    } else if (failed) {
        current_->setText(QStringLiteral("미션 실패"));
        next_->setText(QStringLiteral("—"));
    } else if (returning) {
        current_->setText(QStringLiteral("충전소 복귀 중"));
        next_->setText(QStringLiteral("—"));
    } else if (active && index_ >= 0 && index_ < total) {
        current_->setText(QStringLiteral("%1. %2").arg(index_ + 1).arg(labelAt(index_)));
        next_->setText(index_ + 1 < total
                           ? QStringLiteral("%1. %2").arg(index_ + 2).arg(labelAt(index_ + 1))
                           : QStringLiteral("마지막 단계"));
    } else {
        current_->setText(disconnected ? QStringLiteral("로봇 연결 필요")
                                  : active ? QStringLiteral("미션 준비 중")
                                  : QStringLiteral("실행 중인 작업 없음"));
        next_->setText(total > 0 ? QStringLiteral("1. %1").arg(labelAt(0))
                                 : QStringLiteral("—"));
    }

    // 세우는 것과 그만두는 것의 차이는 누른 뒤에야 드러난다. 누르기 전에,
    // 몇 번 지점이 걸려 있는지까지 넣어서 말해 둔다.
    const int resumeAt = done + 1;
    run_->setToolTip(commandPending_ ? QStringLiteral("로봇 응답 대기 중")
        : !controlsReady_ && (paused || retry) ? controlReason_
        : retry ? QStringLiteral("현재 미션에 새 시작 명령을 보냅니다.")
        : paused ? QStringLiteral("%1번 지점부터 이어서 점검합니다.").arg(resumeAt)
                 : QStringLiteral("로봇이 그 자리에 섭니다. 진행 상황은 그대로 두고,\n"
                                  "재개하면 %1번 지점부터 이어서 합니다.")
                       .arg(resumeAt));
    stop_->setToolTip(
        QStringLiteral("점검을 끝냅니다. 진행 상황(%1 / %2)은 지워지고,\n"
                       "다시 실행하려면 미션 관리에서 선택해야 합니다.")
            .arg(done)
            .arg(total));
}

void MissionPanel::onRunClicked()
{
    if (commandPending_)
        return;
    const bool retry = missionState_ == QLatin1String("ready") &&
        reasonCode_ == QLatin1String("MISSION_START_CANCELLED_BY_SAFETY");
    if ((retry || missionState_ == QLatin1String("paused")) && !controlsReady_)
        return;
    if (!retry && missionState_ != QLatin1String("paused") && missionState_ != QLatin1String("running"))
        return;
    setCommandPending(true);
    if (retry) {
        emit missionRetry();
        return;
    }
    if (missionState_ == QLatin1String("paused"))
        emit missionResume();
    else if (missionState_ == QLatin1String("running"))
        emit missionPause();
}

void MissionPanel::confirmStop()
{
    if (!stop_->isEnabled())
        return;
    const QPointer<MissionPanel> panel(this);
    const quint64 generation = missionGeneration_;
    const int done = std::clamp(index_, 0, std::max(0, total_));

    // 버릴 것이 없으면 묻지 않는다. 확인 창이 늘 뜨면 읽지 않고 누르게 된다.
    if (done > 0) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("미션 취소"),
            QStringLiteral("미션을 취소합니다.\n\n"
                           "지금까지의 진행(%1 / %2)은 지워지고,\n"
                           "다시 실행하려면 미션을 선택해야 합니다.\n\n"
                           "이어서 하려면 재개를 누르십시오.")
                .arg(done)
                .arg(total_),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    if (!panel || generation != missionGeneration_ || !stop_->isEnabled())
        return;
    emit missionStop();
}

}  // namespace hmi::ui
