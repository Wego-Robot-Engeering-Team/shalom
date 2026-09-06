#pragma once

// What the symbols on the map mean.
//
// The map draws points, markers, the robot and its path in four different
// shapes, and nothing on screen said which was which. An operator who was not
// there when the screen was built had to guess, and guessing wrong about a
// marker versus an inspection point changes what they do next.
//
// It sits in the bottom-right corner, small and quiet: it answers a question
// asked once, so it must not compete with the map for attention. Hovering a
// row explains the symbol; clicking one opens the screen that manages it,
// because "what is this" is usually followed by "let me change it".

#include <QWidget>

namespace hmi::ui {

class MapLegend : public QWidget {
    Q_OBJECT
public:
    /// What a row stands for. Rows without a management screen are not
    /// clickable - offering a click that goes nowhere is worse than none.
    enum class Item {
        Waypoint,   ///< inspection point, managed on the locations view
        Marker,     ///< AprilTag, placed physically on site
        Robot,      ///< where the robot is now
        Path,       ///< planned route and the trail behind it
    };

    explicit MapLegend(QWidget *parent = nullptr);

signals:
    /// The operator clicked a row that has a screen behind it.
    void manageRequested(Item item);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

private:
    /// Row under this point, or -1.
    int rowAt(const QPoint &p) const;

    int hovered_ = -1;
};

}  // namespace hmi::ui
