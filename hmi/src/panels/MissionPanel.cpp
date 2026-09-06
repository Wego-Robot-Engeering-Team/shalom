#include "panels/MissionPanel.h"

#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;

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

    // 전체 목록은 위치 화면에 있다. 여기서는 지금과 다음만 말한다.
    card_->body()->addWidget(sectionLabel(QStringLiteral("지금 점검 중")));
    current_ = new QLabel(QStringLiteral("—"));
    current_->setWordWrap(true);
    card_->body()->addWidget(current_);

    card_->body()->addSpacing(metrics::s1);
    card_->body()->addWidget(sectionLabel(QStringLiteral("다음")));
    next_ = new QLabel(QStringLiteral("—"));
    next_->setObjectName(QStringLiteral("Hint"));
    next_->setWordWrap(true);
    card_->body()->addWidget(next_);

    // 조작은 읽은 것 바로 밑에 둔다. 예전에는 카드 바닥까지 밀어 두었는데,
    // 목록을 빼고 나니 읽을 것과 누를 것 사이가 한 뼘 넘게 비었다.
    card_->body()->addSpacing(metrics::s3);
    auto *run = new QHBoxLayout;
    run->setSpacing(metrics::s2);

    // 시작·일시정지·재개는 같은 하나의 일이 이어지는 것이라 버튼 하나로 둔다.
    // 넷을 늘어놓으면 늘 둘은 눌리지 않는 상태로 남고, 그중 "일시정지" 와
    // "정지" 는 이름만으로 구분되지 않았다.
    //
    // 점검을 미는 것(시작·일시정지·재개)이 한 줄, 그 밖의 두 가지가 아랫줄이다.
    // 셋을 한 줄에 늘어놓으면 글자가 상자에 걸리고, 무엇이 주된 조작인지도
    // 흐려진다.
    //
    // 아랫줄 둘은 자리를 바꾸지 않는다. 앞서 취소와 복귀가 상태에 따라
    // 같은 칸을 번갈아 쓰게 해 봤더니, 규칙을 설명할 수가 없었다. 둘은
    // 서로의 대체재가 아니다 — 하나는 점검을 끝내는 일이고, 하나는 로봇을
    // 집으로 보내는 일이다.
    run_ = new QPushButton;
    run_->setProperty("variant", "primary");
    run_->setProperty("size", "sm");
    run->addWidget(run_);
    card_->body()->addLayout(run);

    auto *aux = new QHBoxLayout;
    aux->setSpacing(metrics::s2);
    stop_ = new QPushButton(QStringLiteral("점검 취소"));
    dock_ = new QPushButton(QStringLiteral("충전소 복귀"));
    for (auto *b : {stop_, dock_}) {
        b->setProperty("size", "sm");
        aux->addWidget(b);
    }
    card_->body()->addSpacing(metrics::s2);
    card_->body()->addLayout(aux);

    connect(run_, &QPushButton::clicked, this, &MissionPanel::onRunClicked);
    connect(stop_, &QPushButton::clicked, this, &MissionPanel::confirmStop);
    connect(dock_, &QPushButton::clicked, this, &MissionPanel::returnToDock);

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

void MissionPanel::setDockKnown(bool known)
{
    dockKnown_ = known;
    refresh();
}

void MissionPanel::refresh()
{
    const bool running = missionState_ != QLatin1String("idle");
    const bool paused = missionState_ == QLatin1String("paused");

    if (paused)
        state_->set(QStringLiteral("일시정지"), QStringLiteral("warn"));
    else if (running)
        state_->set(QStringLiteral("점검 중"), QStringLiteral("info"));
    else
        state_->set(QStringLiteral("대기"), QStringLiteral("neutral"));

    run_->setText(!running ? QStringLiteral("자율주행 시작")
                  : paused ? QStringLiteral("재개")
                           : QStringLiteral("일시정지"));

    // 취소할 점검이 없을 때도 자리는 지킨다. 버튼이 사라졌다 나타나면
    // 손이 자리를 외우지 못한다.
    stop_->setEnabled(running);
    dock_->setEnabled(dockKnown_);
    dock_->setToolTip(
        !dockKnown_ ? QStringLiteral("충전 스테이션 위치가 등록되어 있지 않습니다.")
        : running   ? QStringLiteral("점검을 일시정지하고 충전 스테이션으로 돌아갑니다.\n"
                                     "진행 상황은 그대로 두므로 충전 뒤에 이어서 할 수 있습니다.")
                    : QStringLiteral("충전 스테이션까지 자율 주행으로 돌아갑니다."));

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

    const auto nameAt = [this](int i) {
        return points_.at(i)
            .value(QStringLiteral("name"), points_.at(i).value(QStringLiteral("id")))
            .toString();
    };

    // 진행 중인 지점이 없으면 아직 시작하지 않았거나 이미 끝난 것이다.
    // 둘을 같은 "—" 로 뭉뚱그리면 조작자가 상태를 오해한다.
    if (currentIdx >= 0) {
        current_->setText(QStringLiteral("%1. %2").arg(currentIdx + 1).arg(nameAt(currentIdx)));
        next_->setText(currentIdx + 1 < total
                           ? QStringLiteral("%1. %2").arg(currentIdx + 2).arg(nameAt(currentIdx + 1))
                           : QStringLiteral("마지막 지점입니다"));
    } else if (total > 0 && done == total) {
        current_->setText(QStringLiteral("전체 점검 완료"));
        next_->setText(QStringLiteral("충전 스테이션으로 복귀"));
    } else {
        current_->setText(QStringLiteral("아직 시작하지 않았습니다"));
        next_->setText(total > 0 ? QStringLiteral("1. %1").arg(nameAt(0)) : QStringLiteral("—"));
    }

    // 세우는 것과 그만두는 것의 차이는 누른 뒤에야 드러난다. 누르기 전에,
    // 몇 번 지점이 걸려 있는지까지 넣어서 말해 둔다.
    const int resumeAt = (currentIdx >= 0 ? currentIdx : done) + 1;
    run_->setToolTip(
        !running ? QStringLiteral("1번 지점부터 점검을 시작합니다.")
        : paused ? QStringLiteral("%1번 지점부터 이어서 점검합니다.").arg(resumeAt)
                 : QStringLiteral("로봇이 그 자리에 섭니다. 진행 상황은 그대로 두고,\n"
                                  "재개하면 %1번 지점부터 이어서 합니다.")
                       .arg(resumeAt));
    stop_->setToolTip(
        QStringLiteral("점검을 끝냅니다. 진행 상황(%1 / %2)은 지워지고,\n"
                       "다시 시작하면 1번 지점부터입니다.")
            .arg(done)
            .arg(total));
}

void MissionPanel::onRunClicked()
{
    if (missionState_ == QLatin1String("idle"))
        emit missionStart();
    else if (missionState_ == QLatin1String("paused"))
        emit missionResume();
    else
        emit missionPause();
}

void MissionPanel::confirmStop()
{
    int done = 0;
    for (const auto &p : points_)
        if (p.value(QStringLiteral("status")).toString() == QLatin1String("done"))
            ++done;

    // 버릴 것이 없으면 묻지 않는다. 확인 창이 늘 뜨면 읽지 않고 누르게 된다.
    if (done > 0) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("점검 취소"),
            QStringLiteral("점검을 취소합니다.\n\n"
                           "지금까지의 진행(%1 / %2)은 지워지고,\n"
                           "다시 시작하면 1번 지점부터입니다.\n\n"
                           "이어서 하려면 재개를 누르십시오.")
                .arg(done)
                .arg(points_.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }
    emit missionStop();
}

}  // namespace hmi::ui
