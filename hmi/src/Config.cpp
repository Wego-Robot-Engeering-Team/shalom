#include "Config.h"

#include <QSettings>
#include <QStandardPaths>

namespace hmi {
namespace {

// 기본값을 한곳에 모아둔다. 설정 초기화와 최초 실행이 같은 값을 쓰게 하기 위함.
// 로컬이 기본이다. 시뮬레이터든 실기든 관제가 붙는 곳은 같은 브릿지이고,
// 다른 것은 주소뿐이다 — 그리고 가장 흔한 경우가 이 PC 에서 시뮬레이터를
// 돌리는 것이다.
//
// 실기 IP(192.168.123.100, Unitree 기본 서브넷)를 기본으로 두었더니, 화면이
// 기본으로 브릿지에 붙게 바뀐 뒤로는 아무것도 안 뜨는 채로 열렸다. 로봇이
// 없는 자리에서 그 주소로 붙을 방법이 없기 때문이다. 실기 주소는 설정 창의
// 연결 탭에서 지정한다.
constexpr auto kDefaultHost = "127.0.0.1";
constexpr int kDefaultPort = 9090;

// 뷰파인더 주소. 로봇이 8554 에서 /arm-rgb 로 낸다. 브릿지 주소에서
// 유추하지 않는 이유는 카메라가 여러 대가 되면 마운트 이름이 갈리기
// 때문이다 — 그때는 이 값만 고치면 된다.
constexpr auto kDefaultVideoUrl = "rtsp://127.0.0.1:8554/arm-rgb";
constexpr auto kDefaultTheme = "light";
constexpr double kDefaultScale = 1.0;
constexpr double kDefaultLinear = 0.30;
constexpr double kDefaultAngular = 0.50;
constexpr int kDefaultRetention = 90;

/// 복귀 임계와 출발 최소값의 기본값.
///
/// 복귀 25 %: 검수고 끝에서 충전 스테이션까지 74 m 를 돌아오고도 남는
/// 여유다. 출발 60 %: 편성 하나를 다 돌기에 부족한 잔량으로 나갔다가
/// 차량 아래에서 서 버리면, 꺼내려고 열차를 움직여야 한다.
constexpr double kDefaultReturnPct = 25.0;
constexpr double kDefaultDepartPct = 60.0;
/// 촬영 저장 장치의 기본 경로. 현장에서 설정 화면으로 바꾸는 값이지만,
/// 기본값이 그 OS 에서 쓸 수 없는 형태면 처음 켰을 때 "찾을 수 없습니다"
/// 부터 보게 된다. 윈도우에서는 UNC 경로가 자연스럽다.
#ifdef Q_OS_WIN
constexpr auto kDefaultNas = "\\\\nas\\inspection";
#else
constexpr auto kDefaultNas = "/mnt/nas/inspection";
#endif

/// UI 배율 허용 범위. 0.8 미만은 한글 가독성이 무너지고,
/// 1.6 초과는 패널이 잘려 스크롤 없이는 조작이 안 된다.
constexpr double kMinScale = 0.8;
constexpr double kMaxScale = 1.6;

QSettings &store()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("WEGO Robotics"), QStringLiteral("Inspection HMI"));
    return s;
}

}  // namespace

Config &Config::instance()
{
    static Config c;
    return c;
}

Config::Config() = default;

QString Config::bridgeHost() const
{
    return store().value(QStringLiteral("connection/host"),
                         QLatin1String(kDefaultHost)).toString();
}

void Config::setBridgeHost(const QString &host)
{
    store().setValue(QStringLiteral("connection/host"), host);
}

int Config::bridgePort() const
{
    return store().value(QStringLiteral("connection/port"), kDefaultPort).toInt();
}
QString Config::videoUrl() const
{
    return store().value(QStringLiteral("video/url"),
                           QLatin1String(kDefaultVideoUrl)).toString();
}

void Config::setVideoUrl(const QString &url)
{
    store().setValue(QStringLiteral("video/url"), url);
}

QString Config::videoQuality() const
{
    return store().value(QStringLiteral("video/quality"),
                         QStringLiteral("high")).toString();
}

void Config::setVideoQuality(const QString &preset)
{
    store().setValue(QStringLiteral("video/quality"), preset);
}


void Config::setBridgePort(int port)
{
    store().setValue(QStringLiteral("connection/port"), port);
}

double Config::batteryReturnPercent() const
{
    return store().value(QStringLiteral("power/return_pct"), kDefaultReturnPct).toDouble();
}

void Config::setBatteryReturnPercent(double pct)
{
    store().setValue(QStringLiteral("power/return_pct"), qBound(5.0, pct, 90.0));
}

double Config::batteryDeparturePercent() const
{
    return store().value(QStringLiteral("power/depart_pct"), kDefaultDepartPct).toDouble();
}

void Config::setBatteryDeparturePercent(double pct)
{
    store().setValue(QStringLiteral("power/depart_pct"), qBound(10.0, pct, 100.0));
}

QString Config::theme() const
{
    return store().value(QStringLiteral("appearance/theme"),
                         QLatin1String(kDefaultTheme)).toString();
}

void Config::setTheme(const QString &name)
{
    store().setValue(QStringLiteral("appearance/theme"), name);
    emit appearanceChanged();
}

double Config::uiScale() const
{
    const double v = store().value(QStringLiteral("appearance/ui_scale"),
                                   kDefaultScale).toDouble();
    return qBound(kMinScale, v, kMaxScale);
}

void Config::setUiScale(double scale)
{
    store().setValue(QStringLiteral("appearance/ui_scale"),
                     qBound(kMinScale, scale, kMaxScale));
    emit appearanceChanged();
}

double Config::defaultLinearSpeed() const
{
    return store().value(QStringLiteral("operation/linear_speed"), kDefaultLinear).toDouble();
}

void Config::setDefaultLinearSpeed(double v)
{
    store().setValue(QStringLiteral("operation/linear_speed"), v);
}

double Config::defaultAngularSpeed() const
{
    return store().value(QStringLiteral("operation/angular_speed"), kDefaultAngular).toDouble();
}

void Config::setDefaultAngularSpeed(double v)
{
    store().setValue(QStringLiteral("operation/angular_speed"), v);
}

QString Config::logDirectory() const
{
    const QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/logs");
    return store().value(QStringLiteral("logging/directory"), fallback).toString();
}

void Config::setLogDirectory(const QString &dir)
{
    store().setValue(QStringLiteral("logging/directory"), dir);
}

int Config::logRetentionDays() const
{
    return store().value(QStringLiteral("logging/retention_days"), kDefaultRetention).toInt();
}

void Config::setLogRetentionDays(int days)
{
    store().setValue(QStringLiteral("logging/retention_days"), days);
}

QString Config::nasMountPath() const
{
    return store().value(QStringLiteral("storage/nas_path"),
                         QLatin1String(kDefaultNas)).toString();
}

void Config::setNasMountPath(const QString &path)
{
    store().setValue(QStringLiteral("storage/nas_path"), path);
}

void Config::resetToDefaults()
{
    // 자격증명은 건드리지 않는다. 설정을 되돌렸다고 관리자 비밀번호가
    // 사라지면 잠금이 풀려버린다.
    auto &s = store();
    s.remove(QStringLiteral("connection"));
    s.remove(QStringLiteral("appearance"));
    s.remove(QStringLiteral("operation"));
    s.remove(QStringLiteral("logging"));
    s.remove(QStringLiteral("storage"));
    emit appearanceChanged();
}

}  // namespace hmi
