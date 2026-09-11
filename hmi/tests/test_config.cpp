// 로봇 목록의 저장 규칙.
//
// 이 목록은 관제가 어디에 붙을지를 정하므로, 여기서 조용히 어긋나면 화면은
// "연결 안 됨" 만 보여 준다. 무엇이 틀렸는지는 말해 주지 않는다.
//
// 실제 설정 파일을 건드리지 않도록 QSettings 의 경로를 임시 폴더로 돌린다.
// Config 를 처음 만지기 전에 해야 한다 — 저장소가 static 이라 한 번 만들어지면
// 경로가 굳는다.

#include <QTemporaryDir>
#include <QtTest>

#include "Config.h"

using hmi::Config;
using hmi::RobotEntry;

class TestConfig : public QObject {
    Q_OBJECT

private:
    QTemporaryDir dir_;

    static void setRobots(std::initializer_list<RobotEntry> list)
    {
        Config::instance().setRobots(QList<RobotEntry>(list));
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir_.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir_.path());
    }

    void init()
    {
        setRobots({{QStringLiteral("1호기"), QStringLiteral("192.168.0.11"), 9090},
                   {QStringLiteral("2호기"), QStringLiteral("192.168.0.12"), 9090}});
        Config::instance().setCurrentRobot(0);
    }

    /// 목록이 비어 본 적 없어야 한다. 비면 붙을 곳이 사라진다.
    void never_returns_an_empty_list()
    {
        Config::instance().setRobots({});
        QCOMPARE(Config::instance().robots().size(), 1);
        QVERIFY(!Config::instance().bridgeHost().isEmpty());
    }

    void current_selects_which_address_is_used()
    {
        QCOMPARE(Config::instance().bridgeHost(), QStringLiteral("192.168.0.11"));
        Config::instance().setCurrentRobot(1);
        QCOMPARE(Config::instance().bridgeHost(), QStringLiteral("192.168.0.12"));
    }

    /// 범위 밖 값은 재운다. 목록을 줄인 뒤에 없는 자리를 가리키면, 화면은
    /// 아무 데도 붙지 못하면서 이유를 말하지 못한다.
    void clamps_an_out_of_range_selection()
    {
        Config::instance().setCurrentRobot(99);
        QCOMPARE(Config::instance().currentRobot(), 1);
        Config::instance().setCurrentRobot(-5);
        QCOMPARE(Config::instance().currentRobot(), 0);
    }

    void removing_the_current_entry_leaves_a_valid_one()
    {
        Config::instance().setCurrentRobot(1);
        setRobots({{QStringLiteral("1호기"), QStringLiteral("192.168.0.11"), 9090}});
        QCOMPARE(Config::instance().currentRobot(), 0);
        QCOMPARE(Config::instance().bridgeHost(), QStringLiteral("192.168.0.11"));
    }

    /// 줄어든 목록을 쓰면 옛 항목이 남지 않는다. 배열을 덮어쓰기만 하면
    /// 지운 로봇이 목록에 계속 보인다.
    void shrinking_the_list_drops_the_tail()
    {
        setRobots({{QStringLiteral("혼자"), QStringLiteral("10.0.0.1"), 9090}});
        const auto list = Config::instance().robots();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).host, QStringLiteral("10.0.0.1"));
    }

    void single_address_accessors_edit_the_current_entry()
    {
        Config::instance().setCurrentRobot(1);
        Config::instance().setBridgeHost(QStringLiteral("10.9.9.9"));
        Config::instance().setBridgePort(9191);

        const auto list = Config::instance().robots();
        QCOMPARE(list.at(1).host, QStringLiteral("10.9.9.9"));
        QCOMPARE(list.at(1).port, 9191);
        // 다른 로봇은 그대로다.
        QCOMPARE(list.at(0).host, QStringLiteral("192.168.0.11"));
    }

    /// 예전 단일 주소 설정만 있는 설치본이 갱신돼도 그 주소로 붙는다.
    void migrates_a_legacy_single_address()
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("WEGO Robotics"), QStringLiteral("Inspection HMI"));
        s.remove(QStringLiteral("connection/robots"));
        s.setValue(QStringLiteral("connection/host"), QStringLiteral("172.16.5.4"));
        s.setValue(QStringLiteral("connection/port"), 9090);
        s.sync();

        const auto list = Config::instance().robots();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.at(0).host, QStringLiteral("172.16.5.4"));
        QCOMPARE(Config::instance().bridgeHost(), QStringLiteral("172.16.5.4"));
    }

    void survives_a_round_trip()
    {
        setRobots({{QStringLiteral("검수고"), QStringLiteral("192.168.210.88"), 9090}});
        Config::instance().setCurrentRobot(0);

        QSettings s(QSettings::IniFormat, QSettings::UserScope,
                    QStringLiteral("WEGO Robotics"), QStringLiteral("Inspection HMI"));
        s.sync();
        const int n = s.beginReadArray(QStringLiteral("connection/robots"));
        QCOMPARE(n, 1);
        s.setArrayIndex(0);
        QCOMPARE(s.value(QStringLiteral("host")).toString(),
                 QStringLiteral("192.168.210.88"));
        s.endArray();
    }
};

QTEST_MAIN(TestConfig)
#include "test_config.moc"
