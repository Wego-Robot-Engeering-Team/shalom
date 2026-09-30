// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "views/WelcomeDialog.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "auth/Session.h"
#include "theme/Tokens.h"
#include "widgets/BrandMark.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;
using hmi::auth::Session;

WelcomeDialog::WelcomeDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("Root"));
    setWindowTitle(QStringLiteral("철도차량 하부점검 관제 시스템"));
    setModal(true);
    setFixedWidth(470);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(metrics::s6, metrics::s5, metrics::s6, metrics::s5);
    lay->setSpacing(metrics::s3);

    // ---- 머리말 ----
    auto *head = new QHBoxLayout;
    head->setSpacing(metrics::s3);
    head->addWidget(new BrandMark(nullptr, 40), 0, Qt::AlignVCenter);

    auto *title = new QLabel(QStringLiteral("하부점검 관제"));
    title->setObjectName(QStringLiteral("AppTitle"));
    head->addWidget(title, 0, Qt::AlignVCenter);
    head->addStretch(1);
    lay->addLayout(head);

    lay->addSpacing(metrics::s2);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);

    // ---- 입력 ----
    auto *form = new QFormLayout;
    form->setSpacing(metrics::s2);

    id_ = new QLineEdit;
    id_->setObjectName(QStringLiteral("LoginId"));
    form->addRow(QStringLiteral("아이디"), id_);

    password_ = new QLineEdit;
    password_->setObjectName(QStringLiteral("LoginPassword"));
    password_->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("비밀번호"), password_);
    lay->addLayout(form);

    // 두 칸 모두 Enter 로 넘어간다. 시작 버튼까지 마우스를 옮기게 하면
    // 교대마다 반복되는 동작이 한 단계 늘어난다.
    connect(id_, &QLineEdit::returnPressed, this, &WelcomeDialog::submit);
    connect(password_, &QLineEdit::returnPressed, this, &WelcomeDialog::submit);

    error_ = new QLabel;
    error_->setObjectName(QStringLiteral("Hint"));
    error_->setWordWrap(true);
    error_->hide();
    lay->addWidget(error_);

    submit_ = new QPushButton(QStringLiteral("로그인"));
    submit_->setProperty("variant", "primary");
    submit_->setDefault(true);
    lay->addWidget(submit_);
    connect(submit_, &QPushButton::clicked, this, &WelcomeDialog::submit);

    id_->setFocus();
}

void WelcomeDialog::submit()
{
    QString err;
    if (!Session::instance().signIn(id_->text().trimmed(), password_->text(), &err)) {
        error_->setText(err);
        error_->show();
        password_->clear();
        password_->setFocus();
        return;
    }
    accept();
}

}  // namespace hmi::ui
