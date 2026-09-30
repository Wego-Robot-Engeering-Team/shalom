// Copyright (c) 2026 WeGo Robotics. All rights reserved.
// SPDX-License-Identifier: LicenseRef-Wego-Proprietary

#include "panels/ArmPanel.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QScrollArea>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QVariantMap>
#include <QtMath>

#include <algorithm>
#include <cmath>

#include "RobotDef.h"
#include "robot/Kinematics.h"
#include "robot/PoseCheck.h"
#include "theme/Style.h"
#include "theme/Tokens.h"
#include "widgets/ValueSlider.h"
#include "widgets/Robot3DView.h"
#include "widgets/Primitives.h"
#include "widgets/CatalogRow.h"
#include "widgets/IconButton.h"

namespace hmi::ui {

using namespace hmi::theme;
using namespace hmi::robot;

namespace {
/// 각도를 기준값과 같은 바퀴로 옮긴다.
///
/// -π 과 π 은 같은 자세다. 정기구학은 그 경계에서 관절이 눈금 하나만
/// 달라져도 부호를 뒤집어 내놓는데, 그대로 두면 슬라이더에서는 손잡이와
/// 기준 고리가 양 끝으로 벌어진다 — 아무것도 만지지 않았는데 보내지 않은
/// 편집이 있는 것처럼 보인다.
double wrapNear(double v, double ref)
{
    while (v - ref > M_PI)
        v -= 2 * M_PI;
    while (ref - v > M_PI)
        v += 2 * M_PI;
    return v;
}

}  // namespace

ArmPanel::ArmPanel(QWidget *parent) : QWidget(parent)
{
    actual_ = QList<double>(robot::kArmHome.begin(), robot::kArmHome.end());

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    card_ = new Card(QStringLiteral("로봇팔 제어 · FR3"));
    state_ = new Badge(QStringLiteral("대기"), QStringLiteral("neutral"));
    card_->addHeaderWidget(state_);
    outer->addWidget(card_);

    // 조작성 지수 게이지는 뺐다. 숫자든 게이지든 조작자가 그것을 보고
    // 할 일이 정해지지 않았다. 여유가 줄었을 때 무엇을 하라는 한 줄만
    // 남기고, 그마저도 여유가 있을 때는 숨긴다.
    advice_ = new QLabel;
    advice_->setObjectName(QStringLiteral("Hint"));
    advice_->setWordWrap(true);
    advice_->hide();
    card_->body()->addWidget(advice_);

    // 3D 뷰는 고정하고 아래 조작부만 스크롤한다. 슬라이더를 움직일 때
    // 모델이 화면 밖에 있으면 미리보기 기능이 없는 것처럼 보인다.
    build3DSection();
    card_->body()->addWidget(new HLine);
    sectionTabs_ = new QTabWidget;
    sectionTabs_->setObjectName(QStringLiteral("ArmSections"));
    auto *controls = new QWidget;
    controlsLayout_ = new QVBoxLayout(controls);
    controlsLayout_->setContentsMargins(0, 0, 0, 0);
    controlsLayout_->setSpacing(metrics::s2);
    buildCommandTabs();
    auto *controlsScroll = new QScrollArea;
    controlsScroll->setWidget(controls);
    controlsScroll->setWidgetResizable(true);
    controlsScroll->setFrameShape(QFrame::NoFrame);
    controlsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    controlsScroll->setMinimumHeight(210);
    // QScrollArea의 기본 sizeHint가 내용보다 커서 탭 아래에 빈 공간이 생긴다.
    // 조작 탭의 실제 높이를 상한으로 쓰고, 창이 낮으면 스크롤한다.
    controlsScroll->setMaximumHeight(commandTabs_->sizeHint().height() + metrics::s1);
    sectionTabs_->addTab(controlsScroll, QStringLiteral("운용"));
    sectionTabs_->addTab(buildPoseManagementTab(), QStringLiteral("자세 관리"));
    connect(savedPresets_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *item) {
                if (item)
                    previewSavedPose(item->data(Qt::UserRole).toString());
            });
    // 두 탭의 높이를 같게 유지한다. 목록이 길어지면 내부에서 스크롤하고,
    // 탭 전환으로 3D 미리보기 높이가 줄어들지 않게 한다.
    card_->body()->addWidget(sectionTabs_);

    commandStatus_ = new QLabel;
    commandStatus_->setObjectName(QStringLiteral("ArmCommandStatus"));
    commandStatus_->setWordWrap(true);
    commandStatus_->hide();
    card_->body()->addWidget(commandStatus_);

    // 미리보기 저장과 실측 자세 저장은 별개다. 시뮬레이터에는 팔 실측값이
    // 없어도 관절을 화면에서 구성해 저장할 수 있어야 한다.
    auto *bottom = new QHBoxLayout;
    bottom->setSpacing(metrics::s2);
    savePreviewPose_ = new QPushButton(QStringLiteral("미리보기 저장"));
    savePreviewPose_->setObjectName(QStringLiteral("ArmSavePreviewPose"));
    savePreviewPose_->setProperty("size", "sm");
    savePreviewPose_->setProperty("variant", "primary");
    savePreviewPose_->setToolTip(QStringLiteral("3D 화면에 표시한 관절 자세를 저장합니다"));
    bottom->addWidget(savePreviewPose_, 1);
    savePose_ = new QPushButton(QStringLiteral("현재 자세 저장"));
    savePose_->setObjectName(QStringLiteral("ArmSaveCurrentPose"));
    savePose_->setProperty("size", "sm");
    savePose_->setToolTip(QStringLiteral("로봇이 보고한 실제 관절값을 저장합니다"));
    bottom->addWidget(savePose_, 1);
    auto *stop = new QPushButton(QStringLiteral("로봇팔 정지"));
    stop->setProperty("variant", "danger");
    stop->setProperty("size", "sm");
    bottom->addWidget(stop, 1);
    connect(stop, &QPushButton::clicked, this, &ArmPanel::stopRequested);
    card_->body()->addLayout(bottom);
    connect(savePreviewPose_, &QPushButton::clicked, this,
            [this] { savePose(false); });
    connect(savePose_, &QPushButton::clicked, this,
            [this] { savePose(true); });
    // 남는 세로 공간은 조작 탭이나 3D 뷰 안이 아닌 카드 아래에 둔다.
    card_->body()->addStretch(1);
    refreshCommandControls();
}

