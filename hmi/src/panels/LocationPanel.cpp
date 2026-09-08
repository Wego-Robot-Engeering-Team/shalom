#include "panels/LocationPanel.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;

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
        r.reason = QStringLiteral("위치 정보가 오래되었습니다. 로봇 연결을 확인하십시오.");
        r.code = QStringLiteral("LOC_CAPTURE_BLOCKED");
        return r;
    }
    if (snap.speed > kStationarySpeed) {
        r.reason = QStringLiteral("로봇이 움직이는 중입니다. 멈춘 뒤에 등록할 수 있습니다.");
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
        r.reason = QStringLiteral("마커가 보이지 않습니다. 이대로 저장하면 나중에 "
                                  "위치를 정밀하게 맞출 수 없습니다.");
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

    auto *fromRobot = new QPushButton(QStringLiteral("로봇 위치로 추가"));
    fromRobot->setProperty("variant", "primary");
    fromRobot->setToolTip(QStringLiteral(
        "로봇이 서 있는 자세를 그대로 저장합니다.\n"
        "도달 가능성이 이미 검증된 좌표라 지도 클릭보다 정확합니다."));
    connect(fromRobot, &QPushButton::clicked, this,
            [this] { emit captureFromRobot(QStringLiteral("inspection")); });

    auto *fromMap = new QPushButton(QStringLiteral("지도에서 추가"));
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
    card_->body()->addWidget(sectionLabel(QStringLiteral("주요 지점")));
    card_->body()->addWidget(buildFixedRow(QStringLiteral("dock"),
                                           QStringLiteral("충전 스테이션")));
    card_->body()->addWidget(buildFixedRow(QStringLiteral("home"),
                                           QStringLiteral("시작 위치")));

    card_->body()->addSpacing(metrics::s2);
    card_->body()->addWidget(new HLine);
    card_->body()->addWidget(buildMarkerSection());
    card_->body()->addStretch(1);

    refreshEnabled();
}

QWidget *LocationPanel::buildMarkerSection()
{
    // 마커는 현장에서 벽에 붙이고 줄자로 재어 오는 물건이다. 로봇 위치로
    // 잡는 방법은 두지 않는다 — 로봇이 선 자리는 마커가 붙은 자리가 아니라
    // 마커를 바라보는 자리라, 그걸로 등록하면 처음부터 틀린 값이 들어간다.
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s1);

    auto *head = new QHBoxLayout;
    head->addWidget(sectionLabel(QStringLiteral("마커")), 1);
    markerCount_ = new QLabel;
    markerCount_->setObjectName(QStringLiteral("Value"));
    head->addWidget(markerCount_);
    lay->addLayout(head);

    markerList_ = new QListWidget;
    markerList_->setObjectName(QStringLiteral("MarkerList"));
    markerList_->setMaximumHeight(112);
    markerList_->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(markerList_);

    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s2);
    auto *add = new QPushButton(QStringLiteral("지도에서 추가"));
    add->setProperty("size", "sm");
    markerDelete_ = new QPushButton(QStringLiteral("삭제"));
    markerDelete_->setProperty("size", "sm");
    markerDelete_->setEnabled(false);
    row->addWidget(add, 2);
    row->addWidget(markerDelete_, 1);
    lay->addLayout(row);

    connect(add, &QPushButton::clicked, this, &LocationPanel::addMarkerFromMap);
    connect(markerList_, &QListWidget::currentRowChanged, this, [this](int row) {
        markerDelete_->setEnabled(row >= 0 && row < markers_.size());
    });
    connect(markerDelete_, &QPushButton::clicked, this, [this] {
        const int row = markerList_->currentRow();
        if (row < 0 || row >= markers_.size())
            return;
        markers_.removeAt(row);
        refreshMarkerList();
        emit markersChanged(markers_);
    });

    refreshMarkerList();
    return host;
}

void LocationPanel::setMarkers(const QList<QVariantMap> &markers)
{
    markers_ = markers;
    refreshMarkerList();
}

void LocationPanel::refreshMarkerList()
{
    if (!markerList_)
        return;
    const int keep = markerList_->currentRow();
    markerList_->clear();
    for (const auto &m : std::as_const(markers_)) {
        markerList_->addItem(QStringLiteral("#%1    %2, %3")
                                 .arg(m.value(QStringLiteral("id")).toInt())
                                 .arg(m.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                                 .arg(m.value(QStringLiteral("y")).toDouble(), 0, 'f', 2));
    }
    markerCount_->setText(markers_.isEmpty() ? QStringLiteral("없음")
                                             : QStringLiteral("%1개").arg(markers_.size()));
    if (keep >= 0 && keep < markers_.size())
        markerList_->setCurrentRow(keep);
    markerDelete_->setEnabled(markerList_->currentRow() >= 0);
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

    auto *setHere = new QPushButton(QStringLiteral("로봇 위치로 지정"));
    setHere->setProperty("size", "sm");
    connect(setHere, &QPushButton::clicked, this,
            [this, kind] { emit captureFromRobot(kind); });

    auto *fromMap = new QPushButton(QStringLiteral("지도에서 지정"));
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
        hint_->setText(QStringLiteral("로봇 위치  %1, %2   방향 %3°")
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

}  // namespace hmi::ui
