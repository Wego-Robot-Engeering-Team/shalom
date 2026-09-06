#include "views/SettingsDialog.h"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QNetworkInterface>
#include <QTabBar>
#include <QTcpSocket>
#include <QTimer>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QtMath>

#include "Config.h"
#include "RobotDef.h"
#include "auth/Session.h"
#include "theme/Tokens.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;
using gcs::Config;

namespace {

/// 읽기 전용 값 한 줄. 로봇측이 강제하는 항목을 보여줄 때 쓴다.
QWidget *readOnlyRow(const QString &label, const QString &value, const QString &note)
{
    auto *host = new QWidget;
    auto *lay = new QVBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, metrics::s2);
    lay->setSpacing(2);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(label));
    top->addStretch(1);
    top->addWidget(readout(value));
    lay->addLayout(top);

    auto *hint = new QLabel(note);
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);
    return host;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("Root"));
    setWindowTitle(QStringLiteral("설정"));
    resize(520, 560);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(metrics::s4, metrics::s4, metrics::s4, metrics::s4);
    lay->setSpacing(metrics::s3);

    tabs_ = new QTabWidget;
    auto *tabs = tabs_;
    // 탭이 폭에 맞춰 늘어나면 항목 수가 바뀔 때마다 위치가 흔들린다.
    tabs->tabBar()->setExpanding(false);
    tabs->addTab(buildAppearanceTab(), QStringLiteral("표시"));
    tabs->addTab(buildConnectionTab(), QStringLiteral("연결"));
    tabs->addTab(buildOperationTab(), QStringLiteral("조작"));
    tabs->addTab(buildStorageTab(), QStringLiteral("저장"));
    tabs->addTab(buildSafetyTab(), QStringLiteral("안전"));
    lay->addWidget(tabs, 1);

    auto *buttons = new QHBoxLayout;
    auto *reset = new QPushButton(QStringLiteral("기본값으로"));
    auto *close = new QPushButton(QStringLiteral("닫기"));
    close->setProperty("variant", "primary");
    buttons->addWidget(reset);
    buttons->addStretch(1);
    buttons->addWidget(close);
    lay->addLayout(buttons);

    connect(close, &QPushButton::clicked, this, &QWidget::close);
    connect(reset, &QPushButton::clicked, this, [this] {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("기본값으로 되돌리기"),
            QStringLiteral("모든 설정을 기본값으로 되돌립니다.\n"
                           "관리자 비밀번호는 유지됩니다."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        Config::instance().resetToDefaults();
        load();
        emit appearanceChanged();
    });

    load();
}

