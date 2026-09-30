# 지도 저장 구조

지도는 로봇이 소유한다. 지도 하나는 아래처럼 디렉터리 하나에 보관한다.

```text
<map_id>/
├── map.yaml          # Nav2 map_server 메타데이터
├── map.pgm           # 점유 격자 이미지
├── metadata.json     # 지도 이름·생성 이력·출처·설명
├── waypoints.json    # 이 지도 전용 점검 지점
├── locations.json    # 이 지도 전용 충전·시작 위치
├── markers.json      # 이 지도 전용 AprilTag 측량값
└── missions.json     # 이 지도 전용 미션
```

폴더 이름이 지도 ID이자 HMI에 표시되는 이름이다. 시뮬레이터는 기본적으로
`default_map.json`이 가리키는 지도로 시작한다.
지도별 `waypoints.json`·`markers.json`·`locations.json`·`missions.json`에는
`map_id`를 반복 저장하지 않는다. `default_map.json`에만 선택할 지도 ID를 기록한다.

`metadata.json`의 `name`은 폴더와 함께 파일을 복사해도 지도 이름을 알 수 있게
두며, 폴더 이름과 같은 값으로 유지한다. HMI는 폴더 이름을 기준으로 표시한다.
`map.yaml` 경로와 별도의 `id`는 파일 위치로 알 수 있어 기록하지 않는다.
두 지도는 같은 `map.yaml`·`map.pgm`을 가지고 있다. 생성 시각은 확인되지 않아
`created_at: null`로 두었다. 이전 파일의 `2026-09-07` 값은 원본에서 복사된
날짜로, 이 지도들의 실제 생성 시각으로 확인되지 않았다.

| 필드 | 의미 |
|---|---|
| `name` | 지도 이름. 폴더명과 동일하게 유지 |
| `created_at`, `developer` | 생성 시각(ISO 8601, 미기록이면 `null`), 작성자 |
| `place`, `project` | 지도 제작 환경·장소와 프로젝트 이름 |
| `mapping.lidar_sensor`, `mapping.slam_software` | 실제 지도 생성에 사용한 LiDAR 모델과 SLAM 프로그램 |
| `mapping.dimension` | 2D 또는 3D 지도 |
| `description` | 운영자가 참고할 설명 |

`source` 항목은 별도 원본 bag·데이터셋을 추적할 때만 추가한다. 현재 지도의
MuJoCo 정보는 `place`에 있으므로 중복 기록하지 않는다.
현재 HMI 지도 목록에서 읽는 메타데이터는 `created_at`뿐이며,
나머지는 생성 이력 보존용이다. 향후 지도 저장 기능이 생기면 저장 시점의 센서·SLAM
설정과 작성자도 함께 기록해야 한다.

```bash
ros2 launch simulation_bringup bringup.launch.py \
  map:=/home/juno/shalom_ws/src/shalom/simulation/simulation_bringup/maps/gtxa_260928_113708/map.yaml
```

HMI에서 이름을 바꾸면 지도 폴더와 `metadata.json`의 이름이 바뀐다.
기본 지도였다면 `default_map.json`도 갱신된다. `--symlink-install` 개발 환경에서는
설치 경로의 복사본이 아닌 소스 `maps` 폴더를 읽고 쓴다. 지도 없이 실시간 SLAM으로
시작하려면 `map:=none`을 지정한다. `map`에 절대 경로를 명시했다면 해당 지도의
폴더 이름을 바꾼 뒤 다음 실행 명령의 경로도 바꿔야 한다.
HMI 지도 목록의 ‘기본 지정’을 누르면 `default_map.json`이 갱신되며, 기본
지도를 해제하면 다음 기동은 SLAM 모드가 된다.

`simulation_bringup`은 이 디렉터리를 `map_server`와 HMI 브릿지에 같은 값으로
전달한다. HMI의 지도 메뉴에서 선택하면 `map/occupancy`, 웨이포인트, 고정 위치,
마커가 함께 전환된다.
