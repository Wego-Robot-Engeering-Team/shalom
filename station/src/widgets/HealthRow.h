#pragma once

// One sensor's health, drawn as a single dense row:
//
//     * LiDAR                    9.8 / 10 Hz   38.4k pts
//
// The rate bar is drawn relative to the expected rate rather than an absolute
// scale, so a 200 Hz IMU and a 1 Hz battery reading are directly comparable at
// a glance. That comparison is the whole point of the panel.

#include <QWidget>

namespace gcs::ui {

class HealthRow : public QWidget {
    Q_OBJECT
public:
    explicit HealthRow(const QString &name, double expectedHz, QWidget *parent = nullptr);

    /// state is "ok", "degraded", "lost" or "fault".
    void setState(const QString &state, double actualHz, qint64 lastSeenMs,
                  const QString &detail = {});

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString name_;
    QString state_ = QStringLiteral("lost");
    QString detail_;
    double expectedHz_;
    double actualHz_ = 0.0;
    qint64 lastSeenMs_ = 0;
};

}  // namespace gcs::ui
