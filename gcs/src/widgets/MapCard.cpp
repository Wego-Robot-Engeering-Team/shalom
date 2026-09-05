#include "widgets/MapCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "mapview/MapView.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::map::MapView;

MapCard::MapCard(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("Card"));

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(1, 1, 1, 1);
    view_ = new MapView;
    lay->addWidget(view_);

    // 지도 위에 떠 있는 툴바. 레이아웃에 넣지 않고 직접 배치하므로
    // 지도 면적을 잡아먹지 않는다.
    toolbar_ = new QWidget(this);
    toolbar_->setObjectName(QStringLiteral("MapOverlay"));
    auto *tb = new QHBoxLayout(toolbar_);
    tb->setContentsMargins(metrics::s2, metrics::s2, metrics::s2, metrics::s2);
    tb->setSpacing(metrics::s2);

    goal_ = new QPushButton(QStringLiteral("목표 지정"));
    fit_ = new QPushButton(QStringLiteral("전체 보기"));
    for (auto *b : {goal_, fit_}) {
        b->setProperty("size", "sm");
        tb->addWidget(b);
    }
    goal_->setCheckable(true);

    tb->addSpacing(metrics::s2);
    mapLabel_ = sectionLabel(QStringLiteral("맵 없음"));
    tb->addWidget(mapLabel_);

    readout_ = new QLabel(QStringLiteral("—"), this);
    readout_->setObjectName(QStringLiteral("MapReadout"));
    readout_->setAlignment(Qt::AlignCenter);
    readout_->setMinimumWidth(150);

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("MapReadout"));
    hint_->setAlignment(Qt::AlignCenter);
    hint_->hide();

    connect(fit_, &QPushButton::clicked, view_, &MapView::fitMap);
    connect(view_, &MapView::cursorMoved, this, [this](double x, double y) {
        readout_->setText(QStringLiteral("%1, %2").arg(x, 7, 'f', 2).arg(y, 7, 'f', 2));
    });
}

void MapCard::setMapLabel(const QString &mapId, const QString &extent)
{
    mapLabel_->setText(QStringLiteral("%1 · %2").arg(mapId, extent));
    toolbar_->adjustSize();
}

void MapCard::setPlacementHint(const QString &text)
{
    // 지도가 배치 대기 상태임을 알린다. 커서만 십자로 바뀌면
    // 무엇을 찍으려던 참인지 잊는다.
    hint_->setText(text);
    hint_->setVisible(!text.isEmpty());
    if (!text.isEmpty()) {
        hint_->adjustSize();
        hint_->move((width() - hint_->width()) / 2, metrics::s3);
    }
}

void MapCard::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    toolbar_->adjustSize();
    toolbar_->move(metrics::s3, metrics::s3);
    readout_->adjustSize();
    readout_->move(width() - readout_->width() - metrics::s3, metrics::s3);
    if (hint_->isVisible()) {
        hint_->adjustSize();
        hint_->move((width() - hint_->width()) / 2, metrics::s3);
    }
}

}  // namespace gcs::ui
