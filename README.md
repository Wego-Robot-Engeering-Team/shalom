# 철도차량 하부점검 시스템

GTX-A 차량 하부를 자율주행 로봇(Unitree B2 + Franka FR3)으로 점검하는
시스템이다. 관제 소프트웨어와 로봇측 소프트웨어를 한 저장소에서 관리한다.

## 구성

**어디서 도는 코드인가**로 나눈다. 기능별로 나누면 같은 기능이 관제와 로봇에
걸칠 때 어디에 둘지가 매번 논쟁이 되고, 무엇을 납품하는지도 흐려진다.

```text
station/     관제 PC 에서 도는 것 — C++/Qt6 관제 UI
testbed/     로봇 없이 화면을 돌리기 위한 것 (납품 제외)
protocol/    관제와 로봇이 함께 쓰는 프레이밍 헤더
bridge/      로봇 위에서 도는 ROS2 ↔ 관제 브릿지
docs/        통신 규격 등 납품 문서
```

로봇측 코드가 들어오면 `robot/` 아래로 모은다 — 자율주행 파라미터,
안전 노드, 촬영·업로드 응용, 센서 캘리브레이션이 거기다. `bridge/` 도
그때 함께 옮긴다. 지금 미리 옮기지 않는 것은, 로봇측 작업이 다른 곳에서
진행 중이라 파일을 움직여 두면 합칠 때 삭제·수정 충돌이 나기 때문이다.

현재 `nav2_gseg/` 는 저장소 밖에서 작업 중이며 추적하지 않는다. 그 안의
패키지는 전부 외부 저장소(dfki-ric, PRBonn, ros-navigation) 클론이라,
파일로 복사해 넣으면 라이선스 고지와 갱신 경로가 끊기고 저장소가 수백 MB
늘어난다. 편입할 때는 `.repos` + `vcs import` 나 서브모듈을 쓴다.

## 왜 testbed 가 따로인가

시뮬레이터와 대역 지도는 관제 UI 가 아니다. 개발·검수 때 로봇 없이 화면을
띄우기 위한 것이고, 납품 빌드에는 들어가면 안 된다 — 들어가면 현장에서
로봇이 안 붙었을 때 조용히 가짜 데이터로 도는 화면이 만들어진다.

`release` 프리셋은 `GCS_WITH_TESTBED=OFF` 로 빌드하므로 납품 실행 파일에는
시뮬레이터가 없다. 로봇 주소 없이 켜면 뜨지 않고 그렇게 말한다.

## 시작하기

```bash
cmake --preset dev -S station
cmake --build --preset dev
./station/build/inspection_gcs
```

자세한 내용은 [station/README.md](station/README.md) 를 참조한다.
브릿지는 [robot/bridge/README.md](robot/bridge/README.md), 통신 규격은
[docs/bridge_protocol.md](docs/bridge_protocol.md) 에 있다.