QWidget *SettingsDialog::buildAppearanceTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    lay->addWidget(sectionLabel(QStringLiteral("글자 크기")));

    auto *row = new QHBoxLayout;
    scale_ = new QSlider(Qt::Horizontal);
    // 80 % ~ 160 %. 이보다 작으면 한글 가독성이 무너지고, 크면 패널이 잘린다.
    scale_->setRange(80, 160);
    scale_->setSingleStep(5);
    scale_->setPageStep(10);
    scaleValue_ = readout(QStringLiteral("100%"));
    row->addWidget(scale_, 1);
    row->addWidget(scaleValue_);
    lay->addLayout(row);

    auto *hint = new QLabel(QStringLiteral(
        "화면을 멀리 두고 보거나 글자가 작게 느껴지면 키우십시오."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    connect(scale_, &QSlider::valueChanged, this, [this](int v) {
        scaleValue_->setText(QStringLiteral("%1%").arg(v));
        Config::instance().setUiScale(v / 100.0);
        setUiScale(v / 100.0);
        emit appearanceChanged();
    });

    lay->addSpacing(metrics::s3);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);
    lay->addWidget(sectionLabel(QStringLiteral("테마")));

    auto *themeRow = new QHBoxLayout;
    auto *light = new QPushButton(QStringLiteral("라이트"));
    auto *dark = new QPushButton(QStringLiteral("다크"));
    for (auto *b : {light, dark})
        b->setCheckable(true);
    themeRow->addWidget(light, 1);
    themeRow->addWidget(dark, 1);
    lay->addLayout(themeRow);

    auto syncTheme = [light, dark] {
        const bool isDark = Config::instance().theme() == QLatin1String("dark");
        light->setChecked(!isDark);
        dark->setChecked(isDark);
    };
    connect(light, &QPushButton::clicked, this, [this, syncTheme] {
        Config::instance().setTheme(QStringLiteral("light"));
        setTheme(QStringLiteral("light"));
        syncTheme();
        emit appearanceChanged();
    });
    connect(dark, &QPushButton::clicked, this, [this, syncTheme] {
        Config::instance().setTheme(QStringLiteral("dark"));
        setTheme(QStringLiteral("dark"));
        syncTheme();
        emit appearanceChanged();
    });
    syncTheme();

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildConnectionTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    host_ = new QLineEdit;
    port_ = new QSpinBox;
    port_->setRange(1, 65535);
    lay->addWidget(fieldRow(QStringLiteral("로봇 주소"), host_, 96));
    lay->addWidget(fieldRow(QStringLiteral("제어 포트"), port_, 96));

    auto *hint = new QLabel(QStringLiteral(
        "관제 화면은 이 주소로 로봇과 연결합니다. "
        "촬영한 사진은 이 경로를 거치지 않고 로봇에서 저장 장치로 바로 올라갑니다."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    // ---- 연결 확인 ----
    auto *testRow = new QHBoxLayout;
    testButton_ = new QPushButton(QStringLiteral("연결 확인"));
    testResult_ = new QLabel;
    testResult_->setObjectName(QStringLiteral("Hint"));
    testResult_->setWordWrap(true);
    testRow->addWidget(testButton_);
    testRow->addWidget(testResult_, 1);
    lay->addLayout(testRow);
    connect(testButton_, &QPushButton::clicked, this, &SettingsDialog::testConnection);

    lay->addSpacing(metrics::s2);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);

    // ---- 관제 PC 네트워크 ----
    lay->addWidget(sectionLabel(QStringLiteral("관제 PC 네트워크")));
    interfaces_ = readout();
    interfaces_->setWordWrap(true);
    lay->addWidget(interfaces_);

    subnetWarning_ = new QLabel;
    subnetWarning_->setObjectName(QStringLiteral("Hint"));
    subnetWarning_->setWordWrap(true);
    lay->addWidget(subnetWarning_);

    refreshNetworkInfo();
    connect(host_, &QLineEdit::textChanged, this, [this] { refreshNetworkInfo(); });

    connect(host_, &QLineEdit::editingFinished, this,
            [this] { Config::instance().setBridgeHost(host_->text()); });
    connect(port_, &QSpinBox::valueChanged, this,
            [](int v) { Config::instance().setBridgePort(v); });

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildOperationTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    linear_ = new QDoubleSpinBox;
    linear_->setRange(0.05, robot::kVxMax);
    linear_->setSingleStep(0.05);
    linear_->setDecimals(2);
    linear_->setSuffix(QStringLiteral(" m/s"));

    angular_ = new QDoubleSpinBox;
    angular_->setRange(qRadiansToDegrees(0.05), qRadiansToDegrees(robot::kWzMax));
    angular_->setSingleStep(5.0);
    angular_->setDecimals(0);
    angular_->setSuffix(QStringLiteral(" °/s"));

    lay->addWidget(sectionLabel(QStringLiteral("수동 조작 기본 속도")));
    lay->addWidget(fieldRow(QStringLiteral("선속도"), linear_, 84));
    lay->addWidget(fieldRow(QStringLiteral("각속도"), angular_, 84));

    auto *hint = new QLabel(QStringLiteral(
        "수동 조작 화면을 열 때 처음 적용되는 속도입니다. "
        "이보다 빠르게는 설정할 수 없습니다.\n\n"
        "지도에 없는 장애물이 가까워지면 로봇이 스스로 %1 m/s 까지 늦춥니다. "
        "이 설정과는 관계없이 동작합니다.")
            .arg(robot::kVxCaution, 0, 'f', 2));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    connect(linear_, &QDoubleSpinBox::valueChanged, this,
            [](double v) { Config::instance().setDefaultLinearSpeed(v); });
    connect(angular_, &QDoubleSpinBox::valueChanged, this, [](double deg) {
        Config::instance().setDefaultAngularSpeed(qDegreesToRadians(deg));
    });

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildStorageTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s3);

    logDir_ = new QLineEdit;
    retention_ = new QSpinBox;
    retention_->setRange(7, 3650);
    retention_->setSuffix(QStringLiteral(" 일"));
    nasPath_ = new QLineEdit;

    lay->addWidget(sectionLabel(QStringLiteral("이벤트 로그")));
    lay->addWidget(fieldRow(QStringLiteral("저장 위치"), logDir_, 84));
    lay->addWidget(fieldRow(QStringLiteral("보관 기간"), retention_, 84));

    lay->addSpacing(metrics::s2);
    lay->addWidget(new HLine);
    lay->addSpacing(metrics::s2);
    lay->addWidget(sectionLabel(QStringLiteral("촬영 데이터")));
    lay->addWidget(fieldRow(QStringLiteral("저장 장치 경로"), nasPath_, 84));

    auto *hint = new QLabel(QStringLiteral(
        "이력 화면에서 사진을 찾고 내려받을 때 쓰는 경로입니다. "
        "사진을 저장 장치에 올리는 것은 로봇이며, 관제 화면은 읽기만 합니다.\n\n"
        "문제가 생겼을 때 담당자에게 보낼 수 있는 것은 내보낸 로그뿐입니다. "
        "보관 기간을 너무 짧게 두지 마십시오."));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    connect(logDir_, &QLineEdit::editingFinished, this,
            [this] { Config::instance().setLogDirectory(logDir_->text()); });
    connect(retention_, &QSpinBox::valueChanged, this,
            [](int v) { Config::instance().setLogRetentionDays(v); });
    connect(nasPath_, &QLineEdit::editingFinished, this,
            [this] { Config::instance().setNasMountPath(nasPath_->text()); });

    lay->addStretch(1);
    return page;
}

