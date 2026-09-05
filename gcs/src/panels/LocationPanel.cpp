#include "panels/LocationPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

/// 정지 판정 기준. 10 Hz 갱신에서 0.05 m/s 면 프레임 사이 이동이 5 mm 로,
/// 주행 정밀도 요구(±10 cm)에 비해 무시할 만하다. 이보다 빠르면 저장된
/// 좌표가 실제 정지 위치와 어긋난다.
constexpr double kStationarySpeed = 0.05;

}  // namespace

CaptureCheck LocationPanel::checkCapture(const RobotSnapshot &snap, const QString &kind)
{
    CaptureCheck r;

    // ---- 차단 조건 ----
    if (!snap.poseFresh) {
        r.reason = QStringLiteral("위치 정보가 오래되었습니다. 브릿지 연결을 확인하십시오.");
        r.code = QStringLiteral("LOC_CAPTURE_BLOCKED");
        return r;
    }
    if (snap.speed > kStationarySpeed) {
        r.reason = QStringLiteral("로봇이 이동 중입니다 (%1 m/s). 정지 후 등록하십시오.")
                       .arg(snap.speed, 0, 'f', 2);
        r.code = QStringLiteral("LOC_CAPTURE_BLOCKED");
        return r;
    }

    // ---- 경고 조건: 진행은 가능하되 기록에 남긴다 ----
    r.allowed = true;
    if (!snap.localizationOk) {
        r.degraded = true;
        r.reason = QStringLiteral("위치 추정 신뢰도가 낮습니다. 저장된 좌표에 오차가 클 수 있습니다.");
        r.code = QStringLiteral("LOC_CAPTURE_DEGRADED");
        return r;
    }
    if (kind == QLatin1String("inspection") && snap.visibleTagId < 0) {
        // 포인트-마커 연결이 비면 현장에서 2차 정밀 보정을 할 수 없다.
        r.degraded = true;
        r.reason = QStringLiteral("Apriltag가 보이지 않습니다. 마커 연결 없이 저장하면 "
                                  "정밀 보정을 사용할 수 없습니다.");
        r.code = QStringLiteral("LOC_CAPTURE_DEGRADED");
        return r;
    }

    r.reason = QStringLiteral("등록 가능");
    r.code = QStringLiteral("LOC_CAPTURED");
    return r;
}

LocationPanel::LocationPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("위치 등록"));
    ready_ = new Badge(QStringLiteral("대기"), QStringLiteral("neutral"));
    card_->addHeaderWidget(ready_);
    outer->addWidget(card_);

    // ---- 점검포인트 추가 ----
    card_->body()->addWidget(sectionLabel(QStringLiteral("점검포인트 추가")));

    auto *addRow = new QHBoxLayout;
    addRow->setSpacing(metrics::s2);

    auto *fromRobot = new QPushButton(QStringLiteral("현재 위치로"));
    fromRobot->setProperty("variant", "primary");
    fromRobot->setToolTip(QStringLiteral(
        "로봇이 서 있는 자세를 그대로 저장합니다.\n"
        "도달 가능성이 이미 검증된 좌표라 지도 클릭보다 정확합니다."));
    connect(fromRobot, &QPushButton::clicked, this,
            [this] { emit captureFromRobot(QStringLiteral("inspection")); });

    auto *fromMap = new QPushButton(QStringLiteral("지도에서"));
    fromMap->setToolTip(QStringLiteral(
        "지도를 클릭해 위치를, 드래그해 방향을 지정합니다.\n"
        "도달 가능 여부는 확인되지 않습니다."));
    connect(fromMap, &QPushButton::clicked, this,
            [this] { emit captureFromMap(QStringLiteral("inspection")); });

    addRow->addWidget(fromRobot, 1);
    addRow->addWidget(fromMap, 1);
    card_->body()->addLayout(addRow);
    captureButtons_ << fromRobot;

    hint_ = new QLabel;
    hint_->setObjectName(QStringLiteral("Hint"));
    hint_->setWordWrap(true);
    card_->body()->addWidget(hint_);

    card_->body()->addSpacing(metrics::s2);
    card_->body()->addWidget(new HLine);

    // ---- 고정 위치 ----
    card_->body()->addWidget(sectionLabel(QStringLiteral("고정 위치")));
    card_->body()->addWidget(buildFixedRow(QStringLiteral("dock"),
                                           QStringLiteral("충전 스테이션")));
    card_->body()->addWidget(buildFixedRow(QStringLiteral("home"),
                                           QStringLiteral("시작 위치")));
    card_->body()->addStretch(1);

    refreshEnabled();
}

