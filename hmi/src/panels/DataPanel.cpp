// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/DataPanel.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>
#include <QtMath>
#include <QtConcurrentRun>

#include "theme/Tokens.h"
#include "data/RecordScanWorker.h"
#include "widgets/PreviewView.h"
#include "widgets/Primitives.h"

namespace hmi::ui {

using namespace hmi::theme;
using hmi::data::InspectionRecord;
using hmi::data::ScanResult;

namespace {

constexpr int kRoleIndex = Qt::UserRole;
constexpr int kRoleComplete = Qt::UserRole + 1;
constexpr int kRolePoint = Qt::UserRole + 2;
constexpr int kRoleWhen = Qt::UserRole + 3;
constexpr int kRoleVehicle = Qt::UserRole + 4;
constexpr int kRolePath = Qt::UserRole + 5;
constexpr int kHistoryFreshMs = 2000;
constexpr int kHistoryRefreshMs = 10000;
constexpr int kHistoryScanTimeoutMs = 30000;
constexpr qsizetype kMaxScanResultBytes = 64 * 1024 * 1024;

class RecordDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        return {0, 40};
    }

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        const Colors &C = colors();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect r = opt.rect;

        if (opt.state & QStyle::StateFlag::State_Selected) {
            QColor sel(C.accent);
            sel.setAlpha(30);
            p->setPen(Qt::NoPen);
            p->setBrush(sel);
            p->drawRect(r);
        } else if (opt.state & QStyle::StateFlag::State_MouseOver) {
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(C.surfaceHi));
            p->drawRect(r);
        }

        // 메타데이터가 불완전한 기록은 표시한다. 감추면 증거의 구멍이
        // 검수 때까지 보이지 않는다.
        const bool complete = idx.data(kRoleComplete).toBool();
        p->setPen(Qt::NoPen);
        p->setBrush(complete ? QColor(C.wpDone) : QColor(C.warning));
        p->drawEllipse(r.left() + 10, r.center().y() - 3, 6, 6);

        QFont ft;
        ft.setPointSize(11);
        p->setFont(ft);
        p->setPen(QColor(C.text));
        p->drawText(r.adjusted(26, 3, -8, 0), Qt::AlignLeft | Qt::AlignTop,
                    idx.data(kRolePoint).toString());

        QFont fm = monoFont(9);
        p->setFont(fm);
        p->setPen(QColor(C.textMute));
        p->drawText(r.adjusted(26, 0, -8, -3), Qt::AlignLeft | Qt::AlignBottom,
                    QStringLiteral("%1   %2")
                        .arg(idx.data(kRoleVehicle).toString(),
                             idx.data(kRoleWhen).toString()));
        p->restore();
    }
};

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 kB").arg(bytes / 1024.0, 0, 'f', 0);
    return QStringLiteral("%1 B").arg(bytes);
}

}  // namespace

