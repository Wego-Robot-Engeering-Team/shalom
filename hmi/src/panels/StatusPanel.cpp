// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary
#include "panels/StatusPanel.h"
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {
using namespace hmi::theme;
namespace {
QString formatMetric(double value, const QString &unit, int decimals = 1)
{
    return std::isfinite(value) ? QStringLiteral("%1 %2").arg(value, 0, 'f', decimals).arg(unit)
                                : QStringLiteral("—");
}
QString lifecycle(const QString &value)
{
    if (value == QLatin1String("active")) return QStringLiteral("활성");
    if (value == QLatin1String("inactive")) return QStringLiteral("비활성");
    if (value == QLatin1String("unconfigured")) return QStringLiteral("준비 전");
    if (value == QLatin1String("finalized")) return QStringLiteral("종료");
    return QStringLiteral("확인 중");
}
}

StatusPanel::StatusPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new Card(QStringLiteral("주행"));
    outer->addWidget(card);
    state_ = new Badge(QStringLiteral("연결 없음"));
    state_->setObjectName(QStringLiteral("NavigationState"));
    card->addHeaderWidget(state_);
    auto *body = card->body();
    auto *readiness = new QGridLayout;
    readiness->addWidget(sectionLabel(QStringLiteral("내비게이션")), 0, 0);
    readiness->addWidget(sectionLabel(QStringLiteral("위치추정")), 0, 1);
    navigation_ = readout();
    localization_ = readout();
    navigation_->setObjectName(QStringLiteral("NavigationLifecycle"));
    localization_->setObjectName(QStringLiteral("LocalizationLifecycle"));
    readiness->addWidget(navigation_, 1, 0);
    readiness->addWidget(localization_, 1, 1);
    readiness->setColumnStretch(0, 1);
    readiness->setColumnStretch(1, 1);
    body->addLayout(readiness);

    goalArea_ = new QWidget;
    auto *goals = new QVBoxLayout(goalArea_);
    goals->setContentsMargins(0, 0, 0, 0);
    goals->setSpacing(metrics::s2);
    goals->addWidget(new HLine);
    target_ = new QLabel(QStringLiteral("목표 없음"));
    target_->setObjectName(QStringLiteral("NavigationTarget"));
    target_->setWordWrap(true);
    target_->setTextFormat(Qt::PlainText);
    goals->addWidget(target_);
    auto *progress = new QGridLayout;
    progress->addWidget(sectionLabel(QStringLiteral("남은 거리")), 0, 0);
    progress->addWidget(sectionLabel(QStringLiteral("예상 시간")), 0, 1);
    distance_ = readout({}, true);
    eta_ = readout({}, true);
    distance_->setObjectName(QStringLiteral("NavigationDistance"));
    eta_->setObjectName(QStringLiteral("NavigationEta"));
    progress->addWidget(distance_, 1, 0);
    progress->addWidget(eta_, 1, 1);
    progress->setColumnStretch(0, 1);
    progress->setColumnStretch(1, 1);
    goals->addLayout(progress);
    auto *actions = new QGridLayout;
    const auto button = [this](const QString &text, const char *name) {
        auto *b = new QPushButton(text, this);
        b->setObjectName(QLatin1String(name));
        b->setEnabled(false);
        return b;
    };
    goal_ = button(QStringLiteral("목표 지정"), "NavigationGoalButton");
    goal_->setCheckable(true);
    start_ = button(QStringLiteral("주행 시작"), "NavigationStartButton");
    start_->setProperty("variant", "primary");
    pause_ = button(QStringLiteral("일시정지"), "NavigationPauseButton");
    cancel_ = button(QStringLiteral("취소"), "NavigationCancelButton");
    actions->addWidget(goal_, 0, 0);
    actions->addWidget(start_, 0, 1);
    actions->addWidget(pause_, 0, 1);
    actions->addWidget(cancel_, 0, 2);
    actions->setColumnStretch(0, 1);
    actions->setColumnStretch(1, 1);
    goals->addLayout(actions);
    pause_->hide();
    mission_ = new QPushButton(QStringLiteral("미션 제어로 이동"));
    mission_->setObjectName(QStringLiteral("NavigationMissionButton"));
    connect(mission_, &QPushButton::clicked, this, &StatusPanel::missionRequested);
    goals->addWidget(mission_);
    mission_->hide();
    reason_ = new QLabel;
    reason_->setObjectName(QStringLiteral("NavigationUnavailableReason"));
    reason_->setWordWrap(true);
    goals->addWidget(reason_);
    error_ = new QLabel;
    error_->setObjectName(QStringLiteral("NavigationError"));
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    goals->addWidget(error_);
    body->addWidget(goalArea_);

    auto *toggle = new QPushButton(QStringLiteral("▸ 상세 정보"));
    toggle->setObjectName(QStringLiteral("NavigationDetailsButton"));
    toggle->setCheckable(true);
    toggle->setProperty("size", "sm");
    body->addWidget(toggle);
    auto *details = new QWidget;
    auto *detailLayout = new QVBoxLayout(details);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    const auto add = [detailLayout](const QString &title, const char *name) {
        detailLayout->addWidget(sectionLabel(title));
        auto *value = readout();
        value->setObjectName(QLatin1String(name));
        value->setWordWrap(true);
        detailLayout->addWidget(value);
        return value;
    };
    pose_ = add(QStringLiteral("현재 위치"), "NavigationCurrentPose");
    velocity_ = add(QStringLiteral("실제 속도 · 선속도 / 각속도"), "NavigationActualVelocity");
    elapsed_ = add(QStringLiteral("경과 시간"), "NavigationElapsed");
    recoveries_ = add(QStringLiteral("복구 횟수"), "NavigationRecoveries");
    body->addWidget(details);
    details->hide();
    connect(toggle, &QPushButton::toggled, this, [toggle, details](bool on) {
        details->setVisible(on);
        toggle->setText(on ? QStringLiteral("▾ 상세 정보") : QStringLiteral("▸ 상세 정보"));
    });
    setTelemetry({}, false);
}

