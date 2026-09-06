#include "panels/MissionPanel.h"

#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"
#include "widgets/WaypointDelegate.h"

namespace gcs::ui {

using namespace gcs::theme;

MissionPanel::MissionPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("점검 진행"), this);
    state_ = new Badge(QStringLiteral("대기"), QStringLiteral("neutral"));
    card_->addHeaderWidget(state_);
    outer->addWidget(card_);

    bar_ = new QProgressBar;
    bar_->setRange(0, 100);
    bar_->setValue(0);
    bar_->setTextVisible(false);
    bar_->setFixedHeight(metrics::s2);
    card_->body()->addWidget(bar_);

    count_ = readout(QStringLiteral("—"));
    card_->body()->addWidget(count_);

    card_->body()->addSpacing(metrics::s2);

    // 목록은 읽기 전용이다. 추가·삭제·순서 변경과 시작·정지는 위치
    // 화면이 맡는다. 운용 중 이 화면에서 목록을 건드릴 일은 없다.
    list_ = new QListWidget;
    list_->setItemDelegate(new WaypointDelegate(list_));
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setFocusPolicy(Qt::NoFocus);
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    card_->body()->addWidget(list_, 1);

    refresh();
}

void MissionPanel::setWaypoints(const QList<QVariantMap> &points)
{
    points_ = points;
    refresh();
}

void MissionPanel::setMissionState(const QString &state)
{
    missionState_ = state;
    refresh();
}

void MissionPanel::refresh()
{
    if (missionState_ == QLatin1String("running"))
        state_->set(QStringLiteral("점검 중"), QStringLiteral("info"));
    else if (missionState_ == QLatin1String("paused"))
        state_->set(QStringLiteral("일시정지"), QStringLiteral("warn"));
    else
        state_->set(QStringLiteral("대기"), QStringLiteral("neutral"));

    const int total = points_.size();
    int done = 0;
    int currentIdx = -1;
    for (int i = 0; i < total; ++i) {
        const QString st =
            points_.at(i).value(QStringLiteral("status")).toString();
        if (st == QLatin1String("done"))
            ++done;
        else if (st == QLatin1String("current") && currentIdx < 0)
            currentIdx = i;
    }

    bar_->setValue(total > 0 ? done * 100 / total : 0);
    count_->setText(total > 0 ? QStringLiteral("%1 / %2 완료").arg(done).arg(total)
                              : QStringLiteral("등록된 점검포인트 없음"));

    list_->clear();
    for (const auto &wp : points_) {
        auto *it = new QListWidgetItem;
        it->setData(kWaypointRole, wp);
        list_->addItem(it);
    }

    // 진행 중인 지점은 목록에서 스스로 보이게 한다. 스크롤을 조작자가
    // 따라가야 한다면 목록을 띄운 의미가 없다.
    if (currentIdx >= 0)
        list_->scrollToItem(list_->item(currentIdx), QAbstractItemView::PositionAtCenter);
}

}  // namespace gcs::ui
