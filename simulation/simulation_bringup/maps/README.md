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

지도 ID는 생성 날짜(`YYYY-MM-DD`)다. 실행할 때는 `map:=2026-09-07`처럼 ID 로
지정하고, HMI에서 바꾸는 표시 이름은 로봇의 `metadata.json`에 저장된다. 표시 이름을
바꿔도 ID 는 그대로이므로 폴더와 참조가 흔들리지 않는다.

`map:=latest`는 ID 를 이름순으로 정렬해 마지막을 고른다. 날짜 ID 에서는 그것이 곧
최신이다. 다만 시험용 지도를 섞어 두면 그쪽이 뒤로 정렬돼 잡히므로, 기본값은
`bringup.launch.py`에서 실제 지도 ID 로 명시한다. 기존의 `<이름>.yaml` 평면 지도도
호환을 위해 계속 읽는다.

`simulation_bringup`은 이 디렉터리를 `map_server`와 HMI 브릿지에 같은 값으로
전달한다. HMI의 지도 메뉴에서 선택하면 `map/occupancy`, 웨이포인트, 고정 위치,
마커가 함께 전환된다.
