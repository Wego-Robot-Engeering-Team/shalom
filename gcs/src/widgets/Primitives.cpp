#include "widgets/Primitives.h"

#include <QHBoxLayout>
#include <QSizePolicy>
#include <QVBoxLayout>

#include "theme/Style.h"
#include "theme/Tokens.h"

namespace gcs::ui {

using namespace gcs::theme;

Card::Card(const QString &title, QWidget *parent, bool padded)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("Card"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    if (!title.isEmpty()) {
        header_ = new QWidget;
        header_->setObjectName(QStringLiteral("CardHeader"));
        header_->setFixedHeight(34);
        headerLayout_ = new QHBoxLayout(header_);
        headerLayout_->setContentsMargins(metrics::s3, 0, metrics::s2, 0);
        headerLayout_->setSpacing(metrics::s2);

        auto *lbl = new QLabel(title);
        lbl->setObjectName(QStringLiteral("CardTitle"));
        headerLayout_->addWidget(lbl);
        headerLayout_->addStretch(1);
        outer->addWidget(header_);
    }

    auto *host = new QWidget;
    const int pad = padded ? metrics::s3 : 0;
    body_ = new QVBoxLayout(host);
    body_->setContentsMargins(pad, pad, pad, pad);
    body_->setSpacing(metrics::s2);
    outer->addWidget(host, 1);
}

void Card::addHeaderWidget(QWidget *w)
{
    Q_ASSERT_X(headerLayout_, "Card::addHeaderWidget", "제목 없는 Card 에는 헤더가 없다");
    if (headerLayout_)
        headerLayout_->addWidget(w);
}

Badge::Badge(const QString &text, const QString &tone, QWidget *parent)
    : QLabel(text, parent)
{
    setObjectName(QStringLiteral("Badge"));
    setAlignment(Qt::AlignCenter);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    setProperty("tone", tone);
}

void Badge::setTone(const QString &tone)
{
    if (property("tone").toString() == tone)
        return;
    setProperty("tone", tone);
    repolish(this);
}

void Badge::set(const QString &text, const QString &tone)
{
    setText(text);
    setTone(tone);
}

HLine::HLine(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("HLine"));
    setFixedHeight(1);
}

QLabel *sectionLabel(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName(QStringLiteral("SectionLabel"));
    return l;
}

QLabel *readout(const QString &text, bool large)
{
    auto *l = new QLabel(text);
    l->setObjectName(large ? QStringLiteral("ReadoutLg") : QStringLiteral("Readout"));
    return l;
}

QWidget *fieldRow(const QString &label, QWidget *widget, int labelWidth)
{
    auto *host = new QWidget;
    auto *lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(metrics::s2);
    auto *lbl = sectionLabel(label);
    lbl->setFixedWidth(labelWidth);
    lay->addWidget(lbl);
    lay->addWidget(widget, 1);
    return host;
}

}  // namespace gcs::ui
