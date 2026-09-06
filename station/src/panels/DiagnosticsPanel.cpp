#include "panels/DiagnosticsPanel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "theme/Tokens.h"
#include "widgets/Gauges.h"
#include "widgets/HealthRow.h"
#include "widgets/Primitives.h"

namespace gcs::ui {

using namespace gcs::theme;

namespace {

/// 값 한 줄: 라벨 좌측, 등폭 수치 우측.
QLabel *metricRow(QVBoxLayout *lay, const QString &label)
{
    auto *row = new QHBoxLayout;
    auto *name = new QLabel(label);
    name->setObjectName(QStringLiteral("SectionLabel"));
    auto *value = readout(QStringLiteral("—"));
    row->addWidget(name);
    row->addStretch(1);
    row->addWidget(value);
    lay->addLayout(row);
    return value;
}

QString formatRate(double bytesPerS)
{
    if (bytesPerS >= 1024 * 1024)
        return QStringLiteral("%1 MB/s").arg(bytesPerS / (1024 * 1024), 0, 'f', 2);
    if (bytesPerS >= 1024)
        return QStringLiteral("%1 kB/s").arg(bytesPerS / 1024, 0, 'f', 1);
    return QStringLiteral("%1 B/s").arg(bytesPerS, 0, 'f', 0);
}

}  // namespace

QString DiagnosticsPanel::worstState(const QList<SensorHealth> &sensors)
{
    bool degraded = false;
    for (const auto &s : sensors) {
        if (s.state == QLatin1String("fault") || s.state == QLatin1String("lost"))
            return s.state;
        if (s.state == QLatin1String("degraded"))
            degraded = true;
    }
    return degraded ? QStringLiteral("degraded") : QStringLiteral("ok");
}

DiagnosticsPanel::DiagnosticsPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(metrics::s3);

    // ---- 센서 ----
    card_ = new Card(QStringLiteral("센서 상태"));
    summary_ = new Badge(QStringLiteral("—"), QStringLiteral("neutral"));
    card_->addHeaderWidget(summary_);

    sensorHost_ = new QWidget;
    auto *sensorLay = new QVBoxLayout(sensorHost_);
    sensorLay->setContentsMargins(0, 0, 0, 0);
    sensorLay->setSpacing(metrics::s1);
    card_->body()->addWidget(sensorHost_);
    outer->addWidget(card_);

    // ---- 링크 ----
    auto *linkCard = new Card(QStringLiteral("로봇 연결"));
    rtt_ = metricRow(linkCard->body(), QStringLiteral("응답 시간"));
    rssi_ = metricRow(linkCard->body(), QStringLiteral("무선 신호"));
    throughput_ = metricRow(linkCard->body(), QStringLiteral("수신 / 송신"));
    linkCard->body()->addWidget(new HLine);
    gaps_ = metricRow(linkCard->body(), QStringLiteral("데이터 유실"));
    decodeErrors_ = metricRow(linkCard->body(), QStringLiteral("통신 오류"));
    reconnects_ = metricRow(linkCard->body(), QStringLiteral("재연결"));

    outer->addWidget(linkCard);

    // ---- 로봇 제어기 ----
    // 주행 화면에 있던 지표다. 조작자가 주행 중에 CPU 백분율을 보고
    // 할 수 있는 일이 없어서, 상태를 따지는 이 화면으로 옮겼다.
    auto *sysCard = new Card(QStringLiteral("로봇 제어기"));

    // 모델명을 적어 둔다. 정비 담당자가 사양을 찾아보거나 부품을 확인할
    // 때 필요하고, 조작자에게는 그냥 지나가는 글자다.
    auto *sysModel = new QLabel(QStringLiteral("Jetson AGX Orin"));
    sysModel->setObjectName(QStringLiteral("Hint"));
    sysCard->addHeaderWidget(sysModel);

    // 사용률과 온도를 한 격자에 섞어 놓으니 어느 막대가 무엇인지 읽히지
    // 않았다. 단위가 다르면(% 와 °C) 눈금도 다른 뜻이라 나란히 두면 안 된다.
    // 소제목으로 갈라 두 줄로 놓는다.
    sysCard->body()->addWidget(sectionLabel(QStringLiteral("사용률")));
    auto *loadGrid = new QGridLayout;
    loadGrid->setContentsMargins(0, 0, 0, 0);
    loadGrid->setHorizontalSpacing(metrics::s4);
    loadGrid->setVerticalSpacing(metrics::s1);
    cpu_ = new StatBar(QStringLiteral("CPU"), QStringLiteral("%"), nullptr, 80, 92);
    gpu_ = new StatBar(QStringLiteral("GPU"), QStringLiteral("%"), nullptr, 85, 95);
    mem_ = new StatBar(QStringLiteral("메모리"), QStringLiteral("%"), nullptr, 80, 92);
    loadGrid->addWidget(cpu_, 0, 0);
    loadGrid->addWidget(gpu_, 0, 1);
    loadGrid->addWidget(mem_, 1, 0);
    sysCard->body()->addLayout(loadGrid);

