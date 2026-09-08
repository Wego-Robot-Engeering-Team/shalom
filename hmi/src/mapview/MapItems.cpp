#include "mapview/MapItems.h"

#include <QFont>
#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>

#include "theme/Tokens.h"

namespace hmi::map {

using namespace hmi::theme;

QString waypointColor(const QString &status)
{
    const Colors &C = colors();
    if (status == QLatin1String("done"))
        return C.wpDone;
    if (status == QLatin1String("current"))
        return C.wpCurrent;
    if (status == QLatin1String("error"))
        return C.wpError;
    return C.wpTodo;
}

// ============================ RobotMarker ============================

RobotMarker::RobotMarker(double radius) : r_(radius)
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setZValue(100);
}

void RobotMarker::setStale(bool stale)
{
    if (stale == stale_)
        return;
    stale_ = stale;
    update();
}

QRectF RobotMarker::boundingRect() const
{
    const double e = r_ * 3.2;
    return {-e, -e, e * 2, e * 2};
}

void RobotMarker::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Colors &C = colors();
    p->setRenderHint(QPainter::Antialiasing);
    const QColor col(stale_ ? C.textMute : C.accent);

    // 진행 방향 지시선 — 로컬 +x. 시야 원뿔 같은 장식은 쓰지 않는다.
    // 지도에 반투명 면이 늘어나면 경로와 웨이포인트가 묻힌다.
    p->setPen(QPen(col, 1.5));
    p->drawLine(QPointF(r_, 0), QPointF(r_ * 2.1, 0));

    p->setPen(QPen(QColor(C.surface), 2.0));
    p->setBrush(col);
    p->drawEllipse(QPointF(0, 0), r_, r_);

    p->setPen(Qt::NoPen);
    p->setBrush(QColor(C.textOnAccent));
    p->drawPolygon(QPolygonF{{r_ * 0.72, 0.0}, {-r_ * 0.24, -r_ * 0.46}, {-r_ * 0.24, r_ * 0.46}});
}

// ============================ WaypointMarker ============================

WaypointMarker::WaypointMarker(int index, const QString &id, const QString &status,
                               double radius)
    : index_(index), id_(id), status_(status), r_(radius)
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setAcceptHoverEvents(true);
    setZValue(60);
    setToolTip(QStringLiteral("%1  (#%2)").arg(id).arg(index + 1));
}

void WaypointMarker::setStatus(const QString &status)
{
    if (status == status_)
        return;
    status_ = status;
    // 현재 포인트를 다른 마커 위로 올린다. 포인트가 촘촘한 구간에서 겹친다.
    setZValue(selected_ ? 85 : (status_ == QLatin1String("current") ? 80 : 60));
    update();
}

void WaypointMarker::setHeading(double theta)
{
    if (hasHeading_ && qFuzzyCompare(theta_, theta))
        return;
    theta_ = theta;
    hasHeading_ = true;
    update();
}

void WaypointMarker::setSelected(bool selected)
{
    if (selected == selected_)
        return;
    selected_ = selected;
    // 고른 것은 겹친 것들 위로. 촘촘한 구간에서 고르고도 안 보이면 소용없다.
    setZValue(selected_ ? 85 : (status_ == QLatin1String("current") ? 80 : 60));
    update();
}

void WaypointMarker::hoverEnterEvent(QGraphicsSceneHoverEvent *ev)
{
    hover_ = true;
    update();
    QGraphicsItem::hoverEnterEvent(ev);
}

void WaypointMarker::hoverLeaveEvent(QGraphicsSceneHoverEvent *ev)
{
    hover_ = false;
    update();
    QGraphicsItem::hoverLeaveEvent(ev);
}

QRectF WaypointMarker::boundingRect() const
{
    const double e = r_ * 2.6;   // 방향 눈금까지 담는다
    return {-e, -e, e * 2, e * 2};
}

