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

현재 예시 `depot-a`, `depot-b`는 파일 형식과 지도별 상태의 경계를 보이기 위한
샘플이다. 실행할 때는 `map:=depot-a`처럼 지도 ID를 지정할 수 있다. 기존의
`<이름>.yaml` 평면 지도와 `map:=latest`도 호환을 위해 유지한다.

향후 브릿지는 이 디렉터리를 목록으로 제공하고, 선택된 지도에 맞춰
`map/occupancy`, 웨이포인트, 고정 위치, 마커를 함께 내보낸다.
