#include "panels/StatusPanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Gauges.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;

StatusPanel::StatusPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("상태 모니터링"));
    conn_ = new Badge(QStringLiteral("연결 끊김"), QStringLiteral("danger"));
    card_->addHeaderWidget(conn_);
    outer->addWidget(card_);

    // ---- 배터리 + 모드 ----
    auto *top = new QHBoxLayout;
    top->setSpacing(metrics::s4);
    battery_ = new BatteryRing(nullptr, 86, 25.0);
    top->addWidget(battery_, 0, Qt::AlignVCenter);

    auto *right = new QVBoxLayout;
    right->setSpacing(metrics::s2);
    right->addWidget(sectionLabel(QStringLiteral("주행 모드")));
    mode_ = new Badge(QStringLiteral("—"), QStringLiteral("neutral"));
    right->addWidget(mode_, 0, Qt::AlignLeft);
    right->addWidget(sectionLabel(QStringLiteral("Apriltag")));
    tag_ = new Badge(QStringLiteral("미인식"), QStringLiteral("neutral"));
    right->addWidget(tag_, 0, Qt::AlignLeft);
    right->addStretch(1);
    top->addLayout(right, 1);
    card_->body()->addLayout(top);

    // ---- 시스템 지표 ----
    card_->body()->addSpacing(metrics::s2);
    card_->body()->addWidget(sectionLabel(QStringLiteral("시스템")));

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(metrics::s4);
    grid->setVerticalSpacing(metrics::s1);
    cpu_ = new StatBar(QStringLiteral("CPU"), QStringLiteral("%"), nullptr, 80, 92);
    mem_ = new StatBar(QStringLiteral("MEM"), QStringLiteral("%"), nullptr, 80, 92);
    cpuTemp_ = new StatBar(QStringLiteral("CPU 온도"), QStringLiteral("°C"), nullptr, 75, 88);
    gpuTemp_ = new StatBar(QStringLiteral("GPU 온도"), QStringLiteral("°C"), nullptr, 75, 88);
    rtt_ = new StatBar(QStringLiteral("링크 RTT"), QStringLiteral("ms"), nullptr, 120, 400, 500);
    grid->addWidget(cpu_, 0, 0);
    grid->addWidget(mem_, 0, 1);
    grid->addWidget(cpuTemp_, 1, 0);
    grid->addWidget(gpuTemp_, 1, 1);
    grid->addWidget(rtt_, 2, 0, 1, 2);
    card_->body()->addLayout(grid);

    // ---- 위치 ----
    card_->body()->addSpacing(metrics::s2);
    card_->body()->addWidget(sectionLabel(QStringLiteral("현재 위치 (map)")));
    pose_ = readout();
    card_->body()->addWidget(pose_);
    card_->body()->addStretch(1);
}

void StatusPanel::setConnected(bool ok)
{
    conn_->set(ok ? QStringLiteral("연결됨") : QStringLiteral("연결 끊김"),
               ok ? QStringLiteral("ok") : QStringLiteral("danger"));
}

void StatusPanel::setBattery(double socPercent, bool charging)
{
    battery_->setState(socPercent, charging);
}

void StatusPanel::setMode(const QString &mode, bool estop)
{
    // E-Stop 중에는 주행 모드가 조작자에게 의미 없는 정보다. 덮어쓴다.
    if (estop)
        mode_->set(QStringLiteral("E-STOP"), QStringLiteral("danger"));
    else if (mode == QLatin1String("auto"))
        mode_->set(QStringLiteral("자율"), QStringLiteral("info"));
    else if (mode == QLatin1String("manual"))
        mode_->set(QStringLiteral("수동"), QStringLiteral("warn"));
    else
        mode_->set(mode.isEmpty() ? QStringLiteral("—") : mode, QStringLiteral("neutral"));
}

void StatusPanel::setSystem(double cpu, double mem, double cpuTemp, double gpuTemp,
                            double rttMs)
{
    cpu_->setReading(cpu);
    mem_->setReading(mem);
    cpuTemp_->setReading(cpuTemp);
    gpuTemp_->setReading(gpuTemp);
    rtt_->setReading(rttMs);
}

void StatusPanel::setPose(double x, double y, double thetaDeg)
{
    pose_->setText(QStringLiteral("%1, %2   θ %3°")
                       .arg(x, 7, 'f', 2)
                       .arg(y, 7, 'f', 2)
                       .arg(thetaDeg, 6, 'f', 1));
}

void StatusPanel::setTagsSeen(int count)
{
    if (count > 0)
        tag_->set(QStringLiteral("%1개 인식").arg(count), QStringLiteral("ok"));
    else
        tag_->set(QStringLiteral("미인식"), QStringLiteral("neutral"));
}

}  // namespace gcs::ui
