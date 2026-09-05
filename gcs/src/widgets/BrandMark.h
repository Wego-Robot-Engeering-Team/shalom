#pragma once

// Brand mark shown in the title bar.
//
// This is the slot for the customer's logo. The asset is read from Qt
// resources; when it is absent the widget falls back to a neutral mark.
//
// The mark is never drawn as code that imitates a trademark, because:
//   - a hand-approximated mark differs from the official asset in proportion,
//     letter spacing and color, and
//   - shipping a customer's trademark inside delivered software requires the
//     official asset together with written permission to use it (normally
//     obtained at project kickoff along with the brand guidelines).
//
// To install the official asset: drop resources/brand/logo.svg (or logo.png)
// in place and rebuild. See resources/brand/README.md.

#include <QPixmap>
#include <QWidget>

#include <memory>

class QSvgRenderer;

namespace gcs::ui {

class BrandMark : public QWidget {
    Q_OBJECT
public:
    explicit BrandMark(QWidget *parent = nullptr, int height = 26);
    ~BrandMark() override;   ///< out-of-line: QSvgRenderer is incomplete here

    /// True when an official logo asset was loaded. When false the widget
    /// paints the neutral fallback mark instead.
    bool hasBrandAsset() const;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void loadAsset();
    void paintFallback(QPainter &p);

    int height_;
    std::unique_ptr<QSvgRenderer> svg_;
    QPixmap pixmap_;
};

}  // namespace gcs::ui