void ArmPanel::build3DSection()
{
    auto *head = new QHBoxLayout;
    head->addWidget(sectionLabel(QStringLiteral("로봇팔 3D")));

    // 문제가 있을 때만 나타나는 표시. 문장을 늘 띄워 두면 자리를 차지하고,
    // 정작 문제가 생겼을 때 다른 문장과 구분되지 않는다. 마우스를 올리면
    // 무엇이 문제인지 나온다.
    poseWarning_ = new QLabel(QStringLiteral("!"));
    poseWarning_->setObjectName(QStringLiteral("PoseWarning"));
    poseWarning_->setAlignment(Qt::AlignCenter);
    poseWarning_->setFixedSize(18, 18);
    poseWarning_->hide();
    head->addSpacing(metrics::s2);
    head->addWidget(poseWarning_, 0, Qt::AlignVCenter);

    previewStatus_ = new QLabel;
    previewStatus_->setObjectName(QStringLiteral("ArmPreviewStatus"));
    previewStatus_->setToolTip(QStringLiteral("목표 보내기를 누르기 전까지 로봇팔은 움직이지 않습니다"));
    previewStatus_->hide();
    head->addSpacing(metrics::s2);
    head->addWidget(previewStatus_, 0, Qt::AlignVCenter);

    head->addStretch(1);
    auto *reset = new QPushButton(QStringLiteral("시점 초기화"));
    reset->setProperty("size", "sm");
    head->addWidget(reset);
    card_->body()->addLayout(head);

    view3d_ = new Robot3DView;
    view3d_->setFixedHeight(210);
    view3d_->setStale(true);
    view3d_->setToolTip(QStringLiteral("실제 관절값 수신 전"));
    card_->body()->addWidget(view3d_);
    connect(reset, &QPushButton::clicked, view3d_, &Robot3DView::resetCamera);
}

void ArmPanel::buildCommandTabs()
{
    commandTabs_ = new QTabWidget;
    commandTabs_->setObjectName(QStringLiteral("ArmCommandTabs"));
    commandTabs_->setDocumentMode(true);
    commandTabs_->addTab(buildJointTab(), QStringLiteral("관절"));
    commandTabs_->addTab(buildEeTab(), QStringLiteral("끝단 위치"));
    controlsLayout_->addWidget(commandTabs_);

    // 두 입력은 같은 목표를 다르게 적은 것이므로 한쪽을 바꾸면 다른 쪽도
    // 즉시 갱신한다. 첫 텔레메트리 전에도 서로 같은 자세를 보여준다.
    syncEeFromJoints();
}

QWidget *ArmPanel::buildJointTab()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("ArmJointControls"));
    auto *lay = new QVBoxLayout(page);
    // 설명 한 줄은 지웠다. 한 번 읽으면 그만인 문장이 세로 공간을 계속
    // 차지하고, 그만큼 3D 뷰가 줄어든다. 같은 내용은 슬라이더 도구 설명에 있다.
    lay->setContentsMargins(metrics::s2, metrics::s1, metrics::s2, metrics::s2);
    lay->setSpacing(0);

    for (const auto &j : kFr3Joints) {
        auto *slider = new ValueSlider(QString::fromUtf8(j.label), j.lo, j.hi,
                                       QStringLiteral("°"), 1, 180.0 / M_PI);
        slider->setObjectName(QStringLiteral("Joint%1").arg(sliders_.size() + 1));
        connect(slider, &QSlider::valueChanged, this, &ArmPanel::onSliderMoved);
        lay->addWidget(slider);
        sliders_ << slider;
    }

    // 실측값을 받기 전에도 3D 모델의 시작 자세와 슬라이더가 같아야 한다.
    // 연결 후 첫 정상 관절 보고가 오면 그 값으로 다시 맞춘다.
    syncing_ = true;
    for (int i = 0; i < sliders_.size(); ++i)
        sliders_.at(i)->setCommand(robot::kArmHome.at(i));
    syncing_ = false;

    lay->addSpacing(metrics::s1);
    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s2);
    auto *send = new QPushButton(QStringLiteral("관절 목표 보내기"));
    jointSend_ = send;
    send->setProperty("variant", "primary");
    send->setProperty("size", "sm");
    auto *sync = new QPushButton(QStringLiteral("실제 자세로 되돌리기"));
    sync->setToolTip(QStringLiteral("슬라이더를 로봇의 지금 각도로 되돌립니다"));
    sync->setProperty("size", "sm");
    row->addWidget(send, 1);
    row->addWidget(sync, 1);
    lay->addLayout(row);
    lay->addStretch(1);
    commandButtons_ << send << sync;

    connect(send, &QPushButton::clicked, this, [this] {
        QList<double> q;
        for (auto *s : std::as_const(sliders_))
            q << s->command();
        emit jointGoal(q);
    });
    connect(sync, &QPushButton::clicked, this, &ArmPanel::syncSlidersToActual);
    return page;
}

