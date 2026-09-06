#include "panels/CapturePanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

#include "theme/Tokens.h"
#include "widgets/PreviewView.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::capture::CaptureMetadata;

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
    captureButton_->setProperty("variant", "primary");
    captureButton_->setMinimumHeight(38);
    connect(captureButton_, &QPushButton::clicked, this, [this] {
        capturedAt_ = QDateTime::currentDateTime();
        hasCapture_ = true;
        refreshDerived();
        emit captureRequested();
    });
    card_->body()->addWidget(captureButton_);

    hint_ = new QLabel;
    hint_->setObjectName(QStringLiteral("Hint"));
    hint_->setWordWrap(true);
    card_->body()->addWidget(hint_);

    // ---- 미리보기 ----
    auto *previews = new QHBoxLayout;
    previews->setSpacing(metrics::s2);
    preview2d_ = new PreviewView(QStringLiteral("2D"));
    preview3d_ = new PreviewView(QStringLiteral("3D"));
    previews->addWidget(preview2d_);
    previews->addWidget(preview3d_);
    card_->body()->addLayout(previews);

    // ---- 메타데이터 ----
    auto *metaCard = new Card(QStringLiteral("메타데이터"));
    outer->addWidget(metaCard);

    trainNumber_ = new QLineEdit;
    trainNumber_->setPlaceholderText(QStringLiteral("예: 1234"));
    carNumber_ = new QLineEdit;
    carNumber_->setPlaceholderText(QStringLiteral("예: 05"));
    pointId_ = new QLineEdit;
    pointId_->setPlaceholderText(QStringLiteral("예: C01-P03"));

    metaCard->body()->addWidget(fieldRow(QStringLiteral("편성번호"), trainNumber_, 76));
    metaCard->body()->addWidget(fieldRow(QStringLiteral("량번호"), carNumber_, 76));
    metaCard->body()->addWidget(fieldRow(QStringLiteral("포인트ID"), pointId_, 76));

    for (auto *e : {trainNumber_, carNumber_, pointId_})
        connect(e, &QLineEdit::textChanged, this, [this] { refreshDerived(); });

    metaCard->body()->addSpacing(metrics::s1);
    metaCard->body()->addWidget(sectionLabel(QStringLiteral("자동 기입")));
    autoFields_ = readout();
    autoFields_->setWordWrap(true);
    metaCard->body()->addWidget(autoFields_);

    metaCard->body()->addSpacing(metrics::s1);
    metaCard->body()->addWidget(sectionLabel(QStringLiteral("저장 파일명")));
    fileNamePreview_ = readout();
    fileNamePreview_->setWordWrap(true);
    metaCard->body()->addWidget(fileNamePreview_);

    saveButton_ = new QPushButton(QStringLiteral("저장"));
    saveButton_->setProperty("variant", "primary");
    connect(saveButton_, &QPushButton::clicked, this,
            [this] { emit saveRequested(currentMetadata()); });
    metaCard->body()->addWidget(saveButton_);

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
    captureButton_->setEnabled(allowed);
    if (allowed) {
        state_->set(QStringLiteral("촬영 가능"), QStringLiteral("ok"));
        hint_->setText(QStringLiteral(
            "정지 상태에서만 촬영합니다. 촬영 후 메타데이터를 확인하고 저장하십시오."));
    } else {
        state_->set(QStringLiteral("촬영 불가"), QStringLiteral("warn"));
        hint_->setText(reason.isEmpty()
                           ? QStringLiteral("로봇이 정지한 뒤 촬영할 수 있습니다.")
                           : reason);
    }
}

void CapturePanel::showPreview2d(const QImage &image)
{
    preview2d_->setImage(image);
}

void CapturePanel::showPreview3d(const QImage &image)
{
    preview3d_->setImage(image);
}

CaptureMetadata CapturePanel::currentMetadata() const
{
    CaptureMetadata m;
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
        QStringLiteral("좌표  %1, %2   θ %3°\nApriltag  %4")
            .arg(x_, 0, 'f', 2)
            .arg(y_, 0, 'f', 2)
            .arg(qRadiansToDegrees(theta_), 0, 'f', 1)
            .arg(tagId_ >= 0 ? QString::number(tagId_) : QStringLiteral("미인식")));

    const CaptureMetadata m = currentMetadata();

    // 촬영 전에는 저장할 것이 없다. 메타데이터만 채워도 저장이 열리면
    // 이미지 없는 기록이 생긴다.
    if (!hasCapture_) {
        fileNamePreview_->setText(QStringLiteral("촬영 후 결정됩니다"));
        saveButton_->setEnabled(false);
        return;
    }

    const QStringList missing = m.missingFields();
    if (!missing.isEmpty()) {
        // 파일명을 미리 보여주는 이유는, 규정 형식이 검수 항목이기 때문이다.
        // 조작자가 저장 전에 눈으로 확인할 수 있어야 한다.
        fileNamePreview_->setText(
            QStringLiteral("입력 필요: %1").arg(missing.join(QStringLiteral(", "))));
        saveButton_->setEnabled(false);
        return;
    }

    fileNamePreview_->setText(m.fileName(QStringLiteral("jpg")));
    saveButton_->setEnabled(true);
}

}  // namespace gcs::ui
