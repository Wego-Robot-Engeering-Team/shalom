#pragma once

// Fixed-aspect image pane with a caption.
//
// Used for the 2D and 3D capture previews and for the optional live view.
// Draws a placeholder when empty rather than leaving a blank rectangle: a
// white panel is indistinguishable from a broken one, and the operator needs
// to be able to tell "nothing captured yet" from "the camera failed".

#include <QImage>
#include <QWidget>

namespace gcs::ui {

class PreviewView : public QWidget {
    Q_OBJECT
public:
    explicit PreviewView(const QString &caption, QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void clear();

    /// Replaces the placeholder text, for reporting why there is no image.
    void setPlaceholder(const QString &text);


protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString caption_;
    QString placeholder_;
    QImage image_;
};

}  // namespace gcs::ui
