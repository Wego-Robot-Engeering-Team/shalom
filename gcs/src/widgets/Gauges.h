#pragma once

// Custom-painted instrument widgets.
//
// Qt's stock widgets (QProgressBar and friends) do not read as instruments no
// matter how much stylesheet is applied to them, whereas antialiased arcs and
// bars are a few dozen lines of QPainter each and largely determine how the
// screen reads. This is the one area where Qt is materially easier than a web
// stack.
//
// Value changes are eased over a short interval. The easing is kept brief
// (280 ms) on purpose: on a control screen a long animation makes the operator
// distrust which number is current.
//
// All colors are resolved inside paint handlers via theme::colors() rather
// than cached, so that switching themes takes effect without a restart.

#include <QWidget>

// NOTE: this must be declared at global scope. Writing `class QPropertyAnimation
// *anim_;` inside the namespace would declare a *new* type
// gcs::ui::QPropertyAnimation instead of referring to Qt's, and the member then
// fails to resolve against the real class in the implementation file.
class QPropertyAnimation;

namespace gcs::ui {

/// Base for a widget holding one scalar that eases toward its target.
class AnimatedValue : public QWidget {
    Q_OBJECT
    Q_PROPERTY(double value READ value WRITE setValue)
public:
    explicit AnimatedValue(QWidget *parent = nullptr, int durationMs = 280);

    double value() const { return value_; }
    void setValue(double v);

protected:
    /// Starts an eased transition. Ignores no-op updates so that a stream of
    /// identical samples does not restart the animation every frame.
    void animateTo(double target);

private:
    double value_ = 0.0;
    QPropertyAnimation *anim_ = nullptr;
};

/// Circular battery gauge with the percentage in the middle.
///
/// The low-battery threshold is drawn as a notch, because the inspection
/// scenario returns the robot to the dock automatically below it and the
/// operator needs to see that boundary approaching.
class BatteryRing : public AnimatedValue {
    Q_OBJECT
public:
    explicit BatteryRing(QWidget *parent = nullptr, int size = 86,
                         double lowThreshold = 25.0);
    void setState(double socPercent, bool charging = false);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int size_;
    double low_;
    bool charging_ = false;
};

/// Half-circle arc gauge, used for the FR3 manipulability index.
///
/// The color scale is inverted relative to a normal gauge: a low value is the
/// dangerous one. Detecting and avoiding singular configurations is the robot
/// side's responsibility (protocol section 4); this widget only makes the
/// condition visible so the operator can see the arm straining.
class ArcGauge : public AnimatedValue {
    Q_OBJECT
public:
    explicit ArcGauge(QWidget *parent = nullptr, const QString &caption = {},
                      double warnBelow = 0.35, double dangerBelow = 0.15);

    /// normalized is clamped to [0,1]; rawText is what gets printed under the
    /// arc (the unnormalised index, so the value stays traceable).
    void setState(double normalized, const QString &rawText = {});

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString caption_;
    QString rawText_ = QStringLiteral("—");
    double warn_;
    double danger_;
};

/// One-line "label - value" meter for CPU load, temperatures and link latency.
class StatBar : public QWidget {
    Q_OBJECT
public:
    StatBar(const QString &label, const QString &unit = {}, QWidget *parent = nullptr,
            double warnAbove = -1, double dangerAbove = -1, double vmax = 100.0);

    /// Pass a negative value to mark the reading as unavailable; the bar then
    /// shows an em dash instead of a misleading zero.
    void setReading(double v);
    void clearReading();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString label_;
    QString unit_;
    double warn_;
    double danger_;
    double vmax_;
    double value_ = 0.0;
    bool valid_ = false;
};

/// Position of a single joint within its travel limits.
///
/// Display only; the slider drives the command. Both the commanded and the
/// measured value are drawn because the operator has to be able to see them
/// diverge, which is what a planning failure or a controller reflex looks like.
class JointBar : public QWidget {
    Q_OBJECT
public:
    JointBar(const QString &name, double lo, double hi, QWidget *parent = nullptr);

    void setActual(double rad);
    void setCommand(double rad);
    void clearCommand();

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double fraction(double v) const;
    bool nearLimit() const;

    QString name_;
    double lo_;
    double hi_;
    double actual_ = 0.0;
    double command_ = 0.0;
    bool hasCommand_ = false;
};

}  // namespace gcs::ui
