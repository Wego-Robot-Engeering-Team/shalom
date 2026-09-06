#include "widgets/PreviewView.h"

#include <QFont>
#include <QPainter>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

PreviewView::PreviewView(const QString &caption, QWidget *parent)
    : QWidget(parent), caption_(caption),
      placeholder_(QStringLiteral("촬영 전"))
{
    setMinimumHeight(120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void PreviewView::setImage(const QImage &image)
{
    image_ = image;
    update();
}

void PreviewView::clear()
{
    image_ = QImage();
    update();
}

void PreviewView::setPlaceholder(const QString &text)
{
    placeholder_ = text;
    update();
}

void PreviewView::paintEvent(QPaintEvent *)
{
    const Colors &C = colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const QRectF box = rect().adjusted(0, 0, -1, -1);
    p.setPen(QPen(QColor(C.border), 1));
    p.setBrush(QColor(C.isDark() ? C.bg : C.surfaceHi));
    p.drawRoundedRect(box, metrics::rMd, metrics::rMd);

    if (image_.isNull()) {
        QFont f;
        f.setPointSize(10);
        p.setFont(f);
        p.setPen(QColor(C.textMute));
        p.drawText(box, Qt::AlignCenter, placeholder_);
    } else {
        // 비율을 유지해 가운데 맞춘다. 늘이면 대상물의 형상이 왜곡되어
        // 조작자가 잘못 판단한다.
        const QSize scaled = image_.size().scaled(box.size().toSize(),
                                                  Qt::KeepAspectRatio);
        const QRect target(int(box.x() + (box.width() - scaled.width()) / 2),
                           int(box.y() + (box.height() - scaled.height()) / 2),
                           scaled.width(), scaled.height());
        p.drawImage(target, image_);
    }

    // 캡션은 좌상단에 얹는다. 아래에 두면 이미지 영역이 줄어든다.
    QFont cf;
    cf.setPointSize(9);
    p.setFont(cf);
    const QRectF label(box.x() + 6, box.y() + 4, box.width() - 12, 16);
    p.setPen(Qt::NoPen);
    QColor plate(C.overlay);
    plate.setAlpha(image_.isNull() ? 0 : 190);
    p.setBrush(plate);
    if (!image_.isNull())
        p.drawRoundedRect(label.adjusted(-3, -1, 0, 1), 3, 3);
    p.setPen(QColor(C.textDim));
    p.drawText(label, Qt::AlignLeft | Qt::AlignVCenter, caption_);
}

}  // namespace gcs::ui
