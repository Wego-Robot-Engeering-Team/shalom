// 3D 자세 뷰의 그리기 경로 검사.
//
// 이 뷰는 OpenGL 이 아니라 QPainter 로 그린다 (이유는 Robot3DView.h). 그
// 대가로 면 수에 상한이 있고, 상한을 넘겼는지는 눈으로 알 수 없다 — 화면은
// 여전히 나오고 다만 느려진다. 그래서 기계로 지킨다.

#include <QElapsedTimer>
#include <limits>
#include <QPixmap>
#include <QTest>

#include "RobotDef.h"
#include "widgets/Robot3DView.h"
#include "robot/Kinematics.h"
#include "robot/PoseCheck.h"
#include "widgets/RobotMesh.h"

using namespace hmi;

class TestRobotView : public QObject {
    Q_OBJECT

private slots:

    /// 형상 자체를 읽어야 한다. 리소스가 빠지거나 형식이 어긋나면 화면은
    /// 여전히 뜨고 "3D 형상을 읽지 못했습니다" 만 나오므로, 눈으로는 늦게
    /// 발견된다.
    void mesh_loads()
    {
        const auto &m = ui::mesh::model();
        QVERIFY2(!m.isEmpty(), "robot_mesh.bin 을 읽지 못했다");
        QCOMPARE(int(m.fr3.size()), 7);   // base + 6 링크
        QVERIFY2(m.b2.size() >= 13, "B2 부품이 모자란다 (몸통 + 다리 12)");
    }

    /// 링크 형상 하나에 관절 좌표계 하나가 대응해야 한다.
    ///
    /// Robot3DView 는 `model.fr3[i]` 를 `frames[i]` 로 옮겨 그리고, 끝단
    /// 표시는 `frames.last()` 에 놓는다. 둘의 개수가 어긋나면 루프의 짧은
    /// 쪽에서 잘려 마지막 링크가 통째로 사라지거나 끝단 표시가 팔 끝이
    /// 아닌 곳에 뜨는데, 화면에는 여전히 팔처럼 생긴 것이 그려진다.
    void meshParts_matchTheJointFrames()
    {
        const auto &m = ui::mesh::model();
        const auto frames = robot::jointFrames({robot::kArmHome.begin(), robot::kArmHome.end()});
        QCOMPARE(int(m.fr3.size()), int(frames.size()));
    }

    /// 끝단 탭의 좌표와 그림 속 끝단 표시가 같은 곳을 가리켜야 한다.
    ///
    /// 조작자가 본 증상이 정확히 이것이었다 — 관절값을 넣었는데 그림의
    /// 끝단이 숫자와 딴 곳에 있었다. 두 경로가 각자 체인을 세우고 있어서
    /// 생긴 일이라, 이제 forwardKinematics 도 jointFrames 도 같은
    /// kFr3Chain 하나만 본다. 그 사실을 여기서 붙잡아 둔다.
    void endEffectorMarker_agreesWithTheReadout_data()
    {
        QTest::addColumn<QList<double>>("q");
        QTest::newRow("home") << QList<double>{robot::kArmHome.begin(), robot::kArmHome.end()};
        QTest::newRow("standby") << QList<double>{robot::kArmStandby.begin(), robot::kArmStandby.end()};
        QTest::newRow("stow") << QList<double>{robot::kArmStow.begin(), robot::kArmStow.end()};
        QTest::newRow("영점") << QList<double>{0, 0, 0, 0, 0, 0};
        QTest::newRow("임의") << QList<double>{0.4, -1.3, -1.9, -1.0, -1.2, 0.9};
    }

    void endEffectorMarker_agreesWithTheReadout()
    {
        QFETCH(QList<double>, q);
        const robot::EePose p = robot::forwardKinematics(q);
        const QVector3D shown = robot::jointFrames(q).last().map(QVector3D(0, 0, 0));
        const QVector3D read(float(p.x), float(p.y), float(p.z));
        const double gap = double((shown - read).length());
        QVERIFY2(gap < 1e-4,
                 qPrintable(QStringLiteral("끝단 표시와 읽은 좌표가 %1 mm 어긋난다")
                                .arg(gap * 1000.0, 0, 'f', 2)));
    }

    /// 면 예산. tools/make_robot_mesh.py 의 격자가 이 상한을 지킨다.
    ///
    /// 깊이 버퍼는 정렬을 하지 않으므로 삼각형 수보다 채우는 픽셀 수가
    /// 비용을 지배한다. 그래도 상한을 두는 이유는 리소스 크기와 삼각형당
    /// 설정 비용이고, 무엇보다 화면에서 차이가 보이지 않기 때문이다 —
    /// 16000 면과 595000 면을 나란히 놓아도 구분되지 않는다.
    void meshes_stayWithinFaceBudget()
    {
        const int n = ui::mesh::model().faceCount();
        QVERIFY2(n <= 40000,
                 qPrintable(QStringLiteral("면이 %1 개다. make_robot_mesh.py 의 "
                                           "GRID_M 을 키워라.").arg(n)));
        QVERIFY2(n >= 5000,
                 qPrintable(QStringLiteral("면이 %1 개뿐이다 — 격자가 너무 거칠어 "
                                           "형상이 뭉개졌을 수 있다.").arg(n)));
    }

