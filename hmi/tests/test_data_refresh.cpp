// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "data/RecordScanWorker.h"
#include "panels/DataPanel.h"

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
bool image(const QTemporaryDir &directory, const QString &stamp, const QString &point = QStringLiteral("wp"))
{
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(Qt::white);
    return image.save(directory.filePath("train_1_" + point + ',' + stamp + ".png"));
}

#ifdef Q_OS_UNIX
struct BlockedSidecar {
    explicit BlockedSidecar(const QTemporaryDir &directory)
    {
        const auto path = directory.filePath("train_1_wp,20261007120000.json").toLocal8Bit();
        if (::mkfifo(path.constData(), 0600) == 0)
            fd = ::open(path.constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    }
    ~BlockedSidecar() { if (fd >= 0) ::close(fd); }
    int fd = -1;
};
#endif
}

class TestDataRefresh : public QObject {
    Q_OBJECT
private slots:
    void workerRoundTripPreservesMetadata()
    {
        hmi::data::ScanResult result;
        hmi::data::InspectionRecord record;
        record.filePath = QStringLiteral("/자료/image.png");
        record.fileName = QStringLiteral("image.png");
        record.fileSize = 123456789;
        record.vehicleNumber = QStringLiteral("차량");
        record.carNumber = QStringLiteral("1");
        record.pointId = QStringLiteral("wp");
        record.capturedAt = QDateTime::fromString("2026-10-07T12:00:00.123+09:00", Qt::ISODateWithMs);
        record.tagId = 9;
        record.sidecar = QJsonObject{{"map_id", "map-a"}, {"distance_mm", 1200.5}};
        result.records << record;
        result.error = QStringLiteral("partial");
        result.unrecognised << QStringLiteral("other");
        const auto decoded = hmi::data::decodeScanResult(hmi::data::encodeScanResult(result));
        QVERIFY(decoded);
        QCOMPARE(decoded->error, result.error);
        QCOMPARE(decoded->unrecognised, result.unrecognised);
        QCOMPARE(decoded->records.size(), 1);
        const auto actual = decoded->records.first();
        QCOMPARE(actual.filePath, record.filePath);
        QCOMPARE(actual.fileSize, record.fileSize);
        QCOMPARE(actual.pointId, record.pointId);
        QCOMPARE(actual.capturedAt, record.capturedAt);
        QCOMPARE(actual.sidecar, record.sidecar);
        QVERIFY(!hmi::data::decodeScanResult("not-json"));
        QVERIFY(!hmi::data::decodeScanResult("{\"records\":[{}],\"error\":\"\",\"unrecognised\":[]}"));
    }

    void externalImagesAppearWithoutManualRefresh()
    {
        QTemporaryDir directory;
        QVERIFY(image(directory, "20261007120000"));
        hmi::ui::DataPanel panel;
        panel.setDirectory(directory.path());
        QTRY_VERIFY(!panel.isScanning());
        QCOMPARE(panel.recordsForPoint("wp").size(), 1);
        QVERIFY(image(directory, "20261007120001"));
        // Keep the same freshness policy; shorten just the polling interval.
        panel.findChild<QTimer *>("DataRefreshTimer")->setInterval(100);
        QTRY_COMPARE_WITH_TIMEOUT(panel.recordsForPoint("wp").size(), 2, 5000);
    }

    void repeatedRequestsAreCoalesced()
    {
        QTemporaryDir directory;
        QVERIFY(image(directory, "20261007120000"));
        hmi::ui::DataPanel panel;
        panel.setDirectory(directory.path());
        for (int i = 0; i < 20; ++i) panel.refresh();
        QTRY_VERIFY(!panel.isScanning());
        QCOMPARE(panel.recordsForPoint("wp").size(), 1);
        QVERIFY(panel.findChild<QPushButton *>("DataRefreshButton")->isEnabled());
        panel.setDirectory({});
        QVERIFY(!panel.isScanning());
        QVERIFY(!panel.findChild<QTimer *>("DataRefreshTimer")->isActive());
    }

    void refreshPreservesSelectionWhenNewImagesArrive()
    {
        QTemporaryDir directory;
        QVERIFY(image(directory, "20261007120000"));
        hmi::ui::DataPanel panel;
        panel.setDirectory(directory.path());
        QTRY_VERIFY(!panel.isScanning());
        auto *list = panel.findChild<QListWidget *>("DataRecords");
        list->setCurrentRow(0);
        const auto selectedPath = list->currentItem()->data(Qt::UserRole + 5).toString();
        QVERIFY(!selectedPath.isEmpty());
        QVERIFY(image(directory, "20261007120001"));
        panel.refresh();
        QTRY_VERIFY(!panel.isScanning());
        QCOMPARE(list->count(), 2);
        QVERIFY(list->currentItem());
        QCOMPARE(list->currentItem()->data(Qt::UserRole + 5).toString(), selectedPath);
        QCOMPARE(list->currentRow(), 1);
    }

