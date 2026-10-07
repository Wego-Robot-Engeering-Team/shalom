// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "widgets/CatalogRow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/IconButton.h"

namespace hmi::ui {

using namespace hmi::theme;

namespace {
class CatalogNameLabel : public QLabel {
public:
    using QLabel::QLabel;
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setFont(font());
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter,
            fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};
}

CatalogRow::CatalogRow(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("CatalogRow"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(metrics::s2, metrics::s1, metrics::s2, metrics::s1);
    outer->setSpacing(metrics::s1);

    auto *top = new QHBoxLayout;
    top->setSpacing(metrics::s1);
    name_ = new CatalogNameLabel;
    name_->setObjectName(QStringLiteral("CatalogRowName"));
    name_->setTextFormat(Qt::PlainText);
    name_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    top->addWidget(name_, 1);
    status_ = new QLabel;
    status_->setObjectName(QStringLiteral("CatalogRowStatus"));
    status_->setTextFormat(Qt::PlainText);
    status_->hide();
    top->addWidget(status_);
    edit_ = new IconButton(IconButton::Glyph::Edit);
    edit_->setObjectName(QStringLiteral("CatalogRowEdit"));
    edit_->setAccessibleName(QStringLiteral("편집"));
    edit_->setToolTip(QStringLiteral("이름과 값을 편집"));
    top->addWidget(edit_);
    apply_ = new QPushButton(QStringLiteral("적용"));
    apply_->setObjectName(QStringLiteral("CatalogRowApply"));
    apply_->setProperty("size", "sm");
    top->addWidget(apply_);
    outer->addLayout(top);

    details_ = new QLabel;
    details_->setObjectName(QStringLiteral("CatalogRowDetails"));
    details_->setTextFormat(Qt::PlainText);
    details_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    details_->setWordWrap(true);
    outer->addWidget(details_);

    editor_ = new QWidget;
    editor_->setObjectName(QStringLiteral("CatalogRowEditor"));
    editorLayout_ = new QVBoxLayout(editor_);
    editorLayout_->setContentsMargins(0, metrics::s1, 0, metrics::s1);
    editorLayout_->setSpacing(metrics::s2);
    editor_->hide();
    outer->addWidget(editor_);
}

void CatalogRow::setName(const QString &name)
{
    name_->setText(name);
    name_->setToolTip(name);
}

void CatalogRow::setDetails(const QString &details)
{
    details_->setText(details);
    details_->setToolTip(details);
    details_->setVisible(!details.isEmpty());
}

void CatalogRow::setEditing(bool editing)
{
    editor_->setVisible(editing);
    apply_->setVisible(!editing);
    refreshActions();
}

bool CatalogRow::isEditing() const
{
    return !editor_->isHidden();
}

void CatalogRow::setPending(bool pending)
{
    pending_ = pending;
    editor_->setEnabled(!pending);
    refreshActions();
}

void CatalogRow::setApplyEnabled(bool enabled)
{
    applyAllowed_ = enabled;
    refreshActions();
}

void CatalogRow::setEditEnabled(bool enabled)
{
    editAllowed_ = enabled;
    refreshActions();
}

void CatalogRow::refreshActions()
{
    edit_->setEnabled(editAllowed_ && !pending_ && !isEditing());
    apply_->setEnabled(applyAllowed_ && !pending_ && !isEditing());
}

void CatalogRow::setStatus(const QString &text)
{
    status_->setText(text);
    status_->setVisible(!text.isEmpty());
}

void CatalogRow::mousePressEvent(QMouseEvent *event)
{
    if (selected_)
        selected_();
    QWidget::mousePressEvent(event);
}

}  // namespace hmi::ui
