#include "Config.h"

#include <QSettings>
#include <QStandardPaths>

namespace gcs {
namespace {

// 기본값을 한곳에 모아둔다. 설정 초기화와 최초 실행이 같은 값을 쓰게 하기 위함.
constexpr auto kDefaultHost = "192.168.123.100";
constexpr int kDefaultPort = 9090;
constexpr int kDefaultCameraPort = 8080;
constexpr auto kDefaultTheme = "light";
constexpr double kDefaultScale = 1.0;
constexpr double kDefaultLinear = 0.30;
constexpr double kDefaultAngular = 0.50;
constexpr int kDefaultRetention = 90;
constexpr auto kDefaultNas = "/mnt/nas/inspection";

/// UI 배율 허용 범위. 0.8 미만은 한글 가독성이 무너지고,
/// 1.6 초과는 패널이 잘려 스크롤 없이는 조작이 안 된다.
constexpr double kMinScale = 0.8;
constexpr double kMaxScale = 1.6;

QSettings &store()
{
    static QSettings s(QSettings::IniFormat, QSettings::UserScope,
                       QStringLiteral("WEGO Robotics"), QStringLiteral("SHALOM GCS"));
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
    emit changed();
}

int Config::bridgePort() const
{
    return store().value(QStringLiteral("connection/port"), kDefaultPort).toInt();
}

void Config::setBridgePort(int port)
{
    store().setValue(QStringLiteral("connection/port"), port);
    emit changed();
}

int Config::cameraPort() const
{
    return store().value(QStringLiteral("connection/camera_port"),
                         kDefaultCameraPort).toInt();
}

void Config::setCameraPort(int port)
{
    store().setValue(QStringLiteral("connection/camera_port"), port);
    emit changed();
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
    emit changed();
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
    emit changed();
}

double Config::defaultLinearSpeed() const
{
    return store().value(QStringLiteral("operation/linear_speed"), kDefaultLinear).toDouble();
}

void Config::setDefaultLinearSpeed(double v)
{
    store().setValue(QStringLiteral("operation/linear_speed"), v);
    emit changed();
}

double Config::defaultAngularSpeed() const
{
    return store().value(QStringLiteral("operation/angular_speed"), kDefaultAngular).toDouble();
}

void Config::setDefaultAngularSpeed(double v)
{
    store().setValue(QStringLiteral("operation/angular_speed"), v);
    emit changed();
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
    emit changed();
}

int Config::logRetentionDays() const
{
    return store().value(QStringLiteral("logging/retention_days"), kDefaultRetention).toInt();
}

void Config::setLogRetentionDays(int days)
{
    store().setValue(QStringLiteral("logging/retention_days"), days);
    emit changed();
}

QString Config::nasMountPath() const
{
    return store().value(QStringLiteral("storage/nas_path"),
                         QLatin1String(kDefaultNas)).toString();
}

void Config::setNasMountPath(const QString &path)
{
    store().setValue(QStringLiteral("storage/nas_path"), path);
    emit changed();
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
    emit changed();
}

}  // namespace gcs
