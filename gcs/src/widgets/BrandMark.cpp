#include "widgets/BrandMark.h"

#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>

#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {
constexpr auto kSvgPath = ":/brand/logo.svg";
constexpr auto kPngPath = ":/brand/logo.png";
}  // namespace

BrandMark::~BrandMark() = default;

bool BrandMark::hasBrandAsset() const
{
    return svg_ != nullptr || !pixmap_.isNull();
}

BrandMark::BrandMark(QWidget *parent, int height)
    : QWidget(parent), height_(height)
{
    loadAsset();
    setFixedHeight(height_);
    // 폴백 마크는 정사각, 공식 자산은 원본 비율을 유지한다.
    int w = height_;
    if (svg_) {
        const QSizeF s = svg_->defaultSize();
        if (s.height() > 0)
            w = int(height_ * s.width() / s.height());
    } else if (!pixmap_.isNull() && pixmap_.height() > 0) {
        w = pixmap_.width() * height_ / pixmap_.height();
    }
    setFixedWidth(qMax(height_, w));
}

void BrandMark::loadAsset()
{
    if (QFile::exists(QLatin1String(kSvgPath))) {
        auto r = std::make_unique<QSvgRenderer>(QString::fromLatin1(kSvgPath));
        if (r->isValid())
            svg_ = std::move(r);
        return;
    }
    if (QFile::exists(QLatin1String(kPngPath)))
        pixmap_.load(QString::fromLatin1(kPngPath));
}

void BrandMark::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (svg_) {
        svg_->render(&p, QRectF(rect()));
        return;
    }
    if (!pixmap_.isNull()) {
        p.drawPixmap(rect(), pixmap_);
        return;
    }
    paintFallback(p);
}

void BrandMark::paintFallback(QPainter &p)
{
    // 중립 마크: 원근으로 수렴하는 두 레일 + 침목.
    // 일반적인 철도 도상 기호이며 특정 기업의 상표를 모사하지 않는다.
    const Colors &C = colors();
    const qreal s = height_;
    const QRectF box(0.5, 0.5, s - 1, s - 1);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(C.accent));
    p.drawRoundedRect(box, metrics::rMd, metrics::rMd);

    QPen rail(QColor(C.textOnAccent), qMax(1.2, s * 0.055));
    rail.setCapStyle(Qt::RoundCap);
    p.setPen(rail);

    // 아래(넓음) → 위(좁음) 로 수렴하는 레일
    const qreal bx = s * 0.22, tx = s * 0.40;
    p.drawLine(QPointF(bx, s * 0.82), QPointF(tx, s * 0.20));
    p.drawLine(QPointF(s - bx, s * 0.82), QPointF(s - tx, s * 0.20));

    // 침목 3 개 — 위로 갈수록 짧고 촘촘하게
    struct Tie { qreal y, halfW; };
    const Tie ties[] = {{0.74, 0.30}, {0.52, 0.22}, {0.32, 0.14}};
    QPen tie(QColor(C.textOnAccent), qMax(1.0, s * 0.045));
    tie.setCapStyle(Qt::RoundCap);
    p.setPen(tie);
    for (const auto &t : ties)
        p.drawLine(QPointF(s * (0.5 - t.halfW), s * t.y),
                   QPointF(s * (0.5 + t.halfW), s * t.y));
}

}  // namespace gcs::ui
