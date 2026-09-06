#include "widgets/WaypointDelegate.h"

#include <QFont>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

#include "mapview/MapItems.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;

QString waypointStatusLabel(const QString &status)
{
    if (status == QLatin1String("done"))
        return QStringLiteral("완료");
    if (status == QLatin1String("current"))
        return QStringLiteral("진행");
    if (status == QLatin1String("error"))
        return QStringLiteral("오류");
    return QStringLiteral("대기");
}

QSize WaypointDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {0, kWaypointRowHeight};
}

void WaypointDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt,
                             const QModelIndex &idx) const
{
    const Colors &C = colors();
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    const QRect r = opt.rect;
    const QVariantMap d = idx.data(kWaypointRole).toMap();
    const QString status = d.value(QStringLiteral("status"),
                                   QStringLiteral("todo")).toString();
    const QColor col(hmi::map::waypointColor(status));

    if (opt.state & QStyle::StateFlag::State_Selected) {
        QColor sel(C.accent);
        sel.setAlpha(30);
        p->setPen(Qt::NoPen);
        p->setBrush(sel);
        p->drawRect(r);
    } else if (opt.state & QStyle::StateFlag::State_MouseOver) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(C.surfaceHi));
        p->drawRect(r);
    }

    // 상태 점 — 미완료만 속을 비운다. 지도 마커와 같은 규칙이라
    // 목록과 지도를 눈으로 대응시키기 쉽다.
    const int cy = r.center().y();
    const bool filled = status != QLatin1String("todo");
    p->setPen(filled ? QPen(Qt::NoPen) : QPen(col, 1.2));
    p->setBrush(filled ? QBrush(col) : QBrush(Qt::NoBrush));
    p->drawEllipse(r.left() + 10, cy - 4, 8, 8);

    QFont ft;
    ft.setPointSize(11);
    ft.setWeight(status == QLatin1String("current") ? QFont::DemiBold : QFont::Normal);
    p->setFont(ft);
    p->setPen(filled ? QColor(C.text) : QColor(C.textDim));
    p->drawText(r.adjusted(26, 3, -62, 0), Qt::AlignLeft | Qt::AlignTop,
                QStringLiteral("%1.  %2")
                    .arg(idx.row() + 1)
                    .arg(d.value(QStringLiteral("name"),
                                 d.value(QStringLiteral("id"))).toString()));

    QFont fm = monoFont(9);
    p->setFont(fm);
    p->setPen(QColor(C.textMute));
    QString sub = QStringLiteral("%1, %2")
                      .arg(d.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                      .arg(d.value(QStringLiteral("y")).toDouble(), 0, 'f', 2);
    if (d.contains(QStringLiteral("tag_id")))
        sub += QStringLiteral("   마커 %1").arg(d.value(QStringLiteral("tag_id")).toInt());
    p->drawText(r.adjusted(26, 0, -62, -3), Qt::AlignLeft | Qt::AlignBottom, sub);

    QFont fs;
    fs.setPointSize(9);
    p->setFont(fs);
    p->setPen(filled ? col : QColor(C.textMute));
    p->drawText(r.adjusted(0, 0, -10, 0), Qt::AlignRight | Qt::AlignVCenter,
                waypointStatusLabel(status));
    p->restore();
}

}  // namespace hmi::ui
