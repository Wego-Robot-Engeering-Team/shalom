#include "widgets/MapCard.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QEvent>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "mapview/MapView.h"
#include "theme/Tokens.h"
#include "widgets/MapLegend.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::map::MapView;

MapCard::MapCard(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("Card"));
    // 다른 카드는 QFrame 이라 스타일시트의 테두리가 그려지지만, 이 카드는
    // QWidget 이라 무시된다. 그래서 지도만 윤곽 없이 떠 있었다.
    setAttribute(Qt::WA_StyledBackground, true);

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
    toolbarRow_ = toolbar_;

    // 목표 지정은 켜고 끄는 도구다. 주행 모드 버튼과 같은 모양이면 둘이
    // 같은 성격으로 보이는데, 하나는 로봇의 동작 방식을 바꾸고 하나는
    // 지도에서 클릭이 무엇을 뜻하는지만 바꾼다.
    goal_ = new QPushButton(QStringLiteral("목표 지정"));
    goal_->setProperty("size", "sm");
    goal_->setCheckable(true);
    goal_->setToolTip(QStringLiteral("켠 뒤 지도를 클릭해 목표를 지정합니다"));
    tb->addWidget(goal_);

    // "전체 보기" 버튼은 뺐다. 확대를 되돌리는 일이 툴바 한 자리를 늘 차지할
    // 만큼 잦지 않다. 지도를 두 번 누르면 같은 일을 한다.
    tb->addSpacing(metrics::s3);
    mapLabel_ = new QLabel(QStringLiteral("지도 없음"));
    mapLabel_->setObjectName(QStringLiteral("Hint"));
    tb->addWidget(mapLabel_);

    // 범례. 한 번 묻고 마는 것이라 구석에 작게 둔다.
    legend_ = new MapLegend(this);

    readout_ = new QLabel(this);
    readout_->setObjectName(QStringLiteral("MapReadout"));
    readout_->setAlignment(Qt::AlignCenter);
    readout_->setMinimumWidth(150);
    // 커서가 지도 위에 올라와야 나타난다. 값 없는 상자가 떠 있으면
    // 고장난 것처럼 보인다.
    readout_->hide();

    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("MapReadout"));
    hint_->setAlignment(Qt::AlignCenter);
    hint_->hide();

    connect(view_, &MapView::fitRequested, view_, &MapView::fitMap);
    connect(view_, &MapView::cursorMoved, this, [this](double x, double y) {
        readout_->setText(QStringLiteral("%1, %2").arg(x, 7, 'f', 2).arg(y, 7, 'f', 2));
        readout_->show();
        readout_->adjustSize();
        readout_->move(width() - readout_->width() - metrics::s3, metrics::s3);
    });
}

void MapCard::addModeButtons(QWidget *autoBtn, QWidget *manualBtn)
{
    auto *tb = qobject_cast<QHBoxLayout *>(toolbarRow_->layout());
    if (!tb)
        return;
    // 맨 앞에 넣고 선으로 떼어 놓는다. 지도를 어떻게 볼지(목표 지정,
    // 전체 보기)와 로봇이 어떻게 움직일지는 다른 이야기다.
    tb->insertWidget(0, autoBtn);
    tb->insertWidget(1, manualBtn);
    tb->insertSpacing(2, metrics::s1);
    tb->insertWidget(3, new VLine(nullptr, metrics::s1));
    tb->insertSpacing(4, metrics::s1);
    toolbar_->adjustSize();
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

void MapCard::leaveEvent(QEvent *ev)
{
    readout_->hide();
    QWidget::leaveEvent(ev);
}

void MapCard::resizeEvent(QResizeEvent *ev)
{
    QWidget::resizeEvent(ev);
    toolbar_->adjustSize();
    toolbar_->move(metrics::s3, metrics::s3);

    legend_->move(width() - legend_->width() - metrics::s3,
                  height() - legend_->height() - metrics::s3);
    if (readout_->isVisible()) {
        readout_->adjustSize();
        readout_->move(width() - readout_->width() - metrics::s3, metrics::s3);
    }
    if (hint_->isVisible()) {
        hint_->adjustSize();
        hint_->move((width() - hint_->width()) / 2, metrics::s3);
    }
}

}  // namespace gcs::ui
