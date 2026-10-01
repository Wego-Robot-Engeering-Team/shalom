// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/NavigationSpeedPanel.h"

#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QVBoxLayout>

#include <cmath>
#include <algorithm>

#include "net/Channels.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

NavigationSpeedPanel::NavigationSpeedPanel(QWidget *parent, bool editRanges)
    : QWidget(parent), editRanges_(editRanges)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *card = new Card(editRanges_ ? QStringLiteral("속도 범위") : QStringLiteral("주행 속도"));
    if (editRanges_)
        card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    outer->addWidget(card);
    const QString prefix = editRanges_ ? QStringLiteral("NavigationSpeedSettings")
                                      : QStringLiteral("NavigationSpeed");
    if (editRanges_) {
        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(theme::metrics::s3);
        grid->setVerticalSpacing(theme::metrics::s3);
        grid->addWidget(sectionLabel(QStringLiteral("최소")), 0, 1);
        grid->addWidget(sectionLabel(QStringLiteral("최대")), 0, 2);
        grid->setColumnMinimumWidth(0, 64);
        for (int column = 1; column <= 2; ++column)
            grid->setColumnStretch(column, 1);
        const auto addRange = [this, grid](int row, const QString &label, const QString &unit,
                const QString &name, QDoubleSpinBox *&minimum, QDoubleSpinBox *&maximum) {
            grid->addWidget(new QLabel(label), row, 0);
            minimum = new QDoubleSpinBox;
            maximum = new QDoubleSpinBox;
            minimum->setObjectName(name + QStringLiteral("Minimum"));
            maximum->setObjectName(name + QStringLiteral("Maximum"));
            for (auto *input : {minimum, maximum}) {
                input->setDecimals(2);
                input->setSingleStep(0.05);
                input->setKeyboardTracking(false);
                input->setSuffix(unit);
                input->setMinimumHeight(36);
                input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
                input->setAccessibleName(label + (input == minimum ? QStringLiteral(" 최소") : QStringLiteral(" 최대")));
                connect(input, &QDoubleSpinBox::valueChanged, this, [this] {
                    updateDirty();
                    error_->hide();
                    refresh();
                });
            }
            grid->addWidget(minimum, row, 1);
            grid->addWidget(maximum, row, 2);
        };
        addRange(1, QStringLiteral("선속도"), QStringLiteral(" m/s"), prefix, minimum_, maximum_);
        addRange(2, QStringLiteral("각속도"), QStringLiteral(" rad/s"),
                 QStringLiteral("NavigationAngularSpeedSettings"), angularMinimum_, angularMaximum_);
        card->body()->addLayout(grid);
        adjustment_ = captionLabel({});
        adjustment_->setObjectName(prefix + QStringLiteral("Adjustment"));
        adjustment_->setWordWrap(true);
        card->body()->addWidget(adjustment_);
        card->body()->addSpacing(theme::metrics::s1);
        card->body()->addWidget(new HLine);
    } else {
        const auto addRow = [this, card](const QString &label, const QString &unit,
                const QString &name, QSlider *&slider, QDoubleSpinBox *&value) {
            auto *row = new QHBoxLayout;
            slider = new QSlider(Qt::Horizontal);
            slider->setObjectName(name + QStringLiteral("Slider"));
            value = new QDoubleSpinBox;
            value->setObjectName(name + QStringLiteral("Value"));
            value->setDecimals(2);
            value->setSingleStep(0.05);
            value->setKeyboardTracking(false);
            row->addWidget(new QLabel(label));
            row->addWidget(slider, 1);
            value->setSuffix(unit);
            row->addWidget(value);
            card->body()->addLayout(row);
            connect(slider, &QSlider::valueChanged, value,
                    [value](int position) { value->setValue(position / 100.0); });
            connect(value, &QDoubleSpinBox::valueChanged, this, [this, slider](double number) {
                const QSignalBlocker blocker(slider);
                slider->setValue(int(std::lround(number * 100.0)));
                updateDirty();
                error_->hide();
                refresh();
            });
        };
        addRow(QStringLiteral("선속도"), QStringLiteral(" m/s"), prefix, slider_, value_);
        addRow(QStringLiteral("각속도"), QStringLiteral(" rad/s"),
               QStringLiteral("NavigationAngularSpeed"), angularSlider_, angularValue_);
    }
    auto *footer = new QHBoxLayout;
    reported_ = new QLabel;
    reported_->setObjectName(prefix + QStringLiteral("Reported"));
    reported_->setWordWrap(true);
    footer->addWidget(reported_, 1);
    apply_ = new QPushButton(QStringLiteral("적용"));
    apply_->setObjectName(prefix + QStringLiteral("Apply"));
    apply_->setProperty("variant", "primary");
    if (editRanges_) {
        apply_->setMinimumWidth(80);
        apply_->setMinimumHeight(36);
    }
    footer->addWidget(apply_);
    card->body()->addLayout(footer);
    error_ = new QLabel;
    error_->setObjectName(prefix + QStringLiteral("Error"));
    error_->setWordWrap(true);
    error_->setProperty("tone", "danger");
    card->body()->addWidget(error_);

    connect(apply_, &QPushButton::clicked, this, [this] {
        pending_ = true;
        error_->hide();
        refresh();
        if (editRanges_)
            emit speedRangesRequested(minimum_->value(), maximum_->value(),
                                      angularMinimum_->value(), angularMaximum_->value());
        else
            emit speedLimitsRequested(value_->value(), angularValue_->value());
    });
    reset();
}