void WaypointMarker::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Colors &C = colors();
    p->setRenderHint(QPainter::Antialiasing);

    const QColor col(waypointColor(status_));
    const double r = r_ * (hover_ || selected_ ? 1.12 : 1.0);

    // 선택 고리. 상태색과 겹치지 않게 강조색 실선으로 두른다 — 상태를
    // 덧칠하면 "지금 가는 중" 과 "내가 고른 것" 이 구분되지 않는다.
    if (selected_) {
        p->setPen(QPen(QColor(C.accent), 2.0));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(QPointF(0, 0), r * 1.62, r * 1.62);
    }

    if (status_ == QLatin1String("current")) {
        QColor ring(col);
        ring.setAlpha(60);
        p->setPen(Qt::NoPen);
        p->setBrush(ring);
        p->drawEllipse(QPointF(0, 0), r * 1.9, r * 1.9);
    }

    // 미완료만 속이 빈 원. 진행/완료/오류는 채운다 — 남은 일이 한눈에 보인다.
    const bool filled = status_ != QLatin1String("todo");
    p->setPen(filled ? QPen(QColor(C.surface), 1.5) : QPen(col, 1.5));
    p->setBrush(filled ? QBrush(col) : QBrush(QColor(C.surface)));
    p->drawEllipse(QPointF(0, 0), r, r);

    // 도착했을 때 바라볼 방향. 원 바깥에 짧은 눈금으로 낸다.
    //
    // 화살표를 크게 그리면 촘촘한 구간에서 이웃 포인트와 겹쳐 어느 것의
    // 방향인지 오히려 헷갈린다. 원에 붙은 눈금이면 겹쳐도 주인이 분명하다.
    //
    // 씬 y 는 아래로 향하므로 각을 뒤집는다. 지도는 회전하지 않으니 월드
    // 각이 화면 각으로 그대로 간다.
    if (hasHeading_) {
        p->save();
        p->rotate(-theta_ * 180.0 / M_PI);
        p->setPen(QPen(col, 2.0, Qt::SolidLine, Qt::RoundCap));
        p->drawLine(QPointF(r * 1.05, 0), QPointF(r * 1.75, 0));
        p->setPen(Qt::NoPen);
        p->setBrush(col);
        p->drawPolygon(QPolygonF{{r * 2.05, 0.0},
                                 {r * 1.55, -r * 0.34},
                                 {r * 1.55, r * 0.34}});
        p->restore();
    }

    QFont f;
    f.setPointSize(8);
    f.setWeight(QFont::DemiBold);
    p->setFont(f);
    p->setPen(filled ? QColor(C.textOnAccent) : QColor(C.textDim));
    p->drawText(QRectF(-r, -r, r * 2, r * 2), Qt::AlignCenter, QString::number(index_ + 1));
}

// ============================ AprilTagMarker ============================

AprilTagMarker::AprilTagMarker(int tagId, double size) : id_(tagId), s_(size)
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setZValue(50);
    setToolTip(QStringLiteral("마커 #%1").arg(tagId));
}

void AprilTagMarker::setSeen(bool seen)
{
    if (seen == seen_)
        return;
    seen_ = seen;
    update();
}

QRectF AprilTagMarker::boundingRect() const
{
    const double e = s_ * 2.2;
    return {-e, -e, e * 2, e * 2};
}

void AprilTagMarker::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Colors &C = colors();
    p->setRenderHint(QPainter::Antialiasing);
    const QColor col(C.tag);

    if (seen_) {
        QColor halo(col);
        halo.setAlpha(70);
        p->setPen(Qt::NoPen);
        p->setBrush(halo);
        p->drawRoundedRect(QRectF(-s_ * 1.7, -s_ * 1.7, s_ * 3.4, s_ * 3.4), 3, 3);
    }

    p->setPen(QPen(col, 1.5));
    p->setBrush(seen_ ? QBrush(col) : QBrush(QColor(C.surface)));
    p->drawRect(QRectF(-s_, -s_, s_ * 2, s_ * 2));

    QFont f;
    f.setPointSize(7);
    f.setWeight(QFont::DemiBold);
    p->setFont(f);
    p->setPen(seen_ ? QColor(C.textOnAccent) : col);
    p->drawText(QRectF(-s_, -s_, s_ * 2, s_ * 2), Qt::AlignCenter, QString::number(id_));
}

// ============================ StationMarker ============================

StationMarker::StationMarker(Kind kind, double size) : kind_(kind), s_(size)
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    // 웨이포인트(60)보다 위, 로봇(100)보다 아래. 고정된 자리라 로봇을 가리면
    // 안 되지만, 포인트가 촘촘한 구간에서 묻혀도 곤란하다.
    setZValue(70);
    setToolTip(kind_ == Kind::Dock ? QStringLiteral("충전 스테이션")
                                   : QStringLiteral("시작 위치"));
}

QRectF StationMarker::boundingRect() const
{
    const double e = s_ * 2.0;
    return {-e, -e, e * 2, e * 2};
}

