#include "mapview/MapView.h"

#include <cmath>
#include <QFont>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QVariantMap>
#include <QWheelEvent>
#include <QtMath>

#include "mapview/MapItems.h"
#include "theme/Tokens.h"

namespace gcs::map {

using namespace gcs::theme;

MapView::MapView(QWidget *parent) : QGraphicsView(parent)
{
    scene_ = new QGraphicsScene(this);
    setScene(scene_);

    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QGraphicsView::NoFrame);
    setMouseTracking(true);

    // Z 순서: 맵 < 궤적 < 계획 < 태그 < 웨이포인트 < 목표 < 로봇
    mapItem_ = new QGraphicsPixmapItem;
    mapItem_->setZValue(0);
    mapItem_->setTransformationMode(Qt::SmoothTransformation);
    scene_->addItem(mapItem_);

    trailItem_ = new QGraphicsPathItem;
    trailItem_->setZValue(20);
    scene_->addItem(trailItem_);

    planItem_ = new QGraphicsPathItem;
    planItem_->setZValue(30);
    scene_->addItem(planItem_);

    robot_ = new RobotMarker;
    robot_->setVisible(false);
    scene_->addItem(robot_);

    retheme();
}

MapView::~MapView() = default;

void MapView::retheme()
{
    const Colors &C = colors();
    // 맵 경계 바깥은 카드 면색으로 둔다. 미탐색 색으로 채우면 카드 전체가
    // 회색 판이 되어 지도가 어디까지인지 읽히지 않는다.
    setBackgroundBrush(QColor(C.surface));
    // 실제 주행 궤적: 회색 점선 / Nav2 계획 경로: 파랑 실선 (지시서 2.2.7 [1] ③)
    trailItem_->setPen(QPen(QColor(C.trail), 1.6, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
    planItem_->setPen(QPen(QColor(C.plan), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    scene_->update();
}

// ================= 맵 =================

void MapView::setMap(const MapInfo &info, const QImage &image)
{
    info_ = info;
    mapItem_->setPixmap(QPixmap::fromImage(image));
    mapItem_->setPos(0, 0);
    scene_->setSceneRect(QRectF(0, 0, info.sceneWidth(), info.sceneHeight()));
    fitMap();
}

const MapInfo *MapView::mapInfo() const
{
    return info_ ? &(*info_) : nullptr;
}

void MapView::fitMap()
{
    if (!info_)
        return;
    fitInView(scene_->sceneRect(), Qt::KeepAspectRatio);
    scale(0.94, 0.94);   // 가장자리 여백
}

// ================= 모드 =================

void MapView::setMode(MapMode mode)
{
    mode_ = mode;
    viewport()->setCursor(mode_ == MapMode::View ? Qt::ArrowCursor : Qt::CrossCursor);
}

// ================= 상태 =================

void MapView::setRobotPose(double x, double y, double theta, bool stale)
{
    if (!info_)
        return;
    robot_->setVisible(true);
    robot_->setPos(info_->toScene(x, y));
    robot_->setRotation(MapInfo::thetaToItemRotation(theta));
    robot_->setStale(stale);
}

QPainterPath MapView::pathFromWorld(const QList<QPointF> &pts) const
{
    QPainterPath path;
    if (!info_ || pts.size() < 2)
        return path;
    path.moveTo(info_->toScene(pts.first().x(), pts.first().y()));
    for (int i = 1; i < pts.size(); ++i)
        path.lineTo(info_->toScene(pts.at(i).x(), pts.at(i).y()));
    return path;
}

void MapView::setPlan(const QList<QPointF> &worldPoints)
{
    planItem_->setPath(pathFromWorld(worldPoints));
}

void MapView::setTrail(const QList<QPointF> &worldPoints)
{
    trailItem_->setPath(pathFromWorld(worldPoints));
}

void MapView::setWaypoints(const QList<QVariantMap> &waypoints)
{
    for (auto *m : std::as_const(waypoints_))
        scene_->removeItem(m), delete m;
    waypoints_.clear();
    if (!info_)
        return;

    int i = 0;
    for (const auto &wp : waypoints) {
        const QString id = wp.value(QStringLiteral("id")).toString();
        auto *m = new WaypointMarker(i++, id,
                                     wp.value(QStringLiteral("status"),
                                              QStringLiteral("todo")).toString());
        m->setPos(info_->toScene(wp.value(QStringLiteral("x")).toDouble(),
                                 wp.value(QStringLiteral("y")).toDouble()));
        scene_->addItem(m);
        waypoints_.insert(id, m);
    }
}

void MapView::setWaypointStatus(const QString &id, const QString &status)
{
    if (auto *m = waypoints_.value(id, nullptr))
        m->setStatus(status);
}

void MapView::focusWaypoint(const QString &id)
{
    if (auto *m = waypoints_.value(id, nullptr))
        centerOn(m);
}

void MapView::setTags(const QList<QVariantMap> &tags)
{
    for (auto *m : std::as_const(tags_))
        scene_->removeItem(m), delete m;
    tags_.clear();
    if (!info_)
        return;

    for (const auto &t : tags) {
        const int id = t.value(QStringLiteral("id")).toInt();
        auto *m = new AprilTagMarker(id);
        m->setPos(info_->toScene(t.value(QStringLiteral("x")).toDouble(),
                                 t.value(QStringLiteral("y")).toDouble()));
        scene_->addItem(m);
        tags_.insert(id, m);
    }
}

void MapView::setTagsSeen(const QSet<int> &seenIds)
{
    for (auto it = tags_.cbegin(); it != tags_.cend(); ++it)
        it.value()->setSeen(seenIds.contains(it.key()));
}

void MapView::setGoal(double x, double y, double theta)
{
    if (!info_)
        return;
    if (!goal_) {
        goal_ = new GoalMarker;
        scene_->addItem(goal_);
    }
    goal_->setVisible(true);
    goal_->setPos(info_->toScene(x, y));
    goal_->setRotation(MapInfo::thetaToItemRotation(theta));
}

void MapView::clearGoal()
{
    if (goal_)
        goal_->setVisible(false);
}

// ================= 입력 =================

void MapView::wheelEvent(QWheelEvent *ev)
{
    if (!info_)
        return;
    const double factor = ev->angleDelta().y() > 0 ? 1.18 : 1.0 / 1.18;
    const double target = transform().m11() * factor;
    if (target < kMinScale || target > kMaxScale)
        return;

    // 커서 아래 지점을 직접 붙잡는다.
    //
    // AnchorUnderMouse 에 맡기면 확대할 때 화면이 엉뚱한 데로 튄다. 두 가지가
    // 겹치기 때문이다. 하나는 Qt 가 그 앵커에 쓰는 마우스 위치가 휠 이벤트의
    // 위치가 아니라 마지막 마우스 이동 위치라는 것, 다른 하나는 확대한 내용이
    // 뷰보다 작을 때 QGraphicsView 가 내용을 가운데로 되돌린다는 것이다.
    // 이 지도는 178 × 17 m 로 가로세로 비가 커서 세로는 늘 뷰보다 작고, 그래서
    // 확대할 때마다 세로 중심이 다시 잡히며 화면이 미끄러진다.
    const auto anchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);

    const QPointF before = mapToScene(ev->position().toPoint());
    scale(factor, factor);
    const QPointF after = mapToScene(ev->position().toPoint());
    translate(after.x() - before.x(), after.y() - before.y());

    setTransformationAnchor(anchor);
}

void MapView::mouseDoubleClickEvent(QMouseEvent *ev)
{
    // 마커나 포인트 위가 아닐 때만. 포인트를 두 번 누르는 것은 그 포인트에
    // 대한 동작이지 화면을 되돌리라는 뜻이 아니다.
    if (ev->button() == Qt::LeftButton && !itemAt(ev->position().toPoint()))
        emit fitRequested();
    QGraphicsView::mouseDoubleClickEvent(ev);
}

void MapView::mousePressEvent(QMouseEvent *ev)
{
    // 중클릭·우클릭은 모드와 무관하게 항상 패닝. 목표 지정 중에도 화면을
    // 옮길 수 있어야 한다.
    if (ev->button() == Qt::MiddleButton || ev->button() == Qt::RightButton) {
        panning_ = true;
        panAnchor_ = ev->position();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (ev->button() != Qt::LeftButton)
        return;

    if (mode_ == MapMode::View) {
        if (auto *wp = dynamic_cast<WaypointMarker *>(itemAt(ev->position().toPoint())))
            emit waypointClicked(wp->waypointId());
        return;
    }

    dragging_ = true;
    dragOrigin_ = mapToScene(ev->position().toPoint());
    dragCurrent_ = dragOrigin_;
    viewport()->update();
}

void MapView::mouseMoveEvent(QMouseEvent *ev)
{
    if (panning_) {
        const QPointF delta = ev->position() - panAnchor_;
        panAnchor_ = ev->position();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - int(delta.x()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() - int(delta.y()));
        return;
    }

    const QPointF sp = mapToScene(ev->position().toPoint());
    if (info_) {
        double wx = 0, wy = 0;
        info_->toWorld(sp.x(), sp.y(), wx, wy);
        emit cursorMoved(wx, wy);
    }

    if (dragging_) {
        dragCurrent_ = sp;
        viewport()->update();
    }
}

void MapView::mouseReleaseEvent(QMouseEvent *ev)
{
    if (panning_ && (ev->button() == Qt::MiddleButton || ev->button() == Qt::RightButton)) {
        panning_ = false;
        viewport()->setCursor(mode_ == MapMode::View ? Qt::ArrowCursor : Qt::CrossCursor);
        return;
    }

    if (ev->button() != Qt::LeftButton || !dragging_ || !info_)
        return;

    const QPointF origin = dragOrigin_;
    const QPointF end = mapToScene(ev->position().toPoint());
    dragging_ = false;
    viewport()->update();

    double wx = 0, wy = 0;
    info_->toWorld(origin.x(), origin.y(), wx, wy);

    const double dx = end.x() - origin.x();
    const double dy = end.y() - origin.y();
    double theta = 0.0;
    if (std::hypot(dx, dy) >= kHeadingDragThreshold) {
        // 씬 y 가 아래로 향하므로 부호를 뒤집어 world 각으로 변환한다.
        theta = std::atan2(-dy, dx);
    }

    if (mode_ == MapMode::SetGoal) {
        setGoal(wx, wy, theta);
        emit goalRequested(wx, wy, theta);
    } else if (mode_ == MapMode::AddWaypoint) {
        emit waypointPlaced(wx, wy, theta);
    }
    setMode(MapMode::View);
}

// ================= 오버레이 =================

void MapView::drawForeground(QPainter *p, const QRectF &)
{
    p->save();
    p->resetTransform();   // 화면 좌표로 그린다
    p->setRenderHint(QPainter::Antialiasing);

    // 맵이 없으면 빈 흰 판만 남아 고장처럼 보인다. 상태를 글로 알린다.
    if (!info_) {
        const Colors &C = colors();
        QFont f;
        p->setFont(f);
        p->setPen(QColor(C.textMute));
        p->drawText(viewport()->rect(), Qt::AlignCenter,
                    QStringLiteral("지도가 없습니다\n로봇에서 지도를 받으면 표시됩니다"));
        p->restore();
        return;
    }

    if (dragging_) {
        const QPointF o = mapFromScene(dragOrigin_);
        const QPointF c = mapFromScene(dragCurrent_);
        const QColor col(colors().accent);
        const double d = std::hypot(c.x() - o.x(), c.y() - o.y());

        // 화살표 길이는 고정한다. 끌수록 길어지게 두면 지도를 가로지르는
        // 선이 되는데, 그 길이가 뜻하는 것은 아무것도 없다 — 정하는 것은
        // 방향뿐이고 거리는 버려진다. 커서까지는 얇은 안내선만 잇는다.
        constexpr double kArrowLen = 42.0;
        constexpr double kHeadLen = 11.0;

        p->setPen(Qt::NoPen);
        p->setBrush(col);
        p->drawEllipse(o, 4, 4);

        if (d <= kHeadingDragThreshold) {
            // 아직 방향이 정해지지 않았다. 커서까지 점선만 보여준다.
            p->setPen(QPen(col, 1.2, Qt::DotLine));
            p->drawLine(o, c);
            return;
        }

        const double a = std::atan2(c.y() - o.y(), c.x() - o.x());
        const QPointF tip(o.x() + kArrowLen * std::cos(a), o.y() + kArrowLen * std::sin(a));

        // 커서가 화살표보다 멀면, 어디를 끌고 있는지 알 수 있게 옅은 선을 남긴다.
        if (d > kArrowLen) {
            QColor faint(col);
            faint.setAlpha(70);
            p->setPen(QPen(faint, 1.0, Qt::DotLine));
            p->drawLine(tip, c);
        }

        p->setPen(QPen(col, 2.0));
        p->drawLine(o, tip);

        p->setPen(Qt::NoPen);
        p->setBrush(col);
        p->drawPolygon(QPolygonF{
            tip,
            {tip.x() - kHeadLen * std::cos(a - 0.4), tip.y() - kHeadLen * std::sin(a - 0.4)},
            {tip.x() - kHeadLen * std::cos(a + 0.4), tip.y() - kHeadLen * std::sin(a + 0.4)}});
    }

    drawScaleBar(p);
    p->restore();
}

void MapView::drawScaleBar(QPainter *p)
{
    if (!info_)
        return;

    const double pxPerM = transform().m11() / info_->resolution;
    if (pxPerM <= 0)
        return;

    // 화면에서 110px 근처가 되는 '깔끔한' 미터 값을 고른다.
    const double raw = 110.0 / pxPerM;
    static const double kNice[] = {0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100};
    double nice = kNice[0];
    double best = 1e9;
    for (double v : kNice) {
        const double err = qAbs(std::log10(v / raw));
        if (err < best) {
            best = err;
            nice = v;
        }
    }

    const double lengthPx = nice * pxPerM;
    if (lengthPx <= 20 || lengthPx >= 400)
        return;

    const Colors &C = colors();
    const int x0 = 14;
    const int y0 = viewport()->height() - 18;

    p->setPen(QPen(QColor(C.textMute), 1.4));
    p->drawLine(x0, y0, int(x0 + lengthPx), y0);
    p->drawLine(x0, y0 - 3, x0, y0 + 3);
    p->drawLine(int(x0 + lengthPx), y0 - 3, int(x0 + lengthPx), y0 + 3);

    QFont f = monoFont(12);
    f.setPointSize(9);
    p->setFont(f);
    p->setPen(QColor(C.textMute));
    const QString label = nice >= 1 ? QStringLiteral("%1 m").arg(nice)
                                    : QStringLiteral("%1 cm").arg(nice * 100);
    p->drawText(QRectF(x0, y0 - 19, lengthPx, 14), Qt::AlignCenter, label);
}

}  // namespace gcs::map
