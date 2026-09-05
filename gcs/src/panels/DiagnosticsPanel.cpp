#include "panels/DiagnosticsPanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "theme/Tokens.h"
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

    auto *sensorHint = new QLabel(QStringLiteral(
        "막대는 기대 주기 대비 실측 주기입니다. 200 Hz IMU 와 1 Hz 배터리를 "
        "같은 기준으로 볼 수 있도록 절대값이 아닌 비율로 그립니다."));
    sensorHint->setObjectName(QStringLiteral("Hint"));
    sensorHint->setWordWrap(true);
    card_->body()->addWidget(sensorHint);
    outer->addWidget(card_);

    // ---- 링크 ----
    auto *linkCard = new Card(QStringLiteral("링크"));
    rtt_ = metricRow(linkCard->body(), QStringLiteral("왕복 지연"));
    rssi_ = metricRow(linkCard->body(), QStringLiteral("무선 신호"));
    throughput_ = metricRow(linkCard->body(), QStringLiteral("수신 / 송신"));
    linkCard->body()->addWidget(new HLine);
    gaps_ = metricRow(linkCard->body(), QStringLiteral("시퀀스 결손"));
    decodeErrors_ = metricRow(linkCard->body(), QStringLiteral("프레임 디코드 오류"));
    reconnects_ = metricRow(linkCard->body(), QStringLiteral("재연결"));

    auto *linkHint = new QLabel(QStringLiteral(
        "프레이밍을 자체 구현했으므로 디코드 오류는 0 이어야 정상입니다. "
        "값이 늘어나면 운용을 멈추고 로그를 내보내십시오."));
    linkHint->setObjectName(QStringLiteral("Hint"));
    linkHint->setWordWrap(true);
    linkCard->body()->addWidget(linkHint);
    outer->addWidget(linkCard);

    // ---- 저장 ----
    auto *storageCard = new Card(QStringLiteral("촬영 데이터"));
    nas_ = metricRow(storageCard->body(), QStringLiteral("NAS 연결"));
    spool_ = metricRow(storageCard->body(), QStringLiteral("업로드 대기"));

    auto *storageHint = new QLabel(QStringLiteral(
        "촬영 원본은 로봇에서 NAS 로 직접 올라갑니다. 대기 건수가 0 이 되어야 "
        "해당 점검이 실제로 끝난 것입니다."));
    storageHint->setObjectName(QStringLiteral("Hint"));
    storageHint->setWordWrap(true);
    storageCard->body()->addWidget(storageHint);
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