void StatusPanel::setMode(const QString &mode, bool estop)
{
    manual_ = mode == QLatin1String("manual");
    estop_ = estop;
    goalArea_->setVisible(!manual_ || draft_ || missionActive_);
}

void StatusPanel::setTelemetry(const robot::Telemetry &tm, bool connected)
{
    safetyState_ = tm.safetyState;
    safetyFresh_ = connected && tm.safetyFresh;
    safetyMotionPermitted_ = tm.safetyMotionPermitted;
    navigation_->setText(connected && tm.navFresh ? lifecycle(tm.navigationLifecycle) : QStringLiteral("—"));
    localization_->setText(connected && tm.navFresh ? lifecycle(tm.localizationLifecycle) : QStringLiteral("—"));
    const bool progress = connected && tm.navFresh;
    const double missing = std::numeric_limits<double>::quiet_NaN();
    distance_->setText(formatMetric(progress ? tm.navDistance : missing, QStringLiteral("m")));
    eta_->setText(formatMetric(progress ? tm.navEta : missing, QStringLiteral("s"), 0));
    elapsed_->setText(formatMetric(progress ? tm.navElapsed : missing, QStringLiteral("s"), 0));
    recoveries_->setText(progress && tm.navRecoveries >= 0 ? QString::number(tm.navRecoveries) : QStringLiteral("—"));
    pose_->setText(connected && tm.poseFresh ? QStringLiteral("X %1 m  ·  Y %2 m  ·  %3°")
        .arg(tm.x, 0, 'f', 2).arg(tm.y, 0, 'f', 2).arg(qRadiansToDegrees(tm.theta), 0, 'f', 1) : QStringLiteral("—"));
    velocity_->setText(connected && tm.poseFresh ? formatMetric(tm.speed, QStringLiteral("m/s"), 2) +
        QStringLiteral("  /  ") + formatMetric(tm.angularSpeed, QStringLiteral("rad/s"), 2) : QStringLiteral("—"));
}