QWidget *LocationPanel::buildFixedRow(const QString &kind, const QString &title)
{
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, metrics::s1, 0, metrics::s2);
    lay->setSpacing(metrics::s1);

    auto *head = new QHBoxLayout;
    auto *name = new QLabel(title);
    head->addWidget(name);
    head->addStretch(1);

    auto *value = readout(QStringLiteral("미설정"));
    head->addWidget(value);
    valueLabels_.insert(kind, value);
    lay->addLayout(head);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(metrics::s2);

    auto *setHere = new QPushButton(QStringLiteral("현재 위치로 설정"));
    setHere->setProperty("size", "sm");
    connect(setHere, &QPushButton::clicked, this,
            [this, kind] { emit captureFromRobot(kind); });

    auto *fromMap = new QPushButton(QStringLiteral("지도에서"));
    fromMap->setProperty("size", "sm");
    connect(fromMap, &QPushButton::clicked, this,
            [this, kind] { emit captureFromMap(kind); });

    auto *goThere = new QPushButton(QStringLiteral("이동"));
    goThere->setProperty("size", "sm");
    goThere->setEnabled(false);
    connect(goThere, &QPushButton::clicked, this,
            [this, kind] { emit gotoRequested(kind); });

    buttons->addWidget(setHere, 2);
    buttons->addWidget(fromMap, 1);
    buttons->addWidget(goThere, 1);
    lay->addLayout(buttons);

    captureButtons_ << setHere;
    gotoButtons_.insert(kind, goThere);
    return host;
}

void LocationPanel::setSnapshot(const RobotSnapshot &snap)
{
    snap_ = snap;
    refreshEnabled();
}

void LocationPanel::refreshEnabled()
{
    const CaptureCheck check = checkCapture(snap_, QStringLiteral("inspection"));

    for (auto *b : std::as_const(captureButtons_))
        b->setEnabled(check.allowed);

    if (!check.allowed) {
        ready_->set(QStringLiteral("등록 불가"), QStringLiteral("danger"));
        hint_->setText(check.reason);
    } else if (check.degraded) {
        ready_->set(QStringLiteral("주의"), QStringLiteral("warn"));
        hint_->setText(check.reason);
    } else {
        ready_->set(QStringLiteral("등록 가능"), QStringLiteral("ok"));
        hint_->setText(QStringLiteral("현재 %1, %2   θ %3°")
                           .arg(snap_.x, 0, 'f', 2)
                           .arg(snap_.y, 0, 'f', 2)
                           .arg(qRadiansToDegrees(snap_.theta), 0, 'f', 1));
    }
}

void LocationPanel::setDock(const QVariantMap &location)
{
    auto *lbl = valueLabels_.value(QStringLiteral("dock"));
    auto *go = gotoButtons_.value(QStringLiteral("dock"));
    const bool set = !location.isEmpty();
    if (lbl)
        lbl->setText(set ? QStringLiteral("%1, %2")
                               .arg(location.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                               .arg(location.value(QStringLiteral("y")).toDouble(), 0, 'f', 2)
                         : QStringLiteral("미설정"));
    if (go)
        go->setEnabled(set);
}

void LocationPanel::setHome(const QVariantMap &location)
{
    auto *lbl = valueLabels_.value(QStringLiteral("home"));
    auto *go = gotoButtons_.value(QStringLiteral("home"));
    const bool set = !location.isEmpty();
    if (lbl)
        lbl->setText(set ? QStringLiteral("%1, %2")
                               .arg(location.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                               .arg(location.value(QStringLiteral("y")).toDouble(), 0, 'f', 2)
                         : QStringLiteral("미설정"));
    if (go)
        go->setEnabled(set);
}

}  // namespace gcs::ui
