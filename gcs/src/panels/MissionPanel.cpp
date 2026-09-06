#include "panels/MissionPanel.h"

#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

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
    card_->body()->addWidget(sectionLabel(QStringLiteral("지금 점검 중")));
    current_ = new QLabel(QStringLiteral("—"));
    current_->setWordWrap(true);
    card_->body()->addWidget(current_);

    card_->body()->addWidget(sectionLabel(QStringLiteral("다음 지점")));
    next_ = new QLabel(QStringLiteral("—"));
    next_->setWordWrap(true);
    card_->body()->addWidget(next_);

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

    // 진행 중인 지점이 없으면 아직 시작하지 않았거나 이미 끝난 것이다.
    // 두 경우를 같은 "—" 로 뭉뚱그리면 조작자가 상태를 오해한다.
    const auto nameAt = [this](int i) {
        return points_.at(i)
            .value(QStringLiteral("name"),
                   points_.at(i).value(QStringLiteral("id")))
            .toString();
    };

    if (currentIdx >= 0) {
        current_->setText(QStringLiteral("%1. %2").arg(currentIdx + 1).arg(nameAt(currentIdx)));
        next_->setText(currentIdx + 1 < total
                           ? QStringLiteral("%1. %2").arg(currentIdx + 2).arg(nameAt(currentIdx + 1))
                           : QStringLiteral("마지막 지점입니다"));
    } else if (total > 0 && done == total) {
        current_->setText(QStringLiteral("전체 점검 완료"));
        next_->setText(QStringLiteral("—"));
    } else {
        current_->setText(QStringLiteral("아직 시작하지 않았습니다"));
        next_->setText(total > 0 ? QStringLiteral("1. %1").arg(nameAt(0))
                                 : QStringLiteral("—"));
    }
}

}  // namespace gcs::ui
