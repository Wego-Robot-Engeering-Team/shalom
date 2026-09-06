#include "robot/RobotLink.h"

#include <QMetaType>

namespace gcs::robot {
namespace {

/// 시그널 인자 타입을 등록한다.
///
/// 같은 스레드 안의 직접 연결에는 필요 없지만, 나중에 브릿지 입출력을 별도
/// 스레드로 옮기면 큐 연결이 되고, 등록되지 않은 타입은 그 순간 조용히
/// 전달되지 않는다. 원인을 찾기 어려운 종류의 고장이라 미리 등록해 둔다.
const int kRegistered = [] {
    qRegisterMetaType<gcs::robot::Telemetry>("gcs::robot::Telemetry");
    qRegisterMetaType<gcs::robot::MissionState>("gcs::robot::MissionState");
    qRegisterMetaType<gcs::robot::DriveMode>("gcs::robot::DriveMode");
    return 0;
}();

}  // namespace
}  // namespace gcs::robot