void NavigationSpeedPanel::reset()
{
    known_ = dirty_ = pending_ = false;
    limit_ = 0.0;
    angularLimit_ = 0.0;
    reportedMinimum_ = 0.10;
    reportedMaximum_ = 0.60;
    reportedAngularMinimum_ = 0.05;
    reportedAngularMaximum_ = 0.80;
    if (editRanges_) {
        const QSignalBlocker b1(minimum_), b2(maximum_), b3(angularMinimum_), b4(angularMaximum_);
        minimum_->setRange(0.10, 0.60);
        maximum_->setRange(0.10, 0.60);
        angularMinimum_->setRange(0.05, 0.80);
        angularMaximum_->setRange(0.05, 0.80);
        minimum_->setValue(0.10);
        maximum_->setValue(0.60);
        angularMinimum_->setValue(0.05);
        angularMaximum_->setValue(0.80);
    } else {
        const QSignalBlocker b1(value_), b2(slider_), b3(angularValue_), b4(angularSlider_);
        value_->setRange(0.10, 0.60);
        slider_->setRange(10, 60);
        value_->setValue(0.30);
        slider_->setValue(30);
        angularValue_->setRange(0.05, 0.80);
        angularSlider_->setRange(5, 80);
        angularValue_->setValue(0.50);
        angularSlider_->setValue(50);
    }
    reported_->setText(QStringLiteral("설정값 —"));
    error_->hide();
    refresh();
}

void NavigationSpeedPanel::setReportedLimits(double limit, double minimum, double maximum,
                                              double angular, double angularMinimum,
                                              double angularMaximum, bool autonomousApplied)
{
    if (!std::isfinite(limit) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        minimum <= 0 || maximum < minimum || limit < minimum || limit > maximum ||
        !std::isfinite(angular) || !std::isfinite(angularMinimum) ||
        !std::isfinite(angularMaximum) || angularMinimum <= 0 || angularMaximum < angularMinimum ||
        angular < angularMinimum || angular > angularMaximum)
        return;
    const bool replaceDraft = !known_ || (!dirty_ && !pending_);
    limit_ = limit;
    angularLimit_ = angular;
    reportedMinimum_ = minimum;
    reportedMaximum_ = maximum;
    reportedAngularMinimum_ = angularMinimum;
    reportedAngularMaximum_ = angularMaximum;
    known_ = true;
    if (!editRanges_) {
        const QSignalBlocker blocker(value_);
        const QSignalBlocker sliderBlocker(slider_);
        const QSignalBlocker angularBlocker(angularValue_);
        const QSignalBlocker angularSliderBlocker(angularSlider_);
        value_->setRange(minimum, maximum);
        slider_->setRange(int(std::ceil(minimum * 100)), int(std::floor(maximum * 100)));
        if (replaceDraft)
            value_->setValue(limit);
        slider_->setValue(int(std::lround(value_->value() * 100)));
        angularValue_->setRange(angularMinimum, angularMaximum);
        angularSlider_->setRange(int(std::ceil(angularMinimum * 100)),
                                int(std::floor(angularMaximum * 100)));
        if (replaceDraft)
            angularValue_->setValue(angular);
        angularSlider_->setValue(int(std::lround(angularValue_->value() * 100)));
    }
    if (editRanges_ && replaceDraft) {
        const QSignalBlocker b1(minimum_), b2(maximum_), b3(angularMinimum_), b4(angularMaximum_);
        minimum_->setValue(minimum);
        maximum_->setValue(maximum);
        angularMinimum_->setValue(angularMinimum);
        angularMaximum_->setValue(angularMaximum);
    }
    updateDirty();
    if (!editRanges_)
        reported_->setText(QStringLiteral("설정값 %1 m/s · %2 rad/s%3")
            .arg(limit, 0, 'f', 2).arg(angular, 0, 'f', 2)
            .arg(autonomousApplied ? QString{} : QStringLiteral(" · 자율주행 적용 대기")));
    refresh();
}