void StationMarker::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Colors &C = colors();
    p->setRenderHint(QPainter::Antialiasing);
    const QColor col(kind_ == Kind::Dock ? C.dock : C.home);

    // 바닥 그림자. 지도 위 어디에 "놓여" 있다는 느낌을 준다.
    QColor shade(col);
    shade.setAlpha(38);
    p->setPen(Qt::NoPen);
    p->setBrush(shade);
    p->drawEllipse(QPointF(0, s_ * 0.92), s_ * 1.15, s_ * 0.34);

    if (kind_ == Kind::Dock) {
        // 지붕 얹은 작은 집 + 번개.
        const double w = s_ * 0.92, h = s_ * 0.78;
        QPainterPath house;
        house.moveTo(-w * 1.18, -h * 0.18);       // 처마 왼쪽
        house.lineTo(0.0, -h * 1.30);             // 용마루
        house.lineTo(w * 1.18, -h * 0.18);        // 처마 오른쪽
        house.lineTo(w * 0.82, -h * 0.18);
        house.lineTo(w * 0.82, h * 0.86);
        house.lineTo(-w * 0.82, h * 0.86);
        house.lineTo(-w * 0.82, -h * 0.18);
        house.closeSubpath();

        p->setPen(QPen(QColor(C.surface), 2.0));
        p->setBrush(col);
        p->drawPath(house);

        p->setPen(Qt::NoPen);
        p->setBrush(QColor(C.textOnAccent));
        p->drawPolygon(QPolygonF{{s_ * 0.10, -h * 0.06},
                                 {-s_ * 0.34, h * 0.40},
                                 {-s_ * 0.04, h * 0.40},
                                 {-s_ * 0.12, h * 0.80},
                                 {s_ * 0.34, h * 0.18},
                                 {s_ * 0.02, h * 0.18}});
        return;
    }

    // 시작 위치 — 발자국. 네발로봇의 집이라는 뜻이고, 지도 위 다른 무엇과도
    // 닮지 않아 멀리서도 이것만 눈에 든다.
    p->setPen(QPen(QColor(C.surface), 1.6));
    p->setBrush(col);

    // 발바닥
    QPainterPath pad;
    pad.addEllipse(QPointF(0, s_ * 0.34), s_ * 0.74, s_ * 0.60);
    p->drawPath(pad);

    // 발가락 넷. 바깥 둘을 조금 낮추고 기울여 앉히면 발처럼 읽힌다.
    struct Toe { double x, y, rx, ry, rot; };
    static const Toe toes[4] = {{-0.74, -0.52, 0.27, 0.36, -18.0},
                                {-0.26, -0.80, 0.29, 0.38, -6.0},
                                {0.26, -0.80, 0.29, 0.38, 6.0},
                                {0.74, -0.52, 0.27, 0.36, 18.0}};
    for (const Toe &t : toes) {
        p->save();
        p->translate(t.x * s_, t.y * s_);
        p->rotate(t.rot);
        p->drawEllipse(QPointF(0, 0), t.rx * s_, t.ry * s_);
        p->restore();
    }
}

// ============================ GoalMarker ============================

GoalMarker::GoalMarker(double radius) : r_(radius)
{
    setFlag(QGraphicsItem::ItemIgnoresTransformations, true);
    setZValue(95);
}

QRectF GoalMarker::boundingRect() const
{
    const double e = r_ * 3.0;
    return {-e, -e, e * 2, e * 2};
}

void GoalMarker::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    p->setRenderHint(QPainter::Antialiasing);
    const QColor col(colors().accent);

    p->setPen(QPen(col, 1.5, Qt::DashLine));
    p->setBrush(Qt::NoBrush);
    p->drawEllipse(QPointF(0, 0), r_ * 1.7, r_ * 1.7);

    p->setPen(Qt::NoPen);
    p->setBrush(col);
    p->drawEllipse(QPointF(0, 0), r_ * 0.32, r_ * 0.32);

    // 목표 방향 화살표 — 로컬 +x
    p->setPen(QPen(col, 1.8));
    p->drawLine(QPointF(0, 0), QPointF(r_ * 2.4, 0));
    p->setPen(Qt::NoPen);
    p->drawPolygon(QPolygonF{{r_ * 3.0, 0.0}, {r_ * 2.1, -r_ * 0.5}, {r_ * 2.1, r_ * 0.5}});
}

}  // namespace hmi::map
