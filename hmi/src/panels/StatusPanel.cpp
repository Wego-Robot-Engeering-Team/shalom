// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/StatusPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;

StatusPanel::StatusPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("주행 상태"));
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

void StatusPanel::setPose(double x, double y, double thetaDeg)
{
    pose_->setText(QStringLiteral("%1, %2   방향 %3°")
                       .arg(x, 7, 'f', 2)
                       .arg(y, 7, 'f', 2)
                       .arg(thetaDeg, 6, 'f', 1));
}

}  // namespace hmi::ui
