# SDK 변경 이력

SDK 버전은 라이브러리·공개 API 변경을, 봉투의 `v`는 통신 규격을 구분한다.

## 0.4.0 / protocol v1

- Linux·MacOS·Windows 각각의 `cpp/`·`python/`에 개선 사항 반영.
- 주행 정지·재개, 초기 위치, 선속도·각속도 제한과 범위 설정 추가.
- 미션 ID 실행·목록·저장·보관·충전소 복귀 추가.
- 지도 이름 변경·삭제·기본 지도 설정 추가.
- 팔 자세 목록·저장·수정·보관 추가.
- 웨이포인트·위치·마커 저장에 지도와 편집 전 스냅샷 추가.
- C++ 마커 전송을 `x/y/z/yaw`로 수정하고 이름·설명 보존.
- 촬영에 편성 번호 추가, 기본 임의 촬영 식별자 제거.
- C++ JSON 파싱·봉투 검증, 동기 요청·poll·상태 조회 추가.
- 연결 종료 초기화, 타임아웃·잘못된 응답·대기 요청 수 제한 보완.
- 미지원 수동 속도 편의 API·`Twist2D` 제거.
- 회귀 테스트·브리지 연동 테스트 추가.
- C++ ABI `SOVERSION 1`. 기존 연동 프로그램 재빌드 필요.

## 0.3.0 / protocol v1

`robot_sdk` namespace·Python package, `RobotSdk` CMake package와 `robot_sdk::sdk` target 정리.

## 0.2.0 / protocol v1

C++ RobotApi, Python Client, 읽기 전용 예제 추가.

## 0.1.0 / protocol v1

최초 TCP 프로토콜·외부 연동 문서.
