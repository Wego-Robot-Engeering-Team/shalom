// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/CapturePanel.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

#include "theme/Tokens.h"
#include "widgets/PreviewView.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;
using hmi::capture::CaptureMetadata;

CapturePanel::CapturePanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(metrics::s3);

    // ---- 촬영 ----
    card_ = new Card(QStringLiteral("촬영 제어"));
    state_ = new Badge(QStringLiteral("대기"), QStringLiteral("neutral"));
    card_->addHeaderWidget(state_);
    outer->addWidget(card_);

    captureButton_ = new QPushButton(QStringLiteral("촬영"));
    captureButton_->setObjectName(QStringLiteral("CaptureTriggerButton"));
    captureButton_->setProperty("variant", "primary");
    captureButton_->setMinimumHeight(38);
    connect(captureButton_, &QPushButton::clicked, this, [this] {
        if (!captureButton_->isEnabled())
            return;
        capturedAt_ = QDateTime::currentDateTime();
        capturePending_ = true;
        captureStored_ = false;
        captureError_.clear();
        savedFileName_.clear();
        preview2d_->clear();
        refreshDerived();
        emit captureRequested();
    });
    card_->body()->addWidget(captureButton_);

    hint_ = new QLabel;
    hint_->setObjectName(QStringLiteral("Hint"));
    hint_->setWordWrap(true);
    card_->body()->addWidget(hint_);
    blockedReason_ = QStringLiteral("로봇 연결 필요");

    // ---- 미리보기 ----
    preview2d_ = new PreviewView(QStringLiteral("2D"));
    card_->body()->addWidget(preview2d_);

    // ---- 메타데이터 ----
    auto *metaCard = new Card(QStringLiteral("메타데이터"));
    outer->addWidget(metaCard);

    vehicleNumber_ = new QLineEdit;
    vehicleNumber_->setPlaceholderText(QStringLiteral("예: GTXA-042"));
    trainNumber_ = new QLineEdit;
    trainNumber_->setPlaceholderText(QStringLiteral("예: 1234"));
    carNumber_ = new QLineEdit;
    carNumber_->setPlaceholderText(QStringLiteral("예: 05"));
    pointId_ = new QLineEdit;
    pointId_->setPlaceholderText(QStringLiteral("예: C01-P03"));

    metaCard->body()->addWidget(fieldRow(QStringLiteral("차량번호"), vehicleNumber_, 76));
    metaCard->body()->addWidget(fieldRow(QStringLiteral("편성번호"), trainNumber_, 76));
    metaCard->body()->addWidget(fieldRow(QStringLiteral("량번호"), carNumber_, 76));
    metaCard->body()->addWidget(fieldRow(QStringLiteral("포인트ID"), pointId_, 76));

    for (auto *e : {vehicleNumber_, trainNumber_, carNumber_, pointId_})
        connect(e, &QLineEdit::textChanged, this, [this] { refreshDerived(); });

    metaCard->body()->addSpacing(metrics::s1);
    metaCard->body()->addWidget(sectionLabel(QStringLiteral("로봇 정보")));
    autoFields_ = readout();
    autoFields_->setWordWrap(true);
    metaCard->body()->addWidget(autoFields_);

    metaCard->body()->addSpacing(metrics::s1);
    metaCard->body()->addWidget(sectionLabel(QStringLiteral("저장 파일명")));
    fileNamePreview_ = readout();
    fileNamePreview_->setWordWrap(true);
    metaCard->body()->addWidget(fileNamePreview_);

    outer->addStretch(1);
    refreshDerived();
}

void CapturePanel::setContext(double x, double y, double theta, int visibleTagId)
{
    x_ = x;
    y_ = y;
    theta_ = theta;
    tagId_ = visibleTagId;
    refreshDerived();
}

void CapturePanel::setCaptureAllowed(bool allowed, const QString &reason)
{
    captureAllowed_ = allowed;
    blockedReason_ = reason;
    refreshDerived();
}

void CapturePanel::captureStored()
{
    capturePending_ = false;
    captureStored_ = true;
    captureError_.clear();
    refreshDerived();
}

void CapturePanel::captureFailed(const QString &reason)
{
    capturePending_ = false;
    captureStored_ = false;
    savedFileName_.clear();
    captureError_ = reason;
    refreshDerived();
}

void CapturePanel::setSavedFileName(const QString &fileName)
{
    if (!captureStored_)
        return;
    savedFileName_ = fileName;
    refreshDerived();
}

void CapturePanel::resetCapture()
{
    capturePending_ = false;
    captureStored_ = false;
    captureAllowed_ = false;
    blockedReason_ = QStringLiteral("로봇 연결 필요");
    captureError_.clear();
    savedFileName_.clear();
    capturedAt_ = {};
    preview2d_->clear();
    refreshDerived();
}

void CapturePanel::showPreview2d(const QImage &image)
{
    preview2d_->setImage(image);
}

CaptureMetadata CapturePanel::currentMetadata() const
{
    CaptureMetadata m;
    m.vehicleNumber = vehicleNumber_->text().trimmed();
    m.trainNumber = trainNumber_->text().trimmed();
    m.carNumber = carNumber_->text().trimmed();
    m.pointId = pointId_->text().trimmed();
    m.tagId = tagId_;
    m.capturedAt = capturedAt_;
    m.robotX = x_;
    m.robotY = y_;
    m.robotTheta = theta_;
    return m;
}

void CapturePanel::refreshDerived()
{
    autoFields_->setText(
        QStringLiteral("위치  %1, %2   방향 %3°\n마커  %4")
            .arg(x_, 0, 'f', 2)
            .arg(y_, 0, 'f', 2)
            .arg(qRadiansToDegrees(theta_), 0, 'f', 1)
            .arg(tagId_ >= 0 ? QString::number(tagId_) : QStringLiteral("미인식")));

    CaptureMetadata m = currentMetadata();
    m.capturedAt = QDateTime::currentDateTime();
    const QStringList missing = m.missingFields();
    captureButton_->setEnabled(captureAllowed_ && !capturePending_ && missing.isEmpty());
    if (capturePending_) {
        state_->set(QStringLiteral("저장 중"), QStringLiteral("info"));
        hint_->clear();
        fileNamePreview_->setText(QStringLiteral("—"));
    } else if (!captureError_.isEmpty()) {
        state_->set(QStringLiteral("촬영 실패"), QStringLiteral("danger"));
        hint_->setText(captureError_);
        fileNamePreview_->setText(QStringLiteral("저장된 파일 없음"));
    } else if (captureStored_) {
        state_->set(QStringLiteral("저장 완료"), QStringLiteral("ok"));
        hint_->clear();
        fileNamePreview_->setText(savedFileName_.isEmpty()
            ? QStringLiteral("—") : savedFileName_);
    } else if (!captureAllowed_) {
        state_->set(QStringLiteral("촬영 불가"), QStringLiteral("warn"));
        hint_->setText(blockedReason_);
        fileNamePreview_->setText(QStringLiteral("—"));
    } else if (!missing.isEmpty()) {
        state_->set(QStringLiteral("정보 입력 필요"), QStringLiteral("warn"));
        hint_->setText(QStringLiteral("필수 정보: %1").arg(missing.join(QStringLiteral(", "))));
        fileNamePreview_->setText(QStringLiteral("—"));
    } else {
        state_->set(QStringLiteral("촬영 가능"), QStringLiteral("ok"));
        hint_->clear();
        fileNamePreview_->setText(QStringLiteral("—"));
    }
    hint_->setVisible(!hint_->text().isEmpty());
}

}  // namespace hmi::ui
