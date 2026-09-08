#include "panels/CapturePanel.h"

#include <QGridLayout>
#include <QComboBox>
#include <QHBoxLayout>
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

    // ---- 라이브 뷰 ----
    //
    // 팔을 겨눌 때 보는 화면이다. 이것이 없으면 조작자는 찍고 나서야
    // 빗나간 것을 안다. 촬영 버튼 위에 두는 이유도 그것이다 — 보고, 맞추고,
    // 찍는 순서가 화면에서도 위에서 아래로 흐른다.
    //
    // 영상은 관제 링크가 아니라 RTSP 로 따로 온다. 늦은 그림은 없는 그림보다
    // 나쁘기 때문이다 — 화면이 밀리면 팔을 더 움직이게 된다.
    auto *liveCard = new Card(QStringLiteral("카메라"));
    liveState_ = new Badge(QStringLiteral("꺼짐"), QStringLiteral("neutral"));
    liveCard->addHeaderWidget(liveState_);
    outer->addWidget(liveCard);

    // 화질은 화면에서 고른다. 링크가 좁은 자리에서 설정 파일을 고치러
    // 들어가게 하면, 그냥 흐린 화면을 참고 쓰게 된다.
    //
    // 숫자가 아니라 이름을 고르게 한다. 해상도와 비트레이트의 조합은 로봇이
    // 알고, 관제는 무엇을 원하는지만 말한다 — 양쪽이 서로 다른 조합을 들고
    // 있으면 어느 쪽이 맞는지 알 수 없어진다.
    auto *qrow = new QHBoxLayout;
    qrow->setSpacing(metrics::s2);
    qrow->addWidget(new QLabel(QStringLiteral("화질")));
    quality_ = new QComboBox;
    quality_->addItem(QStringLiteral("고화질  1280×720 · 15"), QStringLiteral("high"));
    quality_->addItem(QStringLiteral("저지연  848×480 · 30"), QStringLiteral("low"));
    quality_->addItem(QStringLiteral("절약  640×360 · 15"), QStringLiteral("saver"));
    quality_->setToolTip(
        QStringLiteral("뷰파인더 화질입니다. 실제 점검 사진은 정지 상태에서\n"
                       "원본으로 찍히므로 이 설정과 무관합니다.\n\n"
                       "무선이 좁으면 화질을 낮추는 편이 조작에 낫습니다."));
    connect(quality_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i >= 0)
            emit videoQualityChanged(quality_->itemData(i).toString());
    });
    qrow->addWidget(quality_, 1);
    liveCard->body()->addLayout(qrow);

    live_ = new PreviewView(QStringLiteral("실시간"));
    live_->setMinimumHeight(220);
    live_->setPlaceholder(QStringLiteral("영상 없음"));
    liveCard->body()->addWidget(live_);

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
    metaCard->body()->addWidget(sectionLabel(QStringLiteral("로봇 정보")));
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
        hint_->setText(QStringLiteral("정지 상태에서만 촬영합니다."));
    } else {
        state_->set(QStringLiteral("촬영 불가"), QStringLiteral("warn"));
        hint_->setText(reason.isEmpty()
                           ? QStringLiteral("로봇이 멈춘 뒤에 촬영할 수 있습니다.")
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
        QStringLiteral("위치  %1, %2   방향 %3°\n마커  %4")
            .arg(x_, 0, 'f', 2)
            .arg(y_, 0, 'f', 2)
            .arg(qRadiansToDegrees(theta_), 0, 'f', 1)
            .arg(tagId_ >= 0 ? QString::number(tagId_) : QStringLiteral("미인식")));

    const CaptureMetadata m = currentMetadata();

    // 촬영 전에는 저장할 것이 없다. 메타데이터만 채워도 저장이 열리면
    // 이미지 없는 기록이 생긴다.
    if (!hasCapture_) {
        fileNamePreview_->setText(QStringLiteral("촬영 후 표시"));
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

void CapturePanel::setLiveFrame(const QImage &frame)
{
    if (live_)
        live_->setImage(frame);
}

void CapturePanel::setVideoQuality(const QString &preset)
{
    if (!quality_)
        return;
    const int i = quality_->findData(preset);
    if (i < 0 || i == quality_->currentIndex())
        return;
    // 로봇이 알려 준 값으로 맞춘다. 신호를 막아 두지 않으면 이 갱신이
    // 다시 명령으로 나가 되돌이가 된다.
    QSignalBlocker block(quality_);
    quality_->setCurrentIndex(i);
}

void CapturePanel::setLiveStatus(const QString &text)
{
    if (!live_)
        return;
    // 그림이 있는 동안에는 지우지 않는다. 마지막으로 보이던 장면을 남겨 두는
    // 편이, 갑자기 비는 것보다 무슨 일이 났는지 읽기 쉽다.
    live_->setPlaceholder(text);
    if (liveState_) {
        const bool ok = text == QStringLiteral("수신 중");
        liveState_->set(text, ok ? QStringLiteral("ok") : QStringLiteral("neutral"));
    }
}

}  // namespace hmi::ui
