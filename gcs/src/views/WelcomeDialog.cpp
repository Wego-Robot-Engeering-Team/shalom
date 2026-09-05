#include "views/WelcomeDialog.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "Config.h"
#include "auth/Session.h"
#include "net/Envelope.h"
#include "theme/Tokens.h"
#include "widgets/BrandMark.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::auth::Role;
using gcs::auth::Session;

WelcomeDialog::WelcomeDialog(QWidget *parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("Root"));
    setWindowTitle(QStringLiteral("SHALOM 관제"));
    setModal(true);
    setFixedWidth(440);

    setupMode_ = Session::instance().needsInitialSetup();

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(metrics::s6, metrics::s5, metrics::s6, metrics::s5);
    lay->setSpacing(metrics::s3);

    // ---- 머리말 ----
    auto *head = new QHBoxLayout;
    head->setSpacing(metrics::s3);
    head->addWidget(new BrandMark(nullptr, 40), 0, Qt::AlignVCenter);

    auto *titles = new QVBoxLayout;
    titles->setSpacing(0);
    auto *title = new QLabel(QStringLiteral("SHALOM 관제"));
    title->setObjectName(QStringLiteral("AppTitle"));
    auto *sub = new QLabel(QStringLiteral("Unitree B2 + FR3 · 철도차량 하부 점검"));
    sub->setObjectName(QStringLiteral("AppSubtitle"));
    titles->addWidget(title);
    titles->addWidget(sub);
    head->addLayout(titles);
    head->addStretch(1);
    lay->addLayout(head);

    lay->addSpacing(metrics::s2);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);

    if (setupMode_)
        buildSetupMode();
    else
        buildSignInMode();

    error_ = new QLabel;
    error_->setObjectName(QStringLiteral("Hint"));
    error_->setWordWrap(true);
    error_->hide();
    lay->addWidget(error_);

    submit_ = new QPushButton(setupMode_ ? QStringLiteral("설정하고 시작")
                                         : QStringLiteral("시작"));
    submit_->setProperty("variant", "primary");
    submit_->setDefault(true);
    lay->addWidget(submit_);

    connect(submit_, &QPushButton::clicked, this, [this] {
        if (setupMode_)
            submitSetup();
        else
            submitSignIn();
    });

    // ---- 꼬리말: 시스템 정보와 안전 고지 ----
    lay->addSpacing(metrics::s3);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);

    auto &cfg = Config::instance();
    auto *info = new QLabel(
        QStringLiteral("브릿지  %1:%2      프로토콜 v%3")
            .arg(cfg.bridgeHost())
            .arg(cfg.bridgePort())
            .arg(gcs::net::kProtocolVersion));
    info->setObjectName(QStringLiteral("Mono"));
    lay->addWidget(info);

    auto *notice = new QLabel(QStringLiteral(
        "비상정지의 최종 권한은 하드웨어 버튼과 로봇측 안전 노드에 있습니다. "
        "관제 화면의 정지 버튼은 보조 수단입니다."));
    notice->setObjectName(QStringLiteral("Hint"));
    notice->setWordWrap(true);
    lay->addWidget(notice);
}

void WelcomeDialog::buildSetupMode()
{
    auto *lay = static_cast<QVBoxLayout *>(layout());

    auto *intro = new QLabel(QStringLiteral(
        "최초 실행입니다. 관리자 비밀번호를 설정하십시오.\n"
        "비상정지 해제, 위치 교시, 설정 변경에 필요합니다."));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    lay->addSpacing(metrics::s2);

    name_ = new QLineEdit;
    name_->setPlaceholderText(QStringLiteral("이름 (조작 이력에 기록됩니다)"));
    lay->addWidget(fieldRow(QStringLiteral("이름"), name_, 72));

    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    password_->setPlaceholderText(QStringLiteral("8자 이상"));
    lay->addWidget(fieldRow(QStringLiteral("비밀번호"), password_, 72));

    passwordConfirm_ = new QLineEdit;
    passwordConfirm_->setEchoMode(QLineEdit::Password);
    lay->addWidget(fieldRow(QStringLiteral("확인"), passwordConfirm_, 72));

    auto *warn = new QLabel(QStringLiteral(
        "이 비밀번호는 오조작 방지를 위한 운용 통제 수단입니다. "
        "관제 PC에 파일 접근이 가능한 사람에게는 보안 경계가 되지 않습니다."));
    warn->setObjectName(QStringLiteral("Hint"));
    warn->setWordWrap(true);
    lay->addWidget(warn);
}

void WelcomeDialog::buildSignInMode()
{
    auto *lay = static_cast<QVBoxLayout *>(layout());

    name_ = new QLineEdit;
    name_->setPlaceholderText(QStringLiteral("이름 (조작 이력에 기록됩니다)"));
    lay->addWidget(fieldRow(QStringLiteral("이름"), name_, 72));

    role_ = new QComboBox;
    role_->addItem(QStringLiteral("운용자 — 주행 · 촬영 · 미션 실행"), int(Role::Operator));
    role_->addItem(QStringLiteral("관리자 — 비상정지 해제 · 위치 교시 · 설정"), int(Role::Admin));
    lay->addWidget(fieldRow(QStringLiteral("권한"), role_, 72));

    password_ = new QLineEdit;
    password_->setEchoMode(QLineEdit::Password);
    passwordLabel_ = new QLabel;
    auto *pwRow = fieldRow(QStringLiteral("비밀번호"), password_, 72);
    lay->addWidget(pwRow);
    passwordLabel_ = qobject_cast<QLabel *>(pwRow->layout()->itemAt(0)->widget());
    confirmLabel_ = nullptr;

    // 권한에 따라 비밀번호 칸을 숨긴다. 운용자에게 빈 칸을 보여주면
    // 무엇을 입력해야 하는지 헷갈린다.
    connect(role_, &QComboBox::currentIndexChanged, this, [this, pwRow] {
        pwRow->setVisible(Role(role_->currentData().toInt()) == Role::Admin);
        adjustSize();
    });
    pwRow->setVisible(false);
}

void WelcomeDialog::showError(const QString &message)
{
    error_->setText(message);
    error_->setVisible(!message.isEmpty());
    adjustSize();
}

void WelcomeDialog::submitSetup()
{
    if (password_->text() != passwordConfirm_->text()) {
        showError(QStringLiteral("비밀번호가 일치하지 않습니다."));
        return;
    }
    QString err;
    if (!Session::instance().setAdminPassword(password_->text(), &err)) {
        showError(err);
        return;
    }
    if (!Session::instance().signIn(name_->text(), Role::Admin, password_->text(), &err)) {
        showError(err);
        return;
    }
    accept();
}

void WelcomeDialog::submitSignIn()
{
    const auto role = Role(role_->currentData().toInt());
    QString err;
    if (!Session::instance().signIn(name_->text(), role, password_->text(), &err)) {
        showError(err);
        password_->clear();
        return;
    }
    accept();
}


}  // namespace gcs::ui