    sysCard->body()->addSpacing(metrics::s2);
    sysCard->body()->addWidget(sectionLabel(QStringLiteral("온도")));
    auto *tempGrid = new QGridLayout;
    tempGrid->setContentsMargins(0, 0, 0, 0);
    tempGrid->setHorizontalSpacing(metrics::s4);
    tempGrid->setVerticalSpacing(metrics::s1);
    cpuTemp_ = new StatBar(QStringLiteral("CPU"), QStringLiteral("°C"), nullptr, 75, 88);
    gpuTemp_ = new StatBar(QStringLiteral("GPU"), QStringLiteral("°C"), nullptr, 75, 88);
    tempGrid->addWidget(cpuTemp_, 0, 0);
    tempGrid->addWidget(gpuTemp_, 0, 1);
    sysCard->body()->addLayout(tempGrid);

    outer->addWidget(sysCard);

    // ---- 저장 ----
    auto *storageCard = new Card(QStringLiteral("촬영 데이터"));
    nas_ = metricRow(storageCard->body(), QStringLiteral("저장 장치"));
    spool_ = metricRow(storageCard->body(), QStringLiteral("업로드 대기"));

    outer->addWidget(storageCard);

    outer->addStretch(1);
}

void DiagnosticsPanel::setSensors(const QList<SensorHealth> &sensors)
{
    auto *lay = qobject_cast<QVBoxLayout *>(sensorHost_->layout());

    for (const auto &s : sensors) {
        auto *row = rows_.value(s.id, nullptr);
        if (!row) {
            row = new HealthRow(s.name, s.expectedHz, sensorHost_);
            lay->addWidget(row);
            rows_.insert(s.id, row);
        }
        row->setState(s.state, s.actualHz, s.lastSeenMs, s.detail);
    }

    const QString worst = worstState(sensors);
    if (worst == QLatin1String("ok"))
        summary_->set(QStringLiteral("정상"), QStringLiteral("ok"));
    else if (worst == QLatin1String("degraded"))
        summary_->set(QStringLiteral("주의"), QStringLiteral("warn"));
    else
        summary_->set(QStringLiteral("이상"), QStringLiteral("danger"));
}

void DiagnosticsPanel::setLink(const LinkHealth &link)
{
    if (!link.connected) {
        for (auto *l : {rtt_, rssi_, throughput_})
            l->setText(QStringLiteral("—"));
    } else {
        rtt_->setText(QStringLiteral("%1 ms").arg(link.rttMs, 0, 'f', 0));
        rssi_->setText(QStringLiteral("%1 dBm").arg(link.rssiDbm, 0, 'f', 0));
        throughput_->setText(QStringLiteral("%1  /  %2")
                                 .arg(formatRate(link.rxBytesPerS),
                                      formatRate(link.txBytesPerS)));
    }
    gaps_->setText(QString::number(link.seqGaps));
    decodeErrors_->setText(QString::number(link.decodeErrors));
    reconnects_->setText(QString::number(link.reconnects));
}

void DiagnosticsPanel::setSystem(double cpu, double gpu, double mem, double cpuTemp,
                                double gpuTemp)
{
    cpu_->setReading(cpu);
    gpu_->setReading(gpu);
    mem_->setReading(mem);
    cpuTemp_->setReading(cpuTemp);
    gpuTemp_->setReading(gpuTemp);
}

void DiagnosticsPanel::setStorage(bool nasOnline, int pendingUploads, double spoolFreeMb)
{
    nas_->setText(nasOnline ? QStringLiteral("연결됨") : QStringLiteral("끊김"));
    spool_->setText(pendingUploads == 0
                        ? QStringLiteral("없음  ·  여유 %1 GB").arg(spoolFreeMb / 1024, 0, 'f', 1)
                        : QStringLiteral("%1건  ·  여유 %2 GB")
                              .arg(pendingUploads)
                              .arg(spoolFreeMb / 1024, 0, 'f', 1));
}

}  // namespace gcs::ui