DataPanel::DataPanel(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(metrics::s3);

    card_ = new Card(QStringLiteral("점검 이력"));
    count_ = new Badge(QStringLiteral("0"), QStringLiteral("neutral"));
    card_->addHeaderWidget(count_);
    outer->addWidget(card_);

    card_->body()->addWidget(sectionLabel(QStringLiteral("저장 위치")));

    auto *head = new QHBoxLayout;
    head->setSpacing(metrics::s2);
    pathLabel_ = readout(QStringLiteral("설정되지 않았습니다"));
    pathLabel_->setTextFormat(Qt::PlainText);
    pathLabel_->setWordWrap(true);
    auto *refresh = new QPushButton(QStringLiteral("새로고침"));
    refresh_ = refresh;
    refresh_->setObjectName(QStringLiteral("DataRefreshButton"));
    refresh->setProperty("size", "sm");
    head->addWidget(pathLabel_, 1);
    head->addWidget(refresh);
    card_->body()->addLayout(head);
    connect(refresh, &QPushButton::clicked, this, &DataPanel::rescan);

    filter_ = new QLineEdit;
    filter_->setPlaceholderText(QStringLiteral("차량번호 · 포인트 · 날짜로 찾기"));
    filter_->setClearButtonEnabled(true);
    card_->body()->addWidget(filter_);
    connect(filter_, &QLineEdit::textChanged, this, &DataPanel::applyFilter);

    list_ = new QListWidget;
    list_->setObjectName(QStringLiteral("DataRecords"));
    list_->setItemDelegate(new RecordDelegate(list_));
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setMouseTracking(true);
    list_->setMinimumHeight(200);
    card_->body()->addWidget(list_, 1);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur) {
        if (!cur) {
            ++previewGeneration_;
            preview_->clear();
            preview_->setPlaceholder(QStringLiteral("항목 선택"));
            details_->hide();
            download_->setEnabled(false);
            return;
        }
        const int i = cur->data(kRoleIndex).toInt();
        if (i >= 0 && i < records_.size())
            showRecord(records_.at(i));
    });

    warning_ = new QLabel;
    warning_->setObjectName(QStringLiteral("Hint"));
    warning_->setTextFormat(Qt::PlainText);
    warning_->setWordWrap(true);
    warning_->hide();
    card_->body()->addWidget(warning_);

    // ---- 선택 항목 ----
    auto *detailCard = new Card(QStringLiteral("선택 항목"));
    outer->addWidget(detailCard);

    preview_ = new PreviewView(QStringLiteral("미리보기"));
    preview_->setPlaceholder(QStringLiteral("항목 선택"));
    preview_->setMinimumHeight(160);
    detailCard->body()->addWidget(preview_);

    details_ = readout();
    details_->setTextFormat(Qt::PlainText);
    details_->setWordWrap(true);
    details_->hide();
    detailCard->body()->addWidget(details_);

    download_ = new QPushButton(QStringLiteral("내려받기"));
    download_->setProperty("variant", "primary");
    download_->setEnabled(false);
    detailCard->body()->addWidget(download_);
    connect(download_, &QPushButton::clicked, this, &DataPanel::downloadSelected);

    outer->addStretch(1);

    scanTimeout_ = new QTimer(this);
    scanTimeout_->setObjectName(QStringLiteral("DataScanTimeout"));
    scanTimeout_->setSingleShot(true);
    scanTimeout_->setInterval(kHistoryScanTimeoutMs);
    connect(scanTimeout_, &QTimer::timeout, this, [this] {
        finishScan(scanProcess_, directoryGeneration_, QStringLiteral("촬영 이력 조회 시간 초과"));
    });
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setObjectName(QStringLiteral("DataRefreshTimer"));
    refreshTimer_->setInterval(kHistoryRefreshMs);
    connect(refreshTimer_, &QTimer::timeout, this, &DataPanel::refreshIfStale);
}

DataPanel::~DataPanel()
{
    cancelScan();
}

void DataPanel::setDirectory(const QString &path)
{
    if (directory_ != path) {
        cancelScan();
        ++directoryGeneration_;
        cacheAge_.invalidate();
        lastScanError_.clear();
        records_.clear();
        warning_->hide();
        applyFilter();
    }
    directory_ = path;
    pathLabel_->setText(path.isEmpty() ? QStringLiteral("설정되지 않았습니다") : path);
    if (path.isEmpty()) refreshTimer_->stop();
    else refreshTimer_->start();
    rescan();
    emit recordsChanged();
}

void DataPanel::refresh()
{
    rescan();
}

void DataPanel::refreshIfStale()
{
    if (!scanBusy_ && (!cacheAge_.isValid() || cacheAge_.elapsed() >= kHistoryFreshMs))
        rescan();
}

QList<InspectionRecord> DataPanel::recordsForPoint(const QString &id) const
{
    QList<InspectionRecord> result;
    for (const auto &record : records_)
        if (record.pointId == id)
            result << record;
    return result;
}

void DataPanel::rescan()
{
    if (directory_.isEmpty())
        return;
    if (scanBusy_) {
        rescanRequested_ = true;
        return;
    }
    scanBusy_ = true;
    refresh_->setEnabled(false);
    const quint64 generation = directoryGeneration_;
    auto *process = new QProcess(this);
    scanProcess_ = process;
    scanOutput_.clear();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, generation] {
        if (process != scanProcess_ || generation != directoryGeneration_) return;
        scanOutput_ += process->readAllStandardOutput();
        if (scanOutput_.size() > kMaxScanResultBytes)
            finishScan(process, generation, QStringLiteral("촬영 이력 조회 결과가 너무 큽니다"));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishScan(process, generation, QStringLiteral("촬영 이력 조회 작업을 시작하지 못했습니다"));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, generation](int code, QProcess::ExitStatus status) {
        finishScan(process, generation, code == 0 && status == QProcess::NormalExit
            ? QString{} : QStringLiteral("촬영 이력 조회 작업이 종료되었습니다"));
    });
    // The deployed HMI executable also provides the headless scan worker;
    // there is no separate helper binary or platform-specific script to ship.
#ifdef Q_OS_WIN
    const QString binary = QStringLiteral("inspection_hmi.exe");
#else
    const QString binary = QStringLiteral("inspection_hmi");