void NavigationSpeedPanel::handleCommandResult(const QString &channel, bool ok,
                                               const QString &, const QString &message)
{
    const auto expected = editRanges_ ? hmi::ch::kCmdNavigationSpeedSettings
                                      : hmi::ch::kCmdNavigationSpeedLimit;
    if (channel != QLatin1String(expected) || !pending_)
        return;
    pending_ = false;
    if (!ok) {
        error_->setText(message.isEmpty() ? QStringLiteral("속도 설정 실패") : message);
        error_->show();
    }
    refresh();
}

void NavigationSpeedPanel::refresh()
{
    bool valid = true;
    if (editRanges_) {
        for (auto *input : {minimum_, maximum_, angularMinimum_, angularMaximum_})
            input->setEnabled(known_ && !pending_);
        valid = minimum_->value() <= maximum_->value() &&
                angularMinimum_->value() <= angularMaximum_->value();
        if (known_ && !valid) {
            error_->setText(QStringLiteral("최소값은 최대값 이하로 입력하십시오"));
            error_->show();
        }
        QStringList changes;
        if (known_ && valid) {
            const double linear = std::clamp(limit_, minimum_->value(), maximum_->value());
            const double angular = std::clamp(angularLimit_, angularMinimum_->value(), angularMaximum_->value());
            if (std::abs(linear - limit_) > 0.0001)
                changes << QStringLiteral("선속도 %1 m/s").arg(linear, 0, 'f', 2);
            if (std::abs(angular - angularLimit_) > 0.0001)
                changes << QStringLiteral("각속도 %1 rad/s").arg(angular, 0, 'f', 2);
        }
        adjustment_->setText(QStringLiteral("적용 시 %1로 조정됩니다.").arg(changes.join(QStringLiteral(" · "))));
        adjustment_->setVisible(!changes.isEmpty());
        reported_->setText(!known_ ? QStringLiteral("로봇 연결 대기") :
            pending_ ? QStringLiteral("저장 중…") : dirty_ ? QStringLiteral("수정 중") : QStringLiteral("저장됨"));
    } else {
        slider_->setEnabled(known_ && !pending_);
        value_->setEnabled(known_ && !pending_);
        angularSlider_->setEnabled(known_ && !pending_);
        angularValue_->setEnabled(known_ && !pending_);
    }
    apply_->setEnabled(known_ && dirty_ && valid && !pending_);
}

void NavigationSpeedPanel::updateDirty()
{
    dirty_ = false;
    if (!known_)
        return;
    if (editRanges_)
        dirty_ = std::abs(minimum_->value() - reportedMinimum_) > 0.0001 ||
                 std::abs(maximum_->value() - reportedMaximum_) > 0.0001 ||
                 std::abs(angularMinimum_->value() - reportedAngularMinimum_) > 0.0001 ||
                 std::abs(angularMaximum_->value() - reportedAngularMaximum_) > 0.0001;
    else
        dirty_ = std::abs(value_->value() - limit_) > 0.0001 ||
                 std::abs(angularValue_->value() - angularLimit_) > 0.0001;
}

void NavigationSpeedPanel::restoreReported()
{
    if (editRanges_) {
        const QSignalBlocker b3(minimum_), b4(maximum_), b5(angularMinimum_), b6(angularMaximum_);
        minimum_->setValue(reportedMinimum_);
        maximum_->setValue(reportedMaximum_);
        angularMinimum_->setValue(reportedAngularMinimum_);
        angularMaximum_->setValue(reportedAngularMaximum_);
    } else {
        const QSignalBlocker b1(value_), b2(angularValue_);
        value_->setValue(limit_);
        angularValue_->setValue(angularLimit_);
        slider_->setValue(int(std::lround(limit_ * 100)));
        angularSlider_->setValue(int(std::lround(angularLimit_ * 100)));
    }
    dirty_ = false;
    error_->hide();
    refresh();
}

void NavigationSpeedPanel::discardDraft()
{
    if (known_ && !pending_)
        restoreReported();
}

}  // namespace hmi::ui