QWidget *ArmPanel::buildEeTab()
{
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("ArmEeControls"));
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(metrics::s2, metrics::s1, metrics::s2, metrics::s2);
    lay->setSpacing(0);

    // 관절과 같은 조작으로 통일한다. 한쪽은 슬라이더, 한쪽은 스핀박스면
    // 같은 성격의 값을 다루는 방법을 두 번 배워야 한다.
    struct Spec { const char *key; const char *label; double lo, hi; const char *unit;
                  int decimals; double scale; };
    // 축 이름은 X/Y/Z/Roll/Pitch/Yaw 를 그대로 쓴다. 이 입력을 쓰는 사람은
    // 좌표계를 아는 사람이고, "숙임" 같은 말로 바꾸면 어느 축인지 오히려
    // 되짚어야 한다. 조작자용 표현이 필요한 곳은 프리셋 쪽이다.
    static const Spec specs[] = {
        {"x", "X", -1.0, 1.0, " m", 2, 1.0},
        {"y", "Y", -1.0, 1.0, " m", 2, 1.0},
        {"z", "Z", -0.5, 1.5, " m", 2, 1.0},
        {"roll", "Roll", -M_PI, M_PI, "°", 0, 180.0 / M_PI},
        {"pitch", "Pitch", -M_PI, M_PI, "°", 0, 180.0 / M_PI},
        {"yaw", "Yaw", -M_PI, M_PI, "°", 0, 180.0 / M_PI},
    };

    for (const auto &sp : specs) {
        auto *slider = new ValueSlider(QString::fromUtf8(sp.label), sp.lo, sp.hi,
                                       QString::fromUtf8(sp.unit), sp.decimals, sp.scale);
        slider->setObjectName(QStringLiteral("Ee_%1").arg(QString::fromLatin1(sp.key)));
        slider->setCyclic(qFuzzyCompare(sp.hi - sp.lo, 2 * M_PI));
        // 값은 채우지 않는다. 명령값은 관절에서 정기구학으로(buildCommandTabs
        // 끝), 기준값은 실제 관절에서 온다. 여기에 그럴듯한 상수를 박아 두면
        // 두 입력이 서로 다른 자세를 말하는 채로 화면이 열린다.
        connect(slider, &QSlider::valueChanged, this, &ArmPanel::syncJointsFromEe);
        lay->addWidget(slider);
        ee_.insert(QString::fromLatin1(sp.key), slider);
    }

    lay->addSpacing(metrics::s1);
    auto *row = new QHBoxLayout;
    row->setSpacing(metrics::s2);

    auto *send = new QPushButton(QStringLiteral("끝단 목표 보내기"));
    eeSend_ = send;
    send->setProperty("variant", "primary");
    send->setProperty("size", "sm");

    // 관절 조작과 마찬가지로 되돌릴 방법을 제공한다.
    auto *revert = new QPushButton(QStringLiteral("되돌리기"));
    revert->setProperty("size", "sm");
    revert->setToolTip(QStringLiteral("로봇이 보고한 실제 자세로 되돌립니다"));
    connect(revert, &QPushButton::clicked, this, [this] {
        syncSlidersToActual();
    });

    row->addWidget(send, 1);
    row->addWidget(revert, 1);
    lay->addLayout(row);
    commandButtons_ << send << revert;

    connect(send, &QPushButton::clicked, this, [this] {
        emit eeGoal(QVariantMap{
            {"x", ee_[QStringLiteral("x")]->command()},
            {"y", ee_[QStringLiteral("y")]->command()},
            {"z", ee_[QStringLiteral("z")]->command()},
            {"roll", ee_[QStringLiteral("roll")]->command()},
            {"pitch", ee_[QStringLiteral("pitch")]->command()},
            {"yaw", ee_[QStringLiteral("yaw")]->command()},
            {"frame", QStringLiteral("fr3_link0")},
        });
        // 기준값은 건드리지 않는다. 팔이 실제로 그 자세에 닿을 때까지
        // 편집 표시가 남아 있는 것이 맞다 — 관절 입력도 그렇게 동작한다.
    });

    lay->addStretch(1);
    return page;
}

QWidget *ArmPanel::buildPoseManagementTab()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(metrics::s1, metrics::s1, metrics::s1, metrics::s1);
    savedPresets_ = new QListWidget;
    savedPresets_->setObjectName(QStringLiteral("SavedArmPosePresets"));
    savedPresets_->setMinimumHeight(100);
    savedPresets_->setSpacing(0);
    savedPresets_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    layout->addWidget(savedPresets_, 1);
    return page;
}

void ArmPanel::savePose(bool measured)
{
    if (!controlsEnabled_ || !pendingPose_.isEmpty() ||
        (measured && !hadArmState_) || (!measured && !eeReachable_))
        return;

    int number = 1;
    QString name;
    do {
        name = QStringLiteral("자세 %1").arg(number++);
    } while (std::any_of(posePresets_.cbegin(), posePresets_.cend(),
                         [&name](const QVariantMap &pose) {
                             return !pose.value(QStringLiteral("archived")).toBool() &&
                                    pose.value(QStringLiteral("name")).toString() == name;
                         }));
    QVariantList positions;
    for (int i = 0; i < robot::kArmJointCount; ++i)
        positions << (measured ? actual_.at(i) : sliders_.at(i)->command());
    pendingPose_ = {{QStringLiteral("id"), QUuid::createUuid().toString(
                         QUuid::WithoutBraces).remove(QLatin1Char('-'))},
                    {QStringLiteral("name"), name},
                    {QStringLiteral("description"), QString()},
                    {QStringLiteral("positions"), positions}};
    rebuildPoseList();
    sectionTabs_->setCurrentIndex(1);
    refreshCommandControls();
    emit savePosePresetRequested(pendingPose_);
}

