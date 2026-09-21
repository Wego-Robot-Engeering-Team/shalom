// Copyright (c) 2026 WeGo Robotics. All rights reserved.

#include "robot/RobotTypes.h"

namespace hmi::robot {

MissionState missionStateFromWire(const QString &value)
{
    if (value == QLatin1String("ready"))              return MissionState::Ready;
    if (value == QLatin1String("running"))            return MissionState::Running;
    if (value == QLatin1String("pausing"))            return MissionState::Pausing;
    if (value == QLatin1String("paused"))             return MissionState::Paused;
    if (value == QLatin1String("recovering"))         return MissionState::Recovering;
    if (value == QLatin1String("returning"))          return MissionState::Returning;
    if (value == QLatin1String("completed"))          return MissionState::Completed;
    if (value == QLatin1String("failed"))             return MissionState::Failed;
    if (value == QLatin1String("emergency_stopped"))  return MissionState::EmergencyStopped;
    if (value == QLatin1String("idle"))               return MissionState::Idle;

    // "fault" 와, 이 빌드가 모르는 값이 함께 여기로 온다. 모르는 상태를
    // idle 로 접으면 화면은 "아무 일도 없음" 으로 보이는데, 실제로는 로봇이
    // 무엇을 하고 있는지 모르는 상태다. 그쪽이 훨씬 위험하다.
    return MissionState::Fault;
}

QString missionStateLabel(MissionState state)
{
    switch (state) {
    case MissionState::Idle:             return QStringLiteral("대기");
    case MissionState::Ready:            return QStringLiteral("시작 준비");
    case MissionState::Running:          return QStringLiteral("점검 중");
    case MissionState::Pausing:          return QStringLiteral("정지 확인 중");
    case MissionState::Paused:           return QStringLiteral("일시정지");
    case MissionState::Recovering:       return QStringLiteral("재개 확인 중");
    case MissionState::Returning:        return QStringLiteral("복귀 중");
    case MissionState::Completed:        return QStringLiteral("완료");
    case MissionState::Failed:           return QStringLiteral("실패");
    case MissionState::Fault:            return QStringLiteral("오류");
    case MissionState::EmergencyStopped: return QStringLiteral("비상정지");
    }
    return QStringLiteral("알 수 없음");
}

}  // namespace hmi::robot
