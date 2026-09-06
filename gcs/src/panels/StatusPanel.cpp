#include "panels/StatusPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;

StatusPanel::StatusPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("로봇 상태"));
    conn_ = new Badge(QStringLiteral("연결 끊김"), QStringLiteral("danger"));
    card_->addHeaderWidget(conn_);
    outer->addWidget(card_);

    // 주행 모드만 배지다. 나머지는 값이 자주 바뀌므로 배지로 만들면
    // 화면이 계속 깜빡인다.
    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(sectionLabel(QStringLiteral("주행 모드")));
    modeRow->addStretch(1);
    mode_ = new Badge(QStringLiteral("—"), QStringLiteral("neutral"));
    modeRow->addWidget(mode_);
    card_->body()->addLayout(modeRow);

    motion_ = addRow(QStringLiteral("움직임"));
    arm_ = addRow(QStringLiteral("로봇팔"));
    tag_ = addRow(QStringLiteral("인식된 마커"));

    card_->body()->addSpacing(metrics::s1);
    card_->body()->addWidget(new HLine);
    card_->body()->addWidget(sectionLabel(QStringLiteral("현재 위치")));
    pose_ = readout();
    card_->body()->addWidget(pose_);

    card_->body()->addStretch(1);
}

QLabel *StatusPanel::addRow(const QString &label)
{
    auto *row = new QHBoxLayout;
    row->addWidget(sectionLabel(label));
    row->addStretch(1);
    auto *value = readout();
    row->addWidget(value);
    card_->body()->addLayout(row);
    return value;
}

void StatusPanel::setConnected(bool ok)
{
    conn_->set(ok ? QStringLiteral("연결됨") : QStringLiteral("연결 끊김"),
               ok ? QStringLiteral("ok") : QStringLiteral("danger"));
}

void StatusPanel::setMode(const QString &mode, bool estop)
{
    // 비상정지 중에는 주행 모드가 조작자에게 의미 없는 정보다. 덮어쓴다.
    if (estop)
        mode_->set(QStringLiteral("비상정지"), QStringLiteral("danger"));
    else if (mode == QLatin1String("auto"))
        mode_->set(QStringLiteral("자율"), QStringLiteral("info"));
    else if (mode == QLatin1String("manual"))
        mode_->set(QStringLiteral("수동"), QStringLiteral("warn"));
    else
        mode_->set(mode.isEmpty() ? QStringLiteral("—") : mode, QStringLiteral("neutral"));
}

void StatusPanel::setMotion(double speedMps)
{
    // 판정 기준은 위치 등록의 정지 판정과 같은 값을 쓴다. 한 화면에서
    // "정지 중"이라고 하는데 등록은 막히는 일이 없어야 한다.
    motion_->setText(speedMps < 0.05
                         ? QStringLiteral("정지 중")
                         : QStringLiteral("이동 중  %1 m/s").arg(speedMps, 0, 'f', 2));
}

void StatusPanel::setArmState(const QString &state)
{
    if (state == QLatin1String("executing"))
        arm_->setText(QStringLiteral("움직이는 중"));
    else if (state == QLatin1String("planning"))
        arm_->setText(QStringLiteral("경로 계산 중"));
    else if (state == QLatin1String("error"))
        arm_->setText(QStringLiteral("오류"));
    else
        arm_->setText(QStringLiteral("대기"));
}

void StatusPanel::setPose(double x, double y, double thetaDeg)
{
    pose_->setText(QStringLiteral("%1, %2   방향 %3°")
                       .arg(x, 7, 'f', 2)
                       .arg(y, 7, 'f', 2)
                       .arg(thetaDeg, 6, 'f', 1));
}

void StatusPanel::setTagsSeen(int count)
{
    tag_->setText(count > 0 ? QStringLiteral("%1개").arg(count)
                            : QStringLiteral("없음"));
}

}  // namespace gcs::ui
