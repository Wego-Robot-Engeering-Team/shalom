#include "widgets/MapLegend.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include "mapview/MapItems.h"
#include "theme/Tokens.h"

namespace hmi::ui {

using namespace hmi::theme;

namespace {

constexpr int kRowH = 22;
constexpr int kPad = 10;
constexpr int kSymbolX = 16;
constexpr int kTextX = 32;
constexpr int kWidth = 158;

struct Row {
    MapLegend::Item item;
    const char *label;
    const char *help;
    bool clickable;
};

const Row kRows[] = {
    {MapLegend::Item::Waypoint, "점검포인트",
     "로봇이 서서 촬영하는 자리입니다. 속이 빈 원은 아직 촬영 전, 채운 원은\n"
     "완료입니다.\n\n눌러서 목록을 열고 추가·삭제·순서를 바꿉니다.", true},
    {MapLegend::Item::Marker, "마커",
     "차량과 구조물에 붙은 AprilTag 입니다. 로봇이 이것을 보고 자기 위치를\n"
     "정밀하게 맞춥니다.\n\n현장에 물리적으로 붙는 것이라 화면에서 옮길 수 없습니다.",
     false},
    {MapLegend::Item::Robot, "로봇",
     "지금 로봇이 있는 자리와 바라보는 방향입니다.\n"
     "흐리게 보이면 위치 정보가 오래된 것입니다.", false},
    {MapLegend::Item::Path, "경로",
     "실선은 로봇이 가려는 길, 점선은 지나온 길입니다.", false},
};

constexpr int kRowCount = int(std::size(kRows));

}  // namespace

MapLegend::MapLegend(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("MapOverlay"));
    setFixedSize(kWidth, kPad * 2 + kRowCount * kRowH);
    setMouseTracking(true);
}

int MapLegend::rowAt(const QPoint &p) const
{
    const int i = (p.y() - kPad) / kRowH;
    return (i >= 0 && i < kRowCount) ? i : -1;
}

void MapLegend::mouseMoveEvent(QMouseEvent *ev)
{
    const int row = rowAt(ev->pos());
    if (row != hovered_) {
        hovered_ = row;
        setCursor(row >= 0 && kRows[row].clickable ? Qt::PointingHandCursor
                                                   : Qt::ArrowCursor);
        setToolTip(row >= 0 ? QString::fromUtf8(kRows[row].help) : QString());
        update();
    }
    QWidget::mouseMoveEvent(ev);
}

void MapLegend::leaveEvent(QEvent *ev)
{
    hovered_ = -1;
    update();
    QWidget::leaveEvent(ev);
}

void MapLegend::mousePressEvent(QMouseEvent *ev)
{
    const int row = rowAt(ev->pos());
    if (row >= 0 && kRows[row].clickable)
        emit manageRequested(kRows[row].item);
    ev->accept();
}

void MapLegend::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QFont f;
    f.setPointSize(10);
    p.setFont(f);

    for (int i = 0; i < kRowCount; ++i) {
        const QRect row(4, kPad + i * kRowH, width() - 8, kRowH);
        const bool hot = i == hovered_;

        if (hot && kRows[i].clickable) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(C.surfaceHi));
            p.drawRoundedRect(row, metrics::rSm, metrics::rSm);
        }

        const double cy = row.center().y();

        // 기호는 지도에 그려지는 것과 같은 모양으로 그린다. 범례가 실제와
        // 다르면 없느니만 못하다.
        switch (kRows[i].item) {
        case Item::Waypoint: {
            p.setPen(QPen(QColor(hmi::map::waypointColor(QStringLiteral("todo"))), 1.4));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QPointF(kSymbolX, cy), 4.5, 4.5);
            break;
        }
        case Item::Marker: {
            p.setPen(QPen(QColor(C.tag), 1.4));
            p.setBrush(Qt::NoBrush);
            QPainterPath diamond;
            diamond.moveTo(kSymbolX, cy - 5);
            diamond.lineTo(kSymbolX + 5, cy);
            diamond.lineTo(kSymbolX, cy + 5);
            diamond.lineTo(kSymbolX - 5, cy);
            diamond.closeSubpath();
            p.drawPath(diamond);
            break;
        }
        case Item::Robot: {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(C.accent));
            QPainterPath tri;
            tri.moveTo(kSymbolX + 5, cy);
            tri.lineTo(kSymbolX - 4, cy - 4.5);
            tri.lineTo(kSymbolX - 4, cy + 4.5);
            tri.closeSubpath();
            p.drawPath(tri);
            break;
        }
        case Item::Path: {
            p.setPen(QPen(QColor(C.plan), 1.8));
            p.drawLine(QPointF(kSymbolX - 6, cy - 2), QPointF(kSymbolX + 6, cy - 2));
            QPen dashed(QColor(C.trail), 1.4);
            dashed.setStyle(Qt::DashLine);
            p.setPen(dashed);
            p.drawLine(QPointF(kSymbolX - 6, cy + 3), QPointF(kSymbolX + 6, cy + 3));
            break;
        }
        }

        p.setPen(QColor(hot ? C.text : C.textDim));
        p.drawText(QRect(kTextX, row.top(), width() - kTextX - 6, row.height()),
                   Qt::AlignLeft | Qt::AlignVCenter, QString::fromUtf8(kRows[i].label));
    }
}

}  // namespace hmi::ui
