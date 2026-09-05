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
        "관제실에서 화면을 떨어져 보는 경우에 조정하십시오. "
        "글자 크기만 바뀌며 패널 간격은 유지됩니다."));
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
    cameraPort_ = new QSpinBox;
    cameraPort_->setRange(1, 65535);

    lay->addWidget(fieldRow(QStringLiteral("브릿지 주소"), host_, 96));
    lay->addWidget(fieldRow(QStringLiteral("제어 포트"), port_, 96));
    lay->addWidget(fieldRow(QStringLiteral("카메라 포트"), cameraPort_, 96));

    auto *hint = new QLabel(QStringLiteral(
        "제어 포트는 raw TCP 단일 연결입니다. 카메라 포트는 별도 HTTP MJPEG 경로이며, "
        "촬영 원본은 두 경로 모두 타지 않고 로봇에서 NAS로 직접 올라갑니다."));
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
    connect(cameraPort_, &QSpinBox::valueChanged, this,
            [](int v) { Config::instance().setCameraPort(v); });

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
    angular_->setRange(0.05, robot::kWzMax);
    angular_->setSingleStep(0.05);
    angular_->setDecimals(2);
    angular_->setSuffix(QStringLiteral(" rad/s"));

    lay->addWidget(sectionLabel(QStringLiteral("수동 조작 기본 속도")));
    lay->addWidget(fieldRow(QStringLiteral("선속도"), linear_, 72));
    lay->addWidget(fieldRow(QStringLiteral("각속도"), angular_, 72));

    auto *hint = new QLabel(QStringLiteral(
        "조작 패널을 열 때의 초기 슬라이더 값입니다. 상한은 B2 사양으로 고정됩니다.\n\n"
        "지도상 미등록 물체 접근 시에는 로봇이 자체적으로 %1 m/s 로 감속하며, "
        "이 설정은 그 동작에 영향을 주지 않습니다.")
            .arg(robot::kVxCaution, 0, 'f', 2));
    hint->setObjectName(QStringLiteral("Hint"));
    hint->setWordWrap(true);
    lay->addWidget(hint);

    connect(linear_, &QDoubleSpinBox::valueChanged, this,
            [](double v) { Config::instance().setDefaultLinearSpeed(v); });
    connect(angular_, &QDoubleSpinBox::valueChanged, this,
            [](double v) { Config::instance().setDefaultAngularSpeed(v); });

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
    lay->addWidget(fieldRow(QStringLiteral("NAS 경로"), nasPath_, 84));

    auto *hint = new QLabel(QStringLiteral(
        "NAS 경로는 이력 조회·다운로드에 쓰입니다. 촬영 원본을 NAS로 올리는 주체는 "
        "로봇이며, 관제는 저장된 결과를 읽기만 합니다.\n\n"
        "원격 접속에 서면 승인이 필요한 환경이므로, 로그 내보내기가 사실상 "
        "유일한 원격 진단 수단입니다. 보관 기간을 짧게 두지 마십시오."));
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
        "아래 값은 로봇측 안전 노드가 강제합니다. 관제에서 변경할 수 없으며, "
        "변경 가능한 것처럼 보이게 두지 않습니다."));
    intro->setObjectName(QStringLiteral("Hint"));
    intro->setWordWrap(true);
    lay->addWidget(intro);
    lay->addSpacing(metrics::s2);

    lay->addWidget(readOnlyRow(
        QStringLiteral("비상정지 응답"), QStringLiteral("1 초 이내"),
        QStringLiteral("SDK2 E-Stop API 와 /cmd_vel 차단을 동시에 적용합니다. "
                       "최종 권한은 하드웨어 버튼에 있습니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("통신 두절 정지"), QStringLiteral("3 초"),
        QStringLiteral("관제가 꺼지거나 링크가 끊겨도 로봇이 스스로 정지합니다. "
                       "재연결 후 자율주행은 자동 재개되지 않습니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("수동 조작 데드맨"), QStringLiteral("300 ms"),
        QStringLiteral("조작 명령이 끊기면 브릿지가 즉시 속도를 0 으로 래치합니다.")));
    lay->addWidget(readOnlyRow(
        QStringLiteral("비상정지 해제"), QStringLiteral("관리자 인증 필요"),
        QStringLiteral("자동 해제는 금지되어 있습니다. 발동은 인증 없이 즉시 동작합니다.")));

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
            QStringLiteral("⚠ 브릿지 주소 %1 이 위 인터페이스 어느 서브넷에도 속하지 "
                           "않습니다. 라우팅이 없으면 연결되지 않습니다.")
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
    const QSignalBlocker b1(scale_), b2(host_), b3(port_), b4(cameraPort_);
    const QSignalBlocker b5(linear_), b6(angular_), b7(logDir_), b8(retention_), b9(nasPath_);

    scale_->setValue(int(qRound(cfg.uiScale() * 100)));
    scaleValue_->setText(QStringLiteral("%1%").arg(scale_->value()));
    host_->setText(cfg.bridgeHost());
    port_->setValue(cfg.bridgePort());
    cameraPort_->setValue(cfg.cameraPort());
    linear_->setValue(cfg.defaultLinearSpeed());
    angular_->setValue(cfg.defaultAngularSpeed());
    logDir_->setText(cfg.logDirectory());
    retention_->setValue(cfg.logRetentionDays());
    nasPath_->setText(cfg.nasMountPath());
}


}  // namespace gcs::ui