void StatusPanel::setGoalState(const QString &state, const QVariantMap &goal, bool draft,
                              bool pending, bool mission, const QString &missionLabel, const QString &error)
{
    draft_ = draft;
    missionActive_ = mission;
    const bool moving = state == QLatin1String("accepting") || state == QLatin1String("navigating");
    const bool stopping = state == QLatin1String("pausing") || state == QLatin1String("canceling");
    const bool paused = state == QLatin1String("paused");
    QString label = state.isEmpty() ? QStringLiteral("상태 대기") : QStringLiteral("대기");
    QString tone = QStringLiteral("neutral");
    if (moving) { label = QStringLiteral("주행 중"); tone = QStringLiteral("info"); }
    if (paused) { label = QStringLiteral("일시정지"); tone = QStringLiteral("warn"); }
    if (stopping) label = QStringLiteral("정지 중");
    if (state == QLatin1String("succeeded")) { label = QStringLiteral("도착"); tone = QStringLiteral("ok"); }
    if (state == QLatin1String("canceled")) label = QStringLiteral("취소됨");
    if (state == QLatin1String("failed") || state == QLatin1String("rejected")) { label = QStringLiteral("실패"); tone = QStringLiteral("danger"); }
    if (draft) { label = QStringLiteral("목표 선택됨"); tone = QStringLiteral("info"); }
    if (pending || state == QLatin1String("accepting")) label = QStringLiteral("시작 요청 중");
    if (mission) label = QStringLiteral("미션 수행 중");
    if (manual_) label = QStringLiteral("수동 조작");
    if (safetyFresh_ && safetyState_ == QLatin1String("controlled_stop")) {
        label = QStringLiteral("안전 정지"); tone = QStringLiteral("warn");
    } else if (safetyFresh_ && safetyState_ == QLatin1String("fault")) {
        label = QStringLiteral("안전 오류"); tone = QStringLiteral("danger");
    } else if (safetyFresh_ && safetyState_ == QLatin1String("initializing")) {
        label = QStringLiteral("안전 상태 준비 중"); tone = QStringLiteral("warn");
    } else if (safetyFresh_ && safetyState_ == QLatin1String("normal") &&
               !safetyMotionPermitted_ && moving) {
        label = QStringLiteral("동작 허가 대기"); tone = QStringLiteral("warn");
    }
    if (state == QLatin1String("stale") && !draft && !mission && !manual_) {
        label = QStringLiteral("상태 수신 대기");
        tone = QStringLiteral("warn");
    }
    if (estop_) { label = QStringLiteral("비상정지"); tone = QStringLiteral("danger"); }
    if (state == QLatin1String("disconnected")) { label = QStringLiteral("연결 없음"); tone = QStringLiteral("neutral"); }
    state_->set(label, tone);
    const QString coordinates = goal.isEmpty() ? QString{} : QStringLiteral("X %1 m  ·  Y %2 m  ·  %3°")
        .arg(goal.value("x").toDouble(), 0, 'f', 2).arg(goal.value("y").toDouble(), 0, 'f', 2)
        .arg(qRadiansToDegrees(goal.value("theta").toDouble()), 0, 'f', 1);
    target_->setText(mission ? missionLabel : goal.isEmpty() ? QStringLiteral("목표 없음") :
        (goal.value("name").toString().isEmpty() ? coordinates : goal.value("name").toString() + '\n' + coordinates));
    goal_->setText(draft ? QStringLiteral("다시 지정") : QStringLiteral("목표 지정"));
    goal_->setVisible(!mission);
    start_->setVisible(!mission && !moving && !paused && !stopping);
    pause_->setVisible(!mission && (moving || paused || stopping));
    pause_->setText(paused ? QStringLiteral("재개") : stopping ? QStringLiteral("정지 중") : QStringLiteral("일시정지"));
    cancel_->setVisible(!mission);
    mission_->setVisible(mission);
    // A prepared goal or mission must not inherit the previous direct goal's feedback.
    if (draft || mission) {
        distance_->setText(QStringLiteral("—"));
        eta_->setText(QStringLiteral("—"));
        elapsed_->setText(QStringLiteral("—"));
        recoveries_->setText(QStringLiteral("—"));
    }
    goalArea_->setVisible(!manual_ || draft || mission);
    error_->setText(error);
    error_->setVisible(!error.isEmpty());
}

void StatusPanel::setActions(bool select, bool start, bool pause, bool cancel, const QString &reason)
{
    goal_->setEnabled(select);
    start_->setEnabled(start);
    pause_->setEnabled(pause);
    cancel_->setEnabled(cancel);
    goal_->setToolTip(reason);
    start_->setToolTip(reason);
    reason_->setText(reason);
    reason_->setVisible(!reason.isEmpty());
}

} // namespace hmi::ui