void ArmPanel::setPosePresets(const QList<QVariantMap> &presets)
{
    QString editingId;
    QString draftName;
    QString draftDescription;
    QList<double> draftJoints;
    for (auto it = poseRows_.cbegin(); it != poseRows_.cend(); ++it) {
        auto *row = it.value();
        if (!row->isEditing())
            continue;
        editingId = it.key();
        if (auto *field = row->findChild<QLineEdit *>(QStringLiteral("PoseRowName")))
            draftName = field->text();
        if (auto *field = row->findChild<QLineEdit *>(QStringLiteral("PoseRowDescription")))
            draftDescription = field->text();
        for (int i = 1; i <= robot::kArmJointCount; ++i)
            if (auto *field = row->findChild<QDoubleSpinBox *>(
                    QStringLiteral("PoseRowJoint%1").arg(i)))
                draftJoints << field->value();
        break;
    }
    posePresets_ = presets;
    const QString pendingId = pendingPose_.value(QStringLiteral("id")).toString();
    if (!pendingId.isEmpty() && std::any_of(presets.cbegin(), presets.cend(),
        [&pendingId](const QVariantMap &preset) {
            return preset.value(QStringLiteral("id")).toString() == pendingId;
        }))
        pendingPose_.clear();
    rebuildPoseList();
    if (auto *row = poseRows_.value(editingId, nullptr); row && !editingId.isEmpty()) {
        row->findChild<QLineEdit *>(QStringLiteral("PoseRowName"))->setText(draftName);
        row->findChild<QLineEdit *>(QStringLiteral("PoseRowDescription"))
            ->setText(draftDescription);
        for (int i = 0; i < draftJoints.size(); ++i)
            row->findChild<QDoubleSpinBox *>(QStringLiteral("PoseRowJoint%1").arg(i + 1))
                ->setValue(draftJoints.at(i));
        row->setEditing(true);
        if (editingId == pendingPoseUpdateId_)
            row->setPending(true);
        for (int i = 0; i < savedPresets_->count(); ++i)
            if (savedPresets_->item(i)->data(Qt::UserRole).toString() == editingId) {
                savedPresets_->item(i)->setSizeHint(QSize(0, row->sizeHint().height()));
                break;
            }
    }
    refreshCommandControls();
}