    void staleResultsNeverPopulateAnotherDirectory()
    {
        QTemporaryDir first, second;
        QVERIFY(image(first, "20261007120000", "first"));
        QVERIFY(image(second, "20261007120000", "second"));
        hmi::ui::DataPanel panel;
        panel.setDirectory(first.path());
        panel.setDirectory(second.path());
        QTRY_VERIFY(!panel.isScanning());
        QVERIFY(panel.recordsForPoint("first").isEmpty());
        QCOMPARE(panel.recordsForPoint("second").size(), 1);
    }

    void unreadableDirectoryReportsFailure()
    {
        QTemporaryDir directory;
        hmi::ui::DataPanel panel;
        QSignalSpy notices(&panel, &hmi::ui::DataPanel::notice);
        panel.setDirectory(directory.filePath("missing"));
        QTRY_VERIFY(!panel.isScanning());
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().at(1).toString().contains(QStringLiteral("찾을 수 없습니다")));
    }

    void repeatedScanErrorDoesNotFloodNotifications()
    {
        QTemporaryDir directory;
        hmi::ui::DataPanel panel;
        QSignalSpy notices(&panel, &hmi::ui::DataPanel::notice);
        const auto missing = directory.filePath("missing");
        panel.setDirectory(missing);
        QTRY_VERIFY(!panel.isScanning());
        QCOMPARE(notices.size(), 1);
        for (int i = 0; i < 3; ++i) {
            panel.refresh();
            QTRY_VERIFY(!panel.isScanning());
        }
        QCOMPARE(notices.size(), 1);
        panel.setDirectory(directory.path());
        QTRY_VERIFY(!panel.isScanning());
        panel.setDirectory(missing);
        QTRY_VERIFY(!panel.isScanning());
        QCOMPARE(notices.size(), 2);
    }

    void blockedPreviousDirectoryDoesNotDelayNewOne()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir slow, healthy;
        QVERIFY(image(slow, "20261007120000"));
        QVERIFY(image(healthy, "20261007120001", "healthy"));
        BlockedSidecar held(slow);
        QVERIFY(held.fd >= 0);
        hmi::ui::DataPanel panel;
        panel.setDirectory(slow.path());
        QTest::qWait(200);
        QVERIFY(panel.isScanning());
        panel.setDirectory(healthy.path());
        QTRY_VERIFY_WITH_TIMEOUT(!panel.isScanning(), 2000);
        QVERIFY(panel.recordsForPoint("wp").isEmpty());
        QCOMPARE(panel.recordsForPoint("healthy").size(), 1);
#else
        QSKIP("FIFO-based stalled filesystem fixture is Unix-only");
#endif
    }

    void stalledScanTimesOutAndCanRetry()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        QVERIFY(image(directory, "20261007120000"));
        BlockedSidecar held(directory);
        QVERIFY(held.fd >= 0);
        hmi::ui::DataPanel panel;
        panel.findChild<QTimer *>("DataScanTimeout")->setInterval(200);
        QSignalSpy notices(&panel, &hmi::ui::DataPanel::notice);
        QSignalSpy completed(&panel, &hmi::ui::DataPanel::recordsChanged);
        panel.setDirectory(directory.path());
        QTRY_VERIFY_WITH_TIMEOUT(!panel.isScanning(), 1500);
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().at(1).toString().contains(QStringLiteral("시간 초과")));
        QVERIFY(panel.findChild<QPushButton *>("DataRefreshButton")->isEnabled());
        completed.clear();
        panel.refresh();
        QVERIFY(panel.isScanning());
        QTRY_VERIFY_WITH_TIMEOUT(!panel.isScanning(), 1500);
        QCOMPARE(completed.size(), 2);
        QCOMPARE(notices.size(), 1); // The retry completes without repeating the same toast.
#else
        QSKIP("FIFO-based stalled filesystem fixture is Unix-only");
#endif
    }

    void destroyingPanelDoesNotWaitForFilesystemRead()
    {
#ifdef Q_OS_UNIX
        QTemporaryDir directory;
        QVERIFY(image(directory, "20261007120000"));
        BlockedSidecar held(directory);
        QVERIFY(held.fd >= 0);
        auto *panel = new hmi::ui::DataPanel;
        panel->setDirectory(directory.path());
        QTest::qWait(150);
        QVERIFY(panel->isScanning());
        QElapsedTimer timer;
        timer.start();
        delete panel;
        QVERIFY(timer.elapsed() < 200);
        QTest::qWait(30);
#else
        QSKIP("FIFO-based stalled filesystem fixture is Unix-only");
#endif
    }
};

QTEST_MAIN(TestDataRefresh)
#include "test_data_refresh.moc"
