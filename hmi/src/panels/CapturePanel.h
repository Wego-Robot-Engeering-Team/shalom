#pragma once

// Capture control panel. Statement of work 2.2.7 [4].
//
//   - manual capture trigger, with the 2D and 3D previews shown immediately
//   - metadata entry, with pose, time and tag filled in from telemetry
//   - save to the NAS
//
// The panel shows the file name it is about to write. The name is fixed by the
// statement of work and is an acceptance item, so the operator should be able
// to see it is right before the image is filed rather than discovering it at
// inspection.
//
// There is no live video here, by decision. The statement of work asks for a
// preview of the captured result, not a feed, and capture happens from a
// standstill anyway (2.2.4). A viewfinder was built and then removed: it cost
// an RTSP path off the robot, a GStreamer decoder in this binary and an x264
// (GPL-2+) encoder on the robot, none of which the delivery needs. The one
// place the document mentions a live view is the AI analysis PC's own stream,
// which goes from the robot to that machine and never passes through the
// control station.

#include <QImage>
#include <QWidget>

#include "capture/CaptureMetadata.h"

class QLabel;
class QLineEdit;
class QPushButton;

namespace hmi::ui {

class Badge;
class Card;
class PreviewView;

class CapturePanel : public QWidget {
    Q_OBJECT
public:
    explicit CapturePanel(QWidget *parent = nullptr);

    /// Pose, tag and distance are taken from telemetry at the moment of
    /// capture, not typed. Called on every frame.
    void setContext(double x, double y, double theta, int visibleTagId);

    /// Whether capturing is currently permitted. Capture while moving is
    /// prohibited (2.2.4), so the button follows the robot's motion.
    void setCaptureAllowed(bool allowed, const QString &reason = {});

    /// What the operator has typed, plus the values filled in from the
    /// robot. Sent with the trigger so the robot writes the sidecar.
    hmi::capture::CaptureMetadata currentMetadata() const;

    /// Integration seam: the robot returns the captured frames over
    /// evt/capture_done. Nothing calls these while the control station runs
    /// against the simulator, so do not remove them as unused.
    void showPreview2d(const QImage &image);
    void showPreview3d(const QImage &image);

signals:
    void captureRequested();

    void saveRequested(const hmi::capture::CaptureMetadata &metadata);

private:
    void refreshDerived();


    Card *card_ = nullptr;
    Badge *state_ = nullptr;

    PreviewView *preview2d_ = nullptr;
    PreviewView *preview3d_ = nullptr;

    QLineEdit *trainNumber_ = nullptr;
    QLineEdit *carNumber_ = nullptr;
    QLineEdit *pointId_ = nullptr;

    QLabel *autoFields_ = nullptr;
    QLabel *fileNamePreview_ = nullptr;
    QLabel *hint_ = nullptr;

    QPushButton *captureButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;

    double x_ = 0, y_ = 0, theta_ = 0;
    int tagId_ = -1;
    bool hasCapture_ = false;
    QDateTime capturedAt_;
};

}  // namespace hmi::ui