QWidget *SettingsDialog::buildSafetyTab()
{
    auto *page = new QWidget;
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s3, metrics::s4, metrics::s3, metrics::s3);
    lay->setSpacing(metrics::s2);

    auto *intro = new QLabel(QStringLiteral(
        "아래 값은 로봇이 직접 지킵니다. 관제 화면에서는 바꿀 수 없습니다."));
    intro->setObjectName(QStringLiteral("Hint"));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    lay->addSpacing(metrics::s2);

    lay->addWidget(readOnlyRow(
        QStringLiteral("비상정지 응답"), QStringLiteral("1 초 이내"),
        QStringLiteral("정지 명령과 주행 명령 차단이 동시에 걸립니다. "
                       "최종 권한은 하드웨어 정지 버튼에 있습니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("통신 두절 정지"), QStringLiteral("3 초"),
        QStringLiteral("관제 화면이 꺼지거나 연결이 끊겨도 로봇이 스스로 멈춥니다. "
                       "다시 연결되어도 자율주행은 자동으로 이어지지 않습니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("조작 중단 시 정지"), QStringLiteral("300 ms"),
        QStringLiteral("조작 버튼에서 손을 떼면 로봇이 즉시 멈춥니다. 관제 화면이 멈추거나 "
                       "연결이 끊겨도 마찬가지입니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("비상정지 해제"), QStringLiteral("관리자 인증 필요"),
        QStringLiteral("스스로 풀리지 않습니다. 반대로 정지시킬 때는 인증 없이 바로 됩니다.")));

    lay->addStretch(1);
    return page;
}

void SettingsDialog::setCurrentTab(int index)
{
    tabs_->setCurrentIndex(index);
}

void SettingsDialog::refreshNetworkInfo()
{
    QStringList lines;
    QList<QPair<QHostAddress, int>> local;   // 주소와 프리픽스 길이

    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!(iface.flags() & QNetworkInterface::IsUp)
            || (iface.flags() & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol)
                continue;
            lines << QStringLiteral("%1   %2/%3")
                         .arg(iface.humanReadableName(), entry.ip().toString())
                         .arg(entry.prefixLength());
            local.append({entry.ip(), entry.prefixLength()});
        }
    }

    interfaces_->setText(lines.isEmpty() ? QStringLiteral("사용 가능한 IPv4 인터페이스 없음")
                                         : lines.join(QLatin1Char('\n')));

    // 브릿지 주소가 어느 인터페이스와도 같은 서브넷에 없으면 알린다.
    // 에어갭 설치에서 가장 흔한 연결 실패 원인이고, 증상은 그냥 "연결 안 됨"
    // 이라 원인을 짚기 어렵다.
    const QHostAddress target(Config::instance().bridgeHost());
    if (target.isNull() || target.protocol() != QAbstractSocket::IPv4Protocol) {
        subnetWarning_->clear();
        subnetWarning_->hide();
        return;
    }

    bool sameSubnet = false;
    for (const auto &[ip, prefix] : local) {
        if (target.isInSubnet(ip, prefix)) {
            sameSubnet = true;
            break;
        }
    }

    subnetWarning_->setVisible(!sameSubnet && !local.isEmpty());
    if (!sameSubnet && !local.isEmpty()) {
        subnetWarning_->setText(
            QStringLiteral("⚠ 로봇 주소 %1 이 이 PC 의 네트워크 대역에 없습니다. "
                           "같은 네트워크가 아니면 연결되지 않습니다.")
                .arg(target.toString()));
    }
}