#endif
    const QString program = QCoreApplication::applicationName() == QLatin1String("Inspection HMI")
        ? QCoreApplication::applicationFilePath()
        : QDir(QCoreApplication::applicationDirPath()).filePath(binary);
    process->start(program,
                   {QStringLiteral("--scan-inspection-records"), directory_});
    scanTimeout_->start();
    emit recordsChanged();
}

void DataPanel::cancelScan()
{
    scanTimeout_->stop();
    if (auto *process = scanProcess_) {
        scanProcess_ = nullptr;
        process->disconnect(this);
        // QProcess destruction waits for its child. Reap cancelled workers
        // asynchronously instead, including on panel/application shutdown.
        process->setParent(nullptr);
        connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                process, &QObject::deleteLater);
        connect(process, &QProcess::errorOccurred, process, [process] {
            if (process->state() == QProcess::NotRunning) process->deleteLater();
        });
        process->kill();
        if (process->state() == QProcess::NotRunning) process->deleteLater();
    }
    scanBusy_ = false;
    rescanRequested_ = false;
    scanOutput_.clear();
    refresh_->setEnabled(true);
}

void DataPanel::finishScan(QProcess *process, quint64 generation, const QString &failure)
{
    if (!process || process != scanProcess_ || generation != directoryGeneration_)
        return;
    scanOutput_ += process->readAllStandardOutput();
    const auto result = failure.isEmpty() && scanOutput_.size() <= kMaxScanResultBytes
        ? hmi::data::decodeScanResult(scanOutput_) : std::nullopt;
    const bool again = rescanRequested_;
    cancelScan();
    cacheAge_.restart();
    const QString scanError = result ? result->error : failure.isEmpty()
        ? QStringLiteral("촬영 이력 조회 결과가 올바르지 않습니다") : failure;
    if (!scanError.isEmpty() && scanError != lastScanError_)
        emit notice(QStringLiteral("warn"), scanError);
    lastScanError_ = scanError;
    if (result) {
        if (result->error.isEmpty() || !result->records.isEmpty()) {
            records_ = result->records;
            applyFilter();
        }
        QStringList warnings;
        if (!result->error.isEmpty()) {
            warnings << result->error;
        }
        if (!result->unrecognised.isEmpty())
            warnings << QStringLiteral("점검 이미지 외 파일 %1개: %2")
                .arg(result->unrecognised.size()).arg(result->unrecognised.mid(0, 3).join(QStringLiteral(", ")));
        warning_->setText(warnings.join(QLatin1Char('\n')));
        warning_->setVisible(!warnings.isEmpty());
    } else {
        warning_->setText(scanError);
        warning_->show();
    }
    emit recordsChanged();
    if (again)
        rescan();
}

void DataPanel::applyFilter()
{
    const QString needle = filter_->text().trimmed();
    const QString selectedPath = list_->currentItem()
        ? list_->currentItem()->data(kRolePath).toString() : QString{};
    const int scrollPosition = list_->verticalScrollBar()->value();
    const QSignalBlocker blocker(list_);
    list_->clear();

    int shown = 0;
    int incomplete = 0;
    QListWidgetItem *selected = nullptr;
    for (int i = 0; i < records_.size(); ++i) {
        const auto &r = records_.at(i);
        const QString when = r.capturedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (!needle.isEmpty() && !r.vehicleNumber.contains(needle, Qt::CaseInsensitive)
            && !r.pointId.contains(needle, Qt::CaseInsensitive)
            && !when.contains(needle))
            continue;

        auto *item = new QListWidgetItem;
        item->setData(kRoleIndex, i);
        item->setData(kRoleComplete, r.isComplete());
        item->setData(kRolePoint, r.pointId);
        item->setData(kRoleWhen, when);
        item->setData(kRoleVehicle, r.vehicleNumber);
        item->setData(kRolePath, r.filePath);
        list_->addItem(item);
        if (!selectedPath.isEmpty() && selectedPath == r.filePath)
            selected = item;
        ++shown;
        if (!r.isComplete())
            ++incomplete;
    }

    count_->set(QString::number(shown),
                incomplete > 0 ? QStringLiteral("warn") : QStringLiteral("neutral"));
    if (incomplete > 0) {
        count_->setToolTip(
            QStringLiteral("메타데이터가 불완전한 항목 %1개").arg(incomplete));
    }
    if (selected) {
        list_->setCurrentItem(selected);
        showRecord(records_.at(selected->data(kRoleIndex).toInt()), false);
    } else {
        ++previewGeneration_;
        preview_->clear();
        preview_->setPlaceholder(QStringLiteral("항목 선택"));
        details_->hide();
        download_->setEnabled(false);
    }
    list_->verticalScrollBar()->setValue(scrollPosition);
}

