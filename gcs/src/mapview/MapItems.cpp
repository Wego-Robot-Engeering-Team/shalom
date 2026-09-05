#include "mapview/MapItems.h"

#include <QFont>
#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QPolygonF>

#include "theme/Tokens.h"

namespace gcs::map {

using namespace gcs::theme;

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
    setZValue(status_ == QLatin1String("current") ? 80 : 60);
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
    const double e = r_ * 2.4;
    return {-e, -e, e * 2, e * 2};
}

void WaypointMarker::paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *)
{
    const Colors &C = colors();
    p->setRenderHint(QPainter::Antialiasing);

    const QColor col(waypointColor(status_));
    const double r = r_ * (hover_ ? 1.12 : 1.0);

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
    setToolTip(QStringLiteral("AprilTag #%1").arg(tagId));
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

}  // namespace gcs::map