void ArmPanel::rebuildPoseList()
{
    if (!savedPresets_)
        return;
    const QString selectedId = savedPresets_->currentItem()
        ? savedPresets_->currentItem()->data(Qt::UserRole).toString() : QString{};
    const QSignalBlocker blocker(savedPresets_);
    poseRows_.clear();
    savedPresets_->clear();
    QList<QVariantMap> shown = posePresets_;
    if (!pendingPose_.isEmpty())
        shown << pendingPose_;
    for (const auto &pose : shown) {
        if (pose.value(QStringLiteral("archived")).toBool())
            continue;
        const QString id = pose.value(QStringLiteral("id")).toString();
        const bool pending = id == pendingPose_.value(QStringLiteral("id")).toString();
        auto *item = new QListWidgetItem(savedPresets_);
        item->setData(Qt::UserRole, id);
        auto *row = new CatalogRow(savedPresets_);
        row->setName(pose.value(QStringLiteral("name")).toString());
        QStringList joints;
        const auto positions = pose.value(QStringLiteral("positions")).toList();
        for (int i = 0; i < positions.size(); ++i)
            joints << QStringLiteral("J%1 %2°").arg(i + 1)
                          .arg(qRadiansToDegrees(positions.at(i).toDouble()), 0, 'f', 1);
        row->setDetails(pending ? QStringLiteral("로봇에 저장 중…")
                                : joints.join(QStringLiteral(" · ")));
        row->setPending(pending);
        poseRows_.insert(id, row);
        row->onSelected([this, item] { savedPresets_->setCurrentItem(item); });
        row->setToolTip(QStringLiteral("선택하면 3D 화면에 자세를 미리 봅니다"));
        row->applyButton()->setText(QStringLiteral("실행"));
        row->applyButton()->setToolTip(QStringLiteral("로봇팔을 이 자세로 이동합니다"));
        item->setSizeHint(QSize(0, row->sizeHint().height()));
        savedPresets_->setItemWidget(item, row);

        auto *form = new QFormLayout;
        auto *name = new QLineEdit(pose.value(QStringLiteral("name")).toString());
        name->setObjectName(QStringLiteral("PoseRowName"));
        name->setMaxLength(80);
        form->addRow(QStringLiteral("이름"), name);
        auto *description = new QLineEdit(pose.value(QStringLiteral("description")).toString());
        description->setObjectName(QStringLiteral("PoseRowDescription"));
        description->setMaxLength(400);
        form->addRow(QStringLiteral("설명"), description);
        row->editorLayout()->addLayout(form);
        auto *jointGrid = new QGridLayout;
        jointGrid->setHorizontalSpacing(metrics::s2);
        jointGrid->setVerticalSpacing(metrics::s1);
        QList<QDoubleSpinBox *> jointsEditor;
        for (int i = 0; i < robot::kArmJointCount; ++i) {
            auto *spin = new QDoubleSpinBox;
            spin->setObjectName(QStringLiteral("PoseRowJoint%1").arg(i + 1));
            spin->setDecimals(3);
            spin->setRange(qRadiansToDegrees(kFr3Joints.at(i).lo),
                           qRadiansToDegrees(kFr3Joints.at(i).hi));
            spin->setSuffix(QStringLiteral("°"));
            spin->setValue(qRadiansToDegrees(positions.value(i).toDouble()));
            jointGrid->addWidget(new QLabel(QStringLiteral("J%1").arg(i + 1)), i / 2, (i % 2) * 2);
            jointGrid->addWidget(spin, i / 2, (i % 2) * 2 + 1);
            jointsEditor << spin;
        }
        jointGrid->setColumnStretch(1, 1);
        jointGrid->setColumnStretch(3, 1);
        row->editorLayout()->addLayout(jointGrid);
        auto *actions = new QHBoxLayout;
        auto *save = new QPushButton(QStringLiteral("저장"));
        save->setObjectName(QStringLiteral("PoseRowSave"));
        save->setProperty("variant", "primary");
        auto *remove = new QPushButton(QStringLiteral("삭제"));
        remove->setObjectName(QStringLiteral("PoseRowDelete"));
        auto *cancel = new QPushButton(QStringLiteral("취소"));
        cancel->setObjectName(QStringLiteral("PoseRowCancel"));
        for (auto *button : {save, remove, cancel})
            button->setProperty("size", "sm");
        actions->addWidget(save, 1);
        actions->addWidget(remove);
        actions->addWidget(cancel);
        row->editorLayout()->addLayout(actions);
        const auto resizeItem = [this, item, row] {
            row->layout()->activate();
            item->setSizeHint(QSize(0, row->sizeHint().height()));
            savedPresets_->doItemsLayout();
        };
        connect(row->editButton(), &QPushButton::clicked, this, [this, row, resizeItem] {
            for (int i = 0; i < savedPresets_->count(); ++i) {
                auto *otherItem = savedPresets_->item(i);
                auto *other = static_cast<CatalogRow *>(savedPresets_->itemWidget(otherItem));
                if (other && other != row && other->isEditing()) {
                    other->setEditing(false);
                    otherItem->setSizeHint(QSize(0, other->sizeHint().height()));
                }
            }
            row->setEditing(true);
            resizeItem();
        });
        connect(cancel, &QPushButton::clicked, this, [row, resizeItem] {
            row->setEditing(false);
            resizeItem();
        });
        connect(save, &QPushButton::clicked, this,
                [this, id, name, description, jointsEditor, row] {
            const auto it = std::find_if(posePresets_.cbegin(), posePresets_.cend(),
                [&id](const QVariantMap &entry) {
                    return entry.value(QStringLiteral("id")).toString() == id;
                });
            if (it == posePresets_.cend())
                return;
            const QString newName = name->text().trimmed();
            const QString newDescription = description->text().trimmed();
            const bool duplicate = std::any_of(posePresets_.cbegin(), posePresets_.cend(),
                [&id, &newName](const QVariantMap &entry) {
                    return entry.value(QStringLiteral("id")).toString() != id &&
                           !entry.value(QStringLiteral("archived")).toBool() &&
                           entry.value(QStringLiteral("name")).toString() == newName;
                });
            if (newName.isEmpty() || newName.toUtf8().size() > 80 ||
                newDescription.toUtf8().size() > 400 || duplicate) {
                commandStatus_->setText(QStringLiteral("이름·설명을 확인하십시오. 같은 이름은 사용할 수 없습니다."));
                commandStatus_->setProperty("tone", "danger");
                theme::repolish(commandStatus_);
                commandStatus_->show();
                return;
            }
            QVariantMap updated = *it;
            updated[QStringLiteral("name")] = newName;
            updated[QStringLiteral("description")] = newDescription;
            QVariantList values;
            for (int i = 0; i < jointsEditor.size(); ++i)
                values << qBound(kFr3Joints.at(i).lo,
                                 qDegreesToRadians(jointsEditor.at(i)->value()),
                                 kFr3Joints.at(i).hi);
            updated[QStringLiteral("positions")] = values;
            pendingPoseUpdateId_ = id;
            row->setPending(true);
            emit updatePosePresetRequested(
                updated, it->value(QStringLiteral("revision"), quint64{1}).toULongLong());
        });
        connect(remove, &QPushButton::clicked, this, [this, id, row] {
            const auto it = std::find_if(posePresets_.cbegin(), posePresets_.cend(),
                [&id](const QVariantMap &entry) {
                    return entry.value(QStringLiteral("id")).toString() == id;
                });
            if (it == posePresets_.cend() || QMessageBox::question(
                this, QStringLiteral("자세 삭제"),
                QStringLiteral("'%1' 자세를 삭제하시겠습니까?")
                    .arg(it->value(QStringLiteral("name")).toString()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
                return;
            row->setPending(true);
            emit archivePosePresetRequested(
                id, it->value(QStringLiteral("revision"), quint64{1}).toULongLong());
        });
        connect(row->applyButton(), &QPushButton::clicked, this,
                [this, id] { applySavedPose(id); });
        if (id == selectedId)
            savedPresets_->setCurrentItem(item);
    }
    updatePoseRows();
}

void ArmPanel::updatePoseRows()
{
    for (auto *row : std::as_const(poseRows_)) {
        row->setEditEnabled(controlsEnabled_);
        row->setApplyEnabled(controlsEnabled_ && executionAvailable_ && hadArmState_);
    }
}

void ArmPanel::previewSavedPose(const QString &id)
{
    const auto it = std::find_if(posePresets_.cbegin(), posePresets_.cend(),
        [&id](const QVariantMap &entry) {
            return entry.value(QStringLiteral("id")).toString() == id &&
                   !entry.value(QStringLiteral("archived")).toBool();
        });
    if (it == posePresets_.cend())
        return;
    const auto positions = it->value(QStringLiteral("positions")).toList();
    if (positions.size() != robot::kArmJointCount)
        return;
    syncing_ = true;
    for (int i = 0; i < sliders_.size(); ++i)
        sliders_.at(i)->setCommand(positions.at(i).toDouble());
    syncing_ = false;
    onSliderMoved();
}

void ArmPanel::applySavedPose(const QString &id)
{
    if (!controlsEnabled_ || !executionAvailable_ || !hadArmState_)
        return;
    const auto it = std::find_if(posePresets_.cbegin(), posePresets_.cend(),
        [&id](const QVariantMap &entry) {
            return entry.value(QStringLiteral("id")).toString() == id &&
                   !entry.value(QStringLiteral("archived")).toBool();
        });
    if (it == posePresets_.cend())
        return;
    const auto positions = it->value(QStringLiteral("positions")).toList();
    if (positions.size() != robot::kArmJointCount)
        return;
    QList<double> goal;
    syncing_ = true;
    for (int i = 0; i < sliders_.size(); ++i) {
        goal << positions.at(i).toDouble();
        sliders_.at(i)->setCommand(goal.back());
    }
    syncing_ = false;
    onSliderMoved();
    emit jointGoal(goal);
}

void ArmPanel::setArmState(const QList<double> &positions, double manipulability,
                           double sigmaMin, const QString &moveitState)
{
    // BridgeClient 는 상태가 오기 전에도 빈 텔레메트리 스냅샷을 주기적으로
    // 보낸다. 그것을 첫 관절 보고로 취급하면 hadArmState_ 가 너무 일찍 켜져
    // 편집 중인 목표를 실제값과 비교할 수 없고, 미리보기가 지워진다.
    // FR3 는 6축이므로 전체 관절값이 도착한 경우에만 기준 자세를 갱신한다.
    if (positions.size() >= kArmJointCount) {
        const bool first = !hadArmState_;
        hadArmState_ = true;
        actual_ = positions;
        for (int i = 0; i < sliders_.size() && i < positions.size(); ++i)
            sliders_[i]->setActual(positions.at(i));

        // 첫 보고 전에 목표를 편집했을 수도 있다. 그 경우 실제값만 갱신하고
        // 초안을 보존한다. 이전에는 첫 상태 패킷이 입력값을 조용히 지웠다.
        if (first && !commandEdited_)
            syncSlidersToActual();

        syncEeActualFromJoints(positions);
        view3d_->setArmJoints(positions);
        view3d_->setStale(false);
        view3d_->setToolTip({});
        refreshPreview();
    }

    const bool metricKnown = std::isfinite(manipulability) && manipulability >= 0.0;
    const double norm = metricKnown
        ? qBound(0.0, manipulability / kManipNominal, 1.0) : 1.0;

    // 여유가 있을 때는 아무 말도 하지 않는다. "정상입니다" 를 늘 띄워두면
    // 정말 문제가 생겼을 때의 한 줄이 똑같이 생긴 한 줄로 보인다.
    if (!hadArmState_) {
        advice_->hide();
    } else if (metricKnown && norm <= kManipDanger) {
        advice_->setText(QStringLiteral(
            "팔이 거의 다 펴졌거나 접혔습니다. 이 자세에서는 어떤 방향으로는 "
            "아예 움직이지 못합니다. 팔을 조금 되돌리거나 로봇을 옮기십시오."));
    } else if (metricKnown && norm <= kManipWarn) {
        advice_->setText(QStringLiteral(
            "움직일 수 있는 여유가 줄었습니다. 더 뻗으면 멈출 수 있으니 "
            "로봇을 조금 옮겨 자세를 바꾸는 편이 낫습니다."));
    }
    advice_->setVisible(hadArmState_ && metricKnown && norm <= kManipWarn);
    advice_->setToolTip(metricKnown
        ? QStringLiteral("조작성 지수 %1 · 최소 특이값 %2")
              .arg(manipulability, 0, 'f', 4).arg(sigmaMin, 0, 'f', 4)
        : QStringLiteral("로봇이 조작성 지수를 보내지 않았습니다."));
    view3d_->setSingularWarning(hadArmState_ && metricKnown && norm <= kManipWarn);

    if (moveitState == QLatin1String("planning"))
        state_->set(QStringLiteral("계획 중"), QStringLiteral("info"));
    else if (moveitState == QLatin1String("executing"))
        state_->set(QStringLiteral("실행 중"), QStringLiteral("info"));
    else if (moveitState == QLatin1String("error"))
        state_->set(QStringLiteral("오류"), QStringLiteral("danger"));
    else if (moveitState == QLatin1String("idle"))
        state_->set(QStringLiteral("대기"), QStringLiteral("neutral"));
    else
        state_->set(QStringLiteral("동작 상태 미제공"), QStringLiteral("neutral"));
}

void ArmPanel::setControlsEnabled(bool on)
{
    controlsEnabled_ = on;
    for (auto *s : std::as_const(sliders_))
        s->setEnabled(on);
    for (auto *s : std::as_const(ee_))
        s->setEnabled(on);
    refreshCommandControls();
}

void ArmPanel::setExecutionAvailable(bool available)
{
    if (executionAvailable_ == available)
        return;
    executionAvailable_ = available;
    refreshCommandControls();
}

void ArmPanel::clearReportedState()
{
    pendingPoseUpdateId_.clear();
    pendingPose_.clear();
    rebuildPoseList();
    hadArmState_ = false;
    commandEdited_ = false;
    hasPendingGoal_ = false;
    eeReachable_ = true;
    actual_ = QList<double>(robot::kArmHome.begin(), robot::kArmHome.end());
    syncing_ = true;
    for (int i = 0; i < sliders_.size(); ++i)
        sliders_.at(i)->setCommand(robot::kArmHome.at(i));
    syncing_ = false;
    syncEeFromJoints();
    for (auto *s : std::as_const(sliders_))
        s->clearActual();
    for (auto *s : std::as_const(ee_))
        s->clearActual();
    view3d_->setPreviewJoints({});
    view3d_->setArmJoints(actual_);
    view3d_->setStale(true);
    view3d_->setToolTip(QStringLiteral("실제 관절값 수신 전"));
    previewStatus_->clear();
    previewStatus_->hide();
    commandStatus_->hide();
    advice_->hide();
    showPoseWarning({});
    refreshCommandControls();
}

void ArmPanel::setCommandResult(const QString &channel, bool ok, const QString &code,
                                const QString &message)
{
    if (channel == QLatin1String("cmd/arm/pose_presets/save") ||
        channel == QLatin1String("cmd/arm/pose_presets/update") ||
        channel == QLatin1String("cmd/arm/pose_presets/archive")) {
        if (channel == QLatin1String("cmd/arm/pose_presets/update")) {
            pendingPoseUpdateId_.clear();
            if (ok)
                rebuildPoseList();
        }
        if (!ok && channel == QLatin1String("cmd/arm/pose_presets/save")) {
            pendingPose_.clear();
            rebuildPoseList();
        }
        if (!ok)
            for (auto *row : std::as_const(poseRows_))
                row->setPending(false);
        const bool archived = channel == QLatin1String("cmd/arm/pose_presets/archive");
        commandStatus_->setText(ok ? (archived ? QStringLiteral("자세 삭제됨")
                                               : QStringLiteral("자세 저장됨"))
                                   : QStringLiteral("자세 변경 실패 · %1 %2").arg(code, message));
        commandStatus_->setProperty("tone", ok ? "info" : "danger");
        theme::repolish(commandStatus_);
        commandStatus_->show();
        refreshCommandControls();
        return;
    }
    if (channel != QLatin1String("cmd/arm/joint_goal") &&
        channel != QLatin1String("cmd/arm/ee_goal"))
        return;
    commandStatus_->setText(ok ? QStringLiteral("목표 요청 접수")
                               : QStringLiteral("전송 실패 · %1 %2").arg(code, message));
    commandStatus_->setProperty("tone", ok ? "info" : "danger");
    theme::repolish(commandStatus_);
    commandStatus_->setVisible(true);
}

void ArmPanel::refreshCommandControls()
{
    if (savePreviewPose_)
        savePreviewPose_->setEnabled(controlsEnabled_ && eeReachable_ &&
                                     pendingPose_.isEmpty());
    if (savePose_)
        savePose_->setEnabled(controlsEnabled_ && hadArmState_ && pendingPose_.isEmpty());
    for (auto *b : std::as_const(commandButtons_))
        b->setEnabled(controlsEnabled_ && hadArmState_);
    if (jointSend_)
        jointSend_->setEnabled(controlsEnabled_ && executionAvailable_ &&
                               hadArmState_ && hasPendingGoal_);
    if (eeSend_)
        eeSend_->setEnabled(controlsEnabled_ && executionAvailable_ &&
                            hadArmState_ && hasPendingGoal_ && eeReachable_);
    for (auto *send : {jointSend_, eeSend_})
        if (send)
            send->setToolTip(executionAvailable_ ? QString()
                : QStringLiteral("팔 실행기 비활성"));
    updatePoseRows();
}

void ArmPanel::onSliderMoved()
{
    if (syncing_)
        return;
    commandEdited_ = true;
    commandStatus_->hide();
    for (auto *s : std::as_const(sliders_))
        s->update();
    syncEeFromJoints();
    refreshPreview();
}

void ArmPanel::syncEeFromJoints()
{
    if (ee_.isEmpty())
        return;

    // 두 입력은 같은 목표를 다르게 적은 것이다. 한쪽을 만지고 다른 쪽을 그대로
    // 두면 조작자가 보는 숫자가 실제로 보낼 자세와 달라진다.
    QList<double> q;
    for (auto *s : std::as_const(sliders_))
        q << s->command();

    const auto pose = robot::forwardKinematics(q);
    syncing_ = true;
    ee_[QStringLiteral("x")]->setCommand(pose.x);
    ee_[QStringLiteral("y")]->setCommand(pose.y);
    ee_[QStringLiteral("z")]->setCommand(pose.z);
    // 각도는 지금 표시된 기준값과 같은 바퀴에 올려 둔다.
    for (const auto &axis : {std::pair{"roll", pose.roll}, {"pitch", pose.pitch},
                             {"yaw", pose.yaw}}) {
        auto *s = ee_[QLatin1String(axis.first)];
        s->setCommand(wrapNear(axis.second, s->actual()));
    }
    syncing_ = false;
    eeReachable_ = true;
}

void ArmPanel::syncEeActualFromJoints(const QList<double> &joints)
{
    if (ee_.isEmpty() || joints.size() < robot::kArmJointCount)
        return;

    // 로봇은 끝단 좌표를 따로 보고하지 않는다. 그래도 관절은 보고하고 끝단은
    // 관절에서 나오므로, 정기구학을 한 번 돌리면 두 입력이 같은 하나의 자세를
    // 말한다. 예전에는 "마지막으로 보낸 값" 을 지금 자세라고 적어 두었는데,
    // 그것은 팔이 실제로 어디 있는지와 아무 상관이 없는 숫자였다.
    const auto pose = robot::forwardKinematics(joints);
    ee_[QStringLiteral("x")]->setActual(pose.x);
    ee_[QStringLiteral("y")]->setActual(pose.y);
    ee_[QStringLiteral("z")]->setActual(pose.z);
    for (const auto &axis : {std::pair{"roll", pose.roll}, {"pitch", pose.pitch},
                             {"yaw", pose.yaw}}) {
        auto *s = ee_[QLatin1String(axis.first)];
        s->setActual(wrapNear(axis.second, s->command()));
    }
}

void ArmPanel::syncJointsFromEe()
{
    if (syncing_ || sliders_.isEmpty())
        return;
    commandEdited_ = true;
    commandStatus_->hide();

    robot::EePose target;
    target.x = ee_[QStringLiteral("x")]->command();
    target.y = ee_[QStringLiteral("y")]->command();
    target.z = ee_[QStringLiteral("z")]->command();
    target.roll = ee_[QStringLiteral("roll")]->command();
    target.pitch = ee_[QStringLiteral("pitch")]->command();
    target.yaw = ee_[QStringLiteral("yaw")]->command();

    // 지금 관절에서 출발해 가장 가까운 해를 찾는다. 팔이 갑자기 뒤집히면
    // 조작자는 자기가 그렇게 시킨 줄 안다.
    QList<double> seed;
    for (auto *s : std::as_const(sliders_))
        seed << s->command();

    const auto solved = robot::inverseKinematics(target, seed);
    if (!solved) {
        // 닿지 않는 자리다. 관절을 억지로 옮기지 않는다 — 가장 가까운 자세로
        // 슬쩍 옮겨 두면 조작자는 자기가 지정한 자리로 간다고 믿는다.
        eeReachable_ = false;
        refreshPreview();
        return;
    }

    eeReachable_ = true;
    syncing_ = true;
    for (int i = 0; i < sliders_.size() && i < solved->size(); ++i)
        sliders_[i]->setCommand(solved->at(i));
    syncing_ = false;

    for (auto *s : std::as_const(sliders_))
        s->update();
    refreshPreview();
}

void ArmPanel::refreshPreview()
{
    // 3D 뷰에 보낼 자세를 겹쳐 보여준다. 화면에만 반영되고 로봇은 움직이지
    // 않는다 — 실제 명령은 "보내기" 를 눌러야 나간다.
    QList<double> q;
    bool differs = false;
    bool differsFromActual = !hadArmState_;
    for (int i = 0; i < sliders_.size(); ++i) {
        q << sliders_.at(i)->command();
        if (sliders_.at(i)->diverged())
            differs = true;
        if (hadArmState_ && i < actual_.size()
            && !qFuzzyCompare(q.back() + 1.0, actual_.at(i) + 1.0)) {
            differsFromActual = true;
        }
    }
    // 어느 입력에서 만졌든 3D 는 보낼 자세를 보여준다. 끝단 값은 역기구학을
    // 거쳐 이미 관절로 옮겨져 있다. "보내지 않은 편집" 표시는 기준값과
    // 두 눈금 이상 차이 날 때만 띄우지만, 3D 미리보기는 한 눈금 변화도
    // 보여야 한다. 특히 첫 텔레메트리 전에는 기준값이 없더라도 입력을
    // 버리면 안 된다.
    hasPendingGoal_ = commandEdited_ && differsFromActual;
    view3d_->setPreviewJoints(hasPendingGoal_ ? q : QList<double>{});
    const QString previewText = !eeReachable_ ? QStringLiteral("끝단 목표 도달 불가")
                              : hasPendingGoal_ ? QStringLiteral("목표 미리보기") : QString();
    previewStatus_->setText(previewText);
    previewStatus_->setVisible(!previewText.isEmpty());
    refreshCommandControls();

    // 보내기 전에 조용히 알린다. 로봇이 최종 판정을 하지만, 눌러 본 뒤에야
    // 거부 코드로 알게 되는 것보다 낫다. 요란하게 막지는 않는다 — 조작자가
    // 의도해서 그 자세로 가는 경우도 있다.
    robot::PoseWarning warning;
    if (!eeReachable_) {
        warning = {QStringLiteral("danger"),
                   QStringLiteral("팔이 닿지 않는 자리입니다. 값을 조금 되돌리십시오.")};
    } else if (differs) {
        warning = robot::checkArmPose(q);
    }
    showPoseWarning(warning);
}

void ArmPanel::showPoseWarning(const robot::PoseWarning &warning)
{
    // 내용이 그대로면 위젯을 건드리지 않는다.
    //
    // 이 함수는 텔레메트리마다, 즉 초당 스무 번 불린다. 그때마다 repolish
    // 로 위젯을 다시 칠하면 Qt 가 도구 설명을 띄우려고 세어 두는 시간이
    // 매번 초기화되어, 아이콘에 마우스를 올려도 설명이 끝내 뜨지 않는다.
    // 화면에는 아무 문제가 없어 보이므로 원인을 찾기 어렵다.
    if (warning.text == lastWarning_.text && warning.severity == lastWarning_.severity)
        return;
    lastWarning_ = warning;

    poseWarning_->setToolTip(warning.text);
    poseWarning_->setProperty("tone", warning.severity);
    poseWarning_->setVisible(!warning.isEmpty());
    theme::repolish(poseWarning_);
}

void ArmPanel::syncSlidersToActual()
{
    if (!hadArmState_)
        return;
    syncing_ = true;
    for (int i = 0; i < sliders_.size() && i < actual_.size(); ++i)
        sliders_[i]->setCommand(actual_.at(i));
    syncing_ = false;
    syncEeFromJoints();
    commandEdited_ = false;
    refreshPreview();
}

}  // namespace hmi::ui