void DataPanel::showRecord(const InspectionRecord &record, bool refreshImage)
{
    if (refreshImage) {
        const quint64 generation = ++previewGeneration_;
        const QString path = record.filePath;
        preview_->clear();
        preview_->setPlaceholder(QStringLiteral("불러오는 중"));
        auto *watcher = new QFutureWatcher<QImage>(this);
        connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation] {
            const auto image = watcher->result();
            watcher->deleteLater();
            if (generation != previewGeneration_)
                return;
            if (!image.isNull())
                preview_->setImage(image);
            else
                preview_->setPlaceholder(QStringLiteral("미리보기 지원 안 됨"));
        });
        watcher->setFuture(QtConcurrent::run([path] { return QImage(path); }));
    }

    QStringList lines;
    lines << QStringLiteral("차량  %1  ·  %2량").arg(record.vehicleNumber, record.carNumber);
    lines << QStringLiteral("포인트  %1").arg(record.pointId);
    lines << QStringLiteral("촬영  %1")
                 .arg(record.capturedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    lines << QStringLiteral("마커  %1")
                 .arg(record.tagId >= 0 ? QString::number(record.tagId)
                                        : QStringLiteral("미인식"));
    lines << QStringLiteral("크기  %1").arg(humanSize(record.fileSize));

    if (record.hasSidecar()) {
        QJsonObject pose = record.sidecar->value(QStringLiteral("robot")).toObject();
        if (pose.isEmpty())
            pose = record.sidecar->value(QStringLiteral("robot_pose")).toObject();
        if (!pose.isEmpty()) {
            const double thetaDegrees = pose.contains(QStringLiteral("theta_deg"))
                ? pose.value(QStringLiteral("theta_deg")).toDouble()
                : qRadiansToDegrees(pose.value(QStringLiteral("theta")).toDouble());
            lines << QStringLiteral("로봇 위치  %1, %2   방향 %3°")
                         .arg(pose.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                         .arg(pose.value(QStringLiteral("y")).toDouble(), 0, 'f', 2)
                         .arg(thetaDegrees, 0, 'f', 1);
        }
        const double distance =
            record.sidecar->value(QStringLiteral("distance_mm")).toDouble();
        if (distance > 0)
            lines << QStringLiteral("촬영 거리  %1 mm").arg(distance, 0, 'f', 0);
    }

    const QStringList missing = record.missingFields();
    if (!missing.isEmpty())
        lines << QStringLiteral("⚠ 누락  %1").arg(missing.join(QStringLiteral(", ")));

    details_->setText(lines.join(QLatin1Char('\n')));
    details_->show();
    download_->setEnabled(!downloadPending_);
}

void DataPanel::downloadSelected()
{
    auto *item = list_->currentItem();
    if (!item)
        return;
    const int i = item->data(kRoleIndex).toInt();
    if (i < 0 || i >= records_.size())
        return;
    const auto record = records_.at(i);
    const quint64 generation = directoryGeneration_;

    const QString suggested =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
        + QLatin1Char('/') + record.fileName;
    const QString target = QFileDialog::getSaveFileName(
        this, QStringLiteral("내려받기"), suggested);
    if (target.isEmpty() || generation != directoryGeneration_ || downloadPending_)
        return;
    downloadPending_ = true;
    download_->setEnabled(false);
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, record] {
        const auto error = watcher->result();
        watcher->deleteLater();
        downloadPending_ = false;
        download_->setEnabled(list_->currentItem() != nullptr);
        if (!error.isEmpty()) {
            emit notice(QStringLiteral("warn"), error);
            QMessageBox::warning(this, QStringLiteral("내려받기 실패"), error);
        } else {
            emit notice(QStringLiteral("ok"), QStringLiteral("내려받기 완료: %1").arg(record.fileName));
        }
    });
    watcher->setFuture(QtConcurrent::run([record, target] {
        if (!hmi::data::copyFileAtomically(record.filePath, target))
            return QStringLiteral("파일을 복사하지 못했습니다.");
        const QFileInfo sourceInfo(record.filePath);
        const QString sidecar = sourceInfo.absolutePath() + '/' + sourceInfo.completeBaseName() + ".json";
        if (QFile::exists(sidecar)) {
            const QFileInfo targetInfo(target);
            const QString sidecarTarget = targetInfo.absolutePath() + '/' + targetInfo.completeBaseName() + ".json";
            if (!hmi::data::copyFileAtomically(sidecar, sidecarTarget))
                return QStringLiteral("이미지는 저장했지만 메타데이터 파일을 복사하지 못했습니다.");
        }
        return QString{};
    }));
}

}  // namespace hmi::ui
