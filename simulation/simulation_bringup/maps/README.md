# 지도 저장 구조

지도는 로봇이 소유한다. 지도 하나는 아래처럼 디렉터리 하나에 보관한다.

```text
<map_id>/
├── map.yaml          # Nav2 map_server 메타데이터
├── map.pgm           # 점유 격자 이미지
├── metadata.json     # HMI 목록에 보일 이름·생성 시각
├── waypoints.json    # 이 지도 전용 점검 지점
├── locations.json    # 이 지도 전용 충전·시작 위치
└── markers.json      # 이 지도 전용 AprilTag 측량값
```

지도 ID는 생성 날짜(`YYYY-MM-DD`)다. 실행할 때는 반드시 `map.yaml`의 절대 경로로
지정한다. 예를 들면 다음과 같다.

```bash
ros2 launch simulation_bringup bringup.launch.py \
  map:=/home/juno/shalom_ws/install/simulation_bringup/share/simulation_bringup/maps/2026-09-07/map.yaml
```

HMI에서 바꾸는 표시 이름은 로봇의 `metadata.json`에 저장된다. 표시 이름을 바꿔도
ID는 그대로이므로 폴더와 참조가 흔들리지 않는다. 지도 없이 실시간 SLAM으로
시작하려면 `map` 인자를 생략하거나 빈 값으로 지정한다.

`simulation_bringup`은 이 디렉터리를 `map_server`와 HMI 브릿지에 같은 값으로
전달한다. HMI의 지도 메뉴에서 선택하면 `map/occupancy`, 웨이포인트, 고정 위치,
마커가 함께 전환된다.