void SettingsDialog::testConnection()
{
    auto &cfg = Config::instance();
    testButton_->setEnabled(false);
    testResult_->setText(QStringLiteral("확인 중…"));

    // 소켓을 열었다 닫는 것으로 끝낸다. 프로토콜 핸드셰이크는 하지 않는다 —
    // 여기서 확인하려는 것은 "포트에 닿는가" 뿐이다.
    auto *socket = new QTcpSocket(this);
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(3000);

    const auto finish = [this, socket, timer](const QString &text) {
        timer->stop();
        socket->abort();
        socket->deleteLater();
        timer->deleteLater();
        testResult_->setText(text);
        testButton_->setEnabled(true);
    };

    connect(socket, &QTcpSocket::connected, this, [finish, &cfg] {
        finish(QStringLiteral("연결됨 — %1:%2 에 응답이 있습니다.")
                   .arg(cfg.bridgeHost())
                   .arg(cfg.bridgePort()));
    });
    connect(socket, &QTcpSocket::errorOccurred, this,
            [finish, socket](QAbstractSocket::SocketError) {
                finish(QStringLiteral("연결 실패 — %1").arg(socket->errorString()));
            });
    connect(timer, &QTimer::timeout, this, [finish] {
        finish(QStringLiteral("연결 실패 — 시간 초과(3초). 주소·포트와 방화벽을 확인하십시오."));
    });

    timer->start();
    socket->connectToHost(cfg.bridgeHost(), quint16(cfg.bridgePort()));
}

void SettingsDialog::load()
{
    auto &cfg = Config::instance();
    const QSignalBlocker b1(scale_), b2(host_), b3(port_);
    const QSignalBlocker b5(linear_), b6(angular_), b7(logDir_), b8(retention_), b9(nasPath_);

    scale_->setValue(int(qRound(cfg.uiScale() * 100)));
    scaleValue_->setText(QStringLiteral("%1%").arg(scale_->value()));
    host_->setText(cfg.bridgeHost());
    port_->setValue(cfg.bridgePort());
    linear_->setValue(cfg.defaultLinearSpeed());
    angular_->setValue(qRadiansToDegrees(cfg.defaultAngularSpeed()));
    logDir_->setText(cfg.logDirectory());
    retention_->setValue(cfg.logRetentionDays());
    nasPath_->setText(cfg.nasMountPath());
}


}  // namespace gcs::ui