    /// 서 있는 로봇의 발이 지면에 닿아야 한다. 몸통 높이를 상수로 적어 두면
    /// 언젠가 어긋나고, 화면에서는 로봇이 떠 있거나 잠긴 것으로 보인다.
    void b2_standsOnTheGround()
    {
        const auto &model = ui::mesh::model();
        float lowest = 1e9f;
        for (const auto &m : model.b2)
            for (const auto &v : m.vertices)
                lowest = qMin(lowest, v.z() + model.baseHeight);
        QVERIFY2(qAbs(lowest) < 0.01f,
                 qPrintable(QStringLiteral("가장 낮은 점이 z=%1 이다").arg(lowest)));
    }

    /// 팔 마운트가 몸통 위에 있고 전방 LiDAR 마스트(x=+0.342)보다 뒤에 있다.
    void armMount_sitsBehindTheLidar()
    {
        const auto &mount = ui::mesh::model().armMount;
        QVERIFY(mount.z() > 0.0f);
        QVERIFY2(mount.x() < 0.342f - 0.076f, "팔 밑동이 LiDAR 마스트와 겹친다");
    }

    /// 프리셋을 고르면 경고가 떠서는 안 된다.
    ///
    /// 이전 판정은 팔 밑동 아래 0.10 m 를 전부 몸통으로 봤는데, 실제 트렁크
    /// 윗면은 0.055 m 아래다. 그 차이 때문에 스토우 자세 — 로봇이 주행할 때
    /// 취하는 바로 그 자세 — 가 "몸통과 겹칩니다, 보낼 수 없습니다" 로 막혔다.
    /// 조작자가 팔을 접을 수 없다는 뜻이다.
    void presets_raiseNoWarning_data()
    {
        QTest::addColumn<QList<double>>("joints");
        QTest::newRow("home") << QList<double>{robot::kArmHome.begin(), robot::kArmHome.end()};
        QTest::newRow("standby")
            << QList<double>{robot::kArmStandby.begin(), robot::kArmStandby.end()};
        QTest::newRow("stow") << QList<double>{robot::kArmStow.begin(), robot::kArmStow.end()};
    }

    void presets_raiseNoWarning()
    {
        QFETCH(QList<double>, joints);
        const auto w = robot::checkArmPose(joints);
        QVERIFY2(w.isEmpty(), qPrintable(QStringLiteral("[%1] %2").arg(w.severity, w.text)));
    }

    /// 실제로 한 프레임을 그려 본다. 그리기 경로에서만 터지는 버그는 이것
    /// 말고 잡을 방법이 없고, 시간도 여기서 잰다.
    void paint_completesQuickly()
    {
        ui::Robot3DView view;
        view.resize(640, 480);
        view.setArmJoints({robot::kArmHome.begin(), robot::kArmHome.end()});

        view.grab();  // 첫 프레임은 폰트·캐시 준비가 섞이므로 버린다.

        // 평균이 아니라 최솟값을 본다. 이 검사가 답해야 하는 질문은 "이 그리기
        // 가 얼마나 걸리는가" 이고, 다른 프로세스와의 경합은 거기에 시간을
        // 더하기만 한다. 평균으로 재면 빌드 서버가 바쁠 때마다 무작위로
        // 실패하고, 그런 검사는 곧 무시된다 — 실제로 이 기계에서 부하가
        // 몰렸을 때 11 ms 가 52 ms 로 찍혔다.
        constexpr int kFrames = 30;
        qint64 best = std::numeric_limits<qint64>::max();
        for (int i = 0; i < kFrames; ++i) {
            QElapsedTimer t;
            t.start();
            const QPixmap px = view.grab();
            best = qMin(best, t.nsecsElapsed());
            QVERIFY(!px.isNull());
        }
        const double ms = double(best) / 1e6;
        qInfo("한 프레임 %.1f ms (최솟값, %d 회 중)", ms, kFrames);

        // 30 fps 예산은 33 ms 다. 이 뷰는 화면의 일부일 뿐이므로 그 절반 안에
        // 들어와야 한다. 깊이 버퍼는 위젯의 4 배 픽셀을 채우므로(2 배
        // 슈퍼샘플링) 여기가 비용의 대부분이다.
        //
        // 디버그 빌드는 같은 코드로 2~3 배가 나온다. 인라인이 안 되는 픽셀
        // 루프라 그렇고, 그 숫자는 코드가 아니라 컴파일러를 재는 것이다.
        // 그래서 한계를 구성별로 둔다 — 지켜야 할 값은 납품 쪽이고, 디버그
        // 쪽은 개발 중에 화면이 답답해지는 것을 막는 선이다.
#ifdef NDEBUG
        constexpr double kBudgetMs = 16.0;
#else
        constexpr double kBudgetMs = 40.0;
#endif
        QVERIFY2(ms < kBudgetMs,
                 qPrintable(QStringLiteral("한 프레임에 %1 ms 걸린다 (한계 %2)")
                                .arg(ms).arg(kBudgetMs)));
    }
};

QTEST_MAIN(TestRobotView)
#include "test_robotview.moc"
