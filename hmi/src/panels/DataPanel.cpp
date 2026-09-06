#include "panels/DataPanel.h"

#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include "theme/Tokens.h"
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
    pathLabel_->setWordWrap(true);
    auto *refresh = new QPushButton(QStringLiteral("새로고침"));
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
    list_->setItemDelegate(new RecordDelegate(list_));
    list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    list_->setMouseTracking(true);
    list_->setMinimumHeight(200);
    card_->body()->addWidget(list_, 1);
    connect(list_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *cur) {
        if (!cur)
            return;
        const int i = cur->data(kRoleIndex).toInt();
        if (i >= 0 && i < records_.size())
            showRecord(records_.at(i));
    });

    warning_ = new QLabel;
    warning_->setObjectName(QStringLiteral("Hint"));
    warning_->setWordWrap(true);
    warning_->hide();
    card_->body()->addWidget(warning_);

    // ---- 선택 항목 ----
    auto *detailCard = new Card(QStringLiteral("선택 항목"));
    outer->addWidget(detailCard);

    preview_ = new PreviewView(QStringLiteral("미리보기"));
    preview_->setPlaceholder(QStringLiteral("항목을 선택하십시오"));
    preview_->setMinimumHeight(160);
    detailCard->body()->addWidget(preview_);

    details_ = readout();
    details_->setWordWrap(true);
    details_->hide();
    detailCard->body()->addWidget(details_);

    download_ = new QPushButton(QStringLiteral("내려받기"));
    download_->setProperty("variant", "primary");
    download_->setEnabled(false);
    detailCard->body()->addWidget(download_);
    connect(download_, &QPushButton::clicked, this, &DataPanel::downloadSelected);

    outer->addStretch(1);
}

void DataPanel::setDirectory(const QString &path)
{
    directory_ = path;
    pathLabel_->setText(path.isEmpty() ? QStringLiteral("설정되지 않았습니다") : path);
    rescan();
}

void DataPanel::rescan()
{
    if (directory_.isEmpty())
        return;

    const ScanResult result = hmi::data::scanDirectory(directory_);
    records_ = result.records;

    if (!result.error.isEmpty())
        emit notice(QStringLiteral("warn"), result.error);

    // 알 수 없는 파일이 있으면 알린다. 공유 폴더에 예상 못 한 것이 있다는
    // 사실 자체가 정보다.
    if (!result.unrecognised.isEmpty()) {
        warning_->setText(
            QStringLiteral("이 폴더에 점검 이미지가 아닌 파일이 %1개 있습니다: %2")
                .arg(result.unrecognised.size())
                .arg(result.unrecognised.mid(0, 3).join(QStringLiteral(", "))));
        warning_->show();
    } else {
        warning_->hide();
    }

    applyFilter();
}

void DataPanel::applyFilter()
{
    const QString needle = filter_->text().trimmed();
    list_->clear();

    int shown = 0;
    int incomplete = 0;
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
        list_->addItem(item);
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
}

void DataPanel::showRecord(const InspectionRecord &record)
{
    QImage image;
    if (image.load(record.filePath)) {
        preview_->setImage(image);
    } else {
        // 3D 포인트클라우드(.ply/.pcd)는 이미지가 아니다. 빈 화면 대신
        // 이유를 적어 둔다.
        preview_->clear();
        preview_->setPlaceholder(QStringLiteral("이 형식은 미리보기를 지원하지 않습니다"));
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
        const QJsonObject pose =
            record.sidecar->value(QStringLiteral("robot_pose")).toObject();
        if (!pose.isEmpty()) {
            lines << QStringLiteral("로봇 위치  %1, %2   방향 %3°")
                         .arg(pose.value(QStringLiteral("x")).toDouble(), 0, 'f', 2)
                         .arg(pose.value(QStringLiteral("y")).toDouble(), 0, 'f', 2)
                         .arg(pose.value(QStringLiteral("theta_deg")).toDouble(), 0, 'f', 1);
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
    download_->setEnabled(true);
}

void DataPanel::downloadSelected()
{
    auto *item = list_->currentItem();
    if (!item)
        return;
    const int i = item->data(kRoleIndex).toInt();
    if (i < 0 || i >= records_.size())
        return;
    const auto &record = records_.at(i);

    const QString suggested =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
        + QLatin1Char('/') + record.fileName;
    const QString target = QFileDialog::getSaveFileName(
        this, QStringLiteral("내려받기"), suggested);
    if (target.isEmpty())
        return;

    // 사이드카도 함께 가져간다. 메타데이터 없는 이미지는 증거로서 값이 없다.
    QFile::remove(target);
    if (!QFile::copy(record.filePath, target)) {
        QMessageBox::warning(this, QStringLiteral("내려받기 실패"),
                             QStringLiteral("파일을 복사하지 못했습니다."));
        return;
    }

    const QFileInfo sourceInfo(record.filePath);
    const QString sidecar = sourceInfo.absolutePath() + QLatin1Char('/')
                            + sourceInfo.completeBaseName() + QStringLiteral(".json");
    if (QFile::exists(sidecar)) {
        const QFileInfo targetInfo(target);
        const QString sidecarTarget = targetInfo.absolutePath() + QLatin1Char('/')
                                      + targetInfo.completeBaseName()
                                      + QStringLiteral(".json");
        QFile::remove(sidecarTarget);
        QFile::copy(sidecar, sidecarTarget);
    }

    emit notice(QStringLiteral("ok"),
                QStringLiteral("내려받기 완료: %1").arg(record.fileName));
}

}  // namespace hmi::ui
