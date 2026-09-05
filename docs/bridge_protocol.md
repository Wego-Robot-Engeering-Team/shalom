# SHALOM 관제 브릿지 통신 프로토콜 명세 v1

> 본 문서는 납품 성과물 "ROS2 인터페이스 명세서"의 관제 연동 구간에 해당한다.
> 로봇측 브릿지 노드(`shalom_bridge`, Ubuntu 22.04 / ROS2 Humble)와
> 관제 UI(`shalom_gcs`, Windows/Ubuntu, Qt) 사이의 유일한 통신 규약이다.

## 0. 설계 원칙

1. **관제 PC에 ROS2/DDS를 설치하지 않는다.** DDS 영역은 로봇측 Ubuntu에서 끝나고,
   관제 PC와는 raw TCP 단일 연결로만 붙는다.
   - 근거 1: Humble의 Windows 실사용 지원이 취약하고 Nav2/MoveIt2 Windows 바이너리가 없다.
   - 근거 2: DDS 멀티캐스트 디스커버리는 무선 링크에서 참가자 이탈/재참여를 반복한다.
   - 근거 3: 과업지시서 3장 "통신 차단 검증(패킷 캡처)" — 포트가 적으면 증명이 간단하다.
2. **안전 기능은 이 프로토콜에 의존하지 않는다.** E-Stop과 통신두절 정지는
   로봇측 safety 노드가 자체 판단한다. 관제 UI는 요청자일 뿐 판정자가 아니다.
3. **대용량 데이터는 제어 소켓에 싣지 않는다.** §8 에 경로를 분리해 정의한다.
   촬영 원본은 관제 PC 를 경유하지 않고 로봇에서 NAS 로 직접 올라간다.

## 1. 전송 계층

### 1.0 전송 방식 결정 (raw TCP)

WebSocket 이 아니라 **길이 접두사 프레이밍을 얹은 raw TCP** 를 쓴다.

| 근거 | 내용 |
|---|---|
| 웹 클라이언트 없음 | 브라우저 호환은 이 시스템에서 가치가 0이다. HTTP Upgrade·프레임 마스킹은 순수 부담 |
| **TCP_NODELAY 통제** | `QAbstractSocket::LowDelayOption` 을 명시적으로 켠다. Nagle 이 소형 제어 메시지(E-Stop, cmd_vel)를 뭉치면 수십 ms 지연이 붙는다. 제어 앱에서 대역폭보다 중요 |
| 의존성 0 | `QTcpSocket` 은 Qt 인박스. 임치 의존성 목록이 늘지 않는다 |
| 중복 제거 | WebSocket 의 Ping/Pong 과 텍스트/바이너리 구분은 우리 봉투(`hb`, `t`)와 기능이 겹친다 |

포기하는 것: `foxglove_bridge` / `rosbridge` 재사용 가능성(둘 다 WebSocket 전용).
브릿지를 자체 구현하기로 했으므로 손실이 없다.

**대신 프레이밍은 아래 규격을 정확히 지킨다.** 이 시스템에서 새로 지는 유일한
전송 계층 리스크가 여기이므로, 구현자가 임의로 바꾸지 말 것.

| 항목 | 값 |
|---|---|
| 프로토콜 | TCP |
| 기본 포트 | `9090` (제어/상태) |
| 카메라 라이브뷰 | `8080` HTTP MJPEG (본 명세 범위 밖) |
| 소켓 옵션 | **TCP_NODELAY 필수 (양측)**, SO_KEEPALIVE 권장 |
| 바이트 순서 | 리틀 엔디언 |
| 인코딩 | 좌표 SI 단위 (m, rad), 시각 Unix epoch 초(float) |

### 1.1 프레임 구조

```
 0        4                 8                     12
 +--------+-----------------+---------------------+------------------+
 | magic  | body_len uint32 | header_len uint32   | header (JSON)    | payload |
 | uint32 |                 |                     |                  |         |
 +--------+-----------------+---------------------+------------------+---------+
          |<----------------- body_len 바이트 ----------------------->|
```

| 필드 | 크기 | 설명 |
|---|---|---|
| `magic` | 4 B | `0x4D4C4853` (`"SHLM"` LE). 오접속·역동기화 검출용 |
| `body_len` | 4 B | 이후 본문 전체 길이 = 4 + header_len + payload_len |
| `header_len` | 4 B | JSON 봉투(UTF-8) 바이트 수 |
| `header` | 가변 | §1.2 봉투 |
| `payload` | 가변 | 없으면 0 바이트 |

고정 접두사 8 바이트(`magic` + `body_len`)만 읽으면 나머지 길이를 알 수 있다.
**본문 형식은 텍스트/바이너리 구분 없이 하나다** — 페이로드가 없으면 길이가 0일 뿐이다.

### 1.2 JSON 봉투

```json
{
  "v":   1,
  "t":   "pub",
  "ch":  "state/pose",
  "ts":  1789123456.789,
  "seq": 1042,
  "id":  "c3f1...",
  "p":   { }
}
```

| 필드 | 타입 | 필수 | 설명 |
|---|---|---|---|
| `v` | int | ✅ | 프로토콜 버전. 현재 `1`. 불일치 시 연결 거부 |
| `t` | str | ✅ | 메시지 종류. §1.4 |
| `ch` | str | `hb` 제외 ✅ | 채널명 |
| `ts` | float | ✅ | 송신자 시계 기준 Unix epoch 초 |
| `seq` | int | ⬜ | 스트림 채널의 단조 증가 시퀀스. 유실 검출용 |
| `id` | str | `req`/`res` ✅ | 요청-응답 상관 ID |
| `p` | object | ⬜ | 페이로드 |

> 직렬화는 v1 에서 JSON 이다. 인터페이스가 안정된 뒤 protobuf 로 굳히는 것을
> 전제로 봉투에 `v` 를 두었다. 전환 시 `v` 를 올리고 `header` 를 protobuf
> 바이트로 바꾼다 — 프레이밍은 그대로 쓸 수 있다.

### 1.3 수신 구현 요구사항 (필수)

TCP 는 스트림이라 메시지 경계가 없다. 아래를 **전부** 처리해야 한다.
여기를 대충 짜면 현장에서 몇 시간에 한 번 프레임이 어긋나는, 재현 안 되는
고장이 난다.

1. **부분 수신** — 8 바이트 접두사조차 여러 번에 나뉘어 도착할 수 있다.
   누적 버퍼에 쌓고, 완결된 프레임이 만들어질 때만 상위로 올린다.
2. **병합 수신** — 한 번의 read 에 여러 프레임이 들어온다. 버퍼가 마를 때까지
   루프를 돌며 꺼낸다. `readyRead` 한 번에 하나만 처리하면 지연이 누적된다.
3. **magic 불일치** → **즉시 연결 종료.** 재동기화를 시도하지 말 것 —
   스트림 정합이 깨진 시점에서 이미 신뢰할 수 없다.
4. **`body_len` 상한 가드** — `MAX_FRAME = 32 MiB`. 초과 시 연결 종료.
   가드 없이 길이를 믿고 할당하면 버그 하나로 메모리가 터진다.
5. **송신 백프레셔** — `bytesToWrite()` 가 임계(예: 4 MiB)를 넘으면
   손실 허용 채널(`pub`)의 발행을 건너뛴다. 무한 버퍼링 금지.
6. **재연결 시 수신 버퍼 완전 초기화** — 이전 연결의 잔여 바이트가 남으면
   첫 프레임부터 어긋난다.
7. **half-open 감지는 TCP 가 못 한다** — 상대가 FIN 없이 죽으면 소켓은
   살아 있는 것처럼 보인다. §5 앱 레벨 하트비트가 유일한 검출 수단이다.

### 1.4 메시지 종류 (`t`)

| `t` | 방향 | 응답 | 설명 |
|---|---|---|---|
| `hb` | 양방향 | 없음 | 하트비트. 5 Hz. §5 |
| `sub` | GCS → 브릿지 | `res` | 채널 구독. `p.channels: [str]` |
| `unsub` | GCS → 브릿지 | `res` | 구독 해제 |
| `pub` | 양방향 | 없음 | 스트림 발행. 손실 허용 |
| `req` | GCS → 브릿지 | `res` | 명령 요청. `id` 필수 |
| `res` | 브릿지 → GCS | — | 요청 응답. `p.ok: bool`, 실패 시 `p.err: {code,msg}` |
| `evt` | 브릿지 → GCS | 없음 | 비동기 이벤트(로그, 경고). 구독 무관 항상 수신 |

`res` 실패 예:
```json
{"v":1,"t":"res","ch":"cmd/goto","id":"c3f1","ts":1789123456.8,
 "p":{"ok":false,"err":{"code":"E_ESTOP_ENGAGED","msg":"E-Stop 발동 상태"}}}
```

## 2. 상태 채널 (브릿지 → GCS, `pub`)

과업지시서 2.2.7 [5] "갱신 3초 이내" 요구를 모든 채널이 충족한다.

| 채널 | 주기 | 페이로드 |
|---|---|---|
| `state/pose` | 10 Hz | `x, y, theta, frame, cov_xy, cov_theta` |
| `state/battery` | 1 Hz | `soc, voltage, current, temp_c, charging` |
| `state/system` | 1 Hz | `cpu_pct, mem_pct, cpu_temp_c, gpu_temp_c, net_rtt_ms, net_rssi` |
| `state/safety` | 변화 시 + 1 Hz | `estop, estop_source, mode, heartbeat_ok, reason` |
| `state/nav` | 5 Hz | `status, goal, distance_remaining_m, eta_s, current_waypoint_id` |
| `state/plan` | 변화 시 | `points: [[x,y], ...]` — Nav2 계획 경로 |
| `state/trail` | 2 Hz | `points: [[x,y], ...]`, `reset: bool` — 실제 주행 궤적(증분) |
| `state/arm` | 10 Hz | §2.1 |
| `state/apriltag` | 검출 시 | `tags: [{id, x, y, z, qx,qy,qz,qw, conf}], last_seen_ts` |
| `state/mission` | 변화 시 | `state, car_no, point_id, index, total` |
| `state/waypoints` | 변화 시 | `points: [{id,name,x,y,theta,tag_id,status}]` — 점검 시퀀스 |
| `state/locations` | 변화 시 | `locations: [{id,kind,name,x,y,theta,tag_id}]` — 충전소·시작점 등 고정 위치 |
| `evt/log` | 이벤트 | `level, code, msg, detail` |
| `map/occupancy` | 변화 시 | **바이너리**. §2.2 |
| `capture/preview` | 촬영 시 | **바이너리**. §2.3 |
| `state/capture_spool` | 변화 시 + 5 s | `pending, uploading, uploaded, failed, bytes_pending, spool_free_mb, nas_online` |
| `state/health` | 1 Hz | 센서·링크 건강 상태. §9 |

### 2.1 `state/arm`

```json
{
  "names":     ["fr3_joint1", "...", "fr3_joint7"],
  "positions": [0.0, -0.78, 0.0, -2.36, 0.0, 1.57, 0.78],
  "velocities":[0.0, ...],
  "efforts":   [0.0, ...],
  "ee_pose":   {"x":0.4,"y":0.0,"z":0.5,"roll":3.14,"pitch":0.0,"yaw":0.0,"frame":"fr3_link0"},
  "manipulability": 0.0731,
  "sigma_min":      0.0412,
  "singular_warn":  false,
  "limits_ok":      true,
  "moveit_state":   "idle"
}
```

- `manipulability` = Yoshikawa 조작성 지수 `w = sqrt(det(J·Jᵀ))`
- `sigma_min` = 야코비안 최소 특이값
- **두 값은 브릿지가 계산해서 내려준다.** 관제 UI는 게이지로 표시만 한다.
  FR3 특이자세 회피 판정은 로봇측 암 노드 책임이다(§4 참조).

### 2.2 `map/occupancy` (바이너리)

header `p`:
```json
{"width":2048,"height":1536,"resolution":0.05,
 "origin":{"x":-51.2,"y":-38.4,"theta":0.0},
 "encoding":"png","map_id":"gtxa_pit_v3"}
```
payload: PNG 바이트. **행 0 = 이미지 상단**(ROS OccupancyGrid와 상하 반전된 상태로 인코딩).
`origin`은 셀 (0,0) = 격자 좌하단 모서리의 map 프레임 상 좌표.

> 제약: `origin.theta != 0` 인 맵은 v1에서 지원하지 않는다. 브릿지가 회전을 흡수해서 발행할 것.

### 2.3 `capture/preview` (바이너리)

header `p`:
```json
{"point_id":"C03-P07","kind":"2d","format":"jpeg",
 "metadata":{"car_no":"1234-05","tag_id":17,"captured_at":"2026-09-05T14:03:21",
             "robot_pose":{"x":3.21,"y":-1.04,"theta":1.57},"distance_mm":620}}
```

## 3. 명령 채널 (GCS → 브릿지, `req`/`res`)

| 채널 | 페이로드 | 비고 |
|---|---|---|
| `cmd/estop` | `{}` | **발동 전용.** 항상 성공해야 한다 |
| `cmd/estop_release` | `{"confirm": true}` | 수동 해제만. 자동 해제 금지 |
| `cmd/mode` | `{"mode":"auto"\|"manual"}` | 수동 전환 시 자율주행 즉시 중단 |
| `cmd/goto` | `{"x":..,"y":..,"theta":..}` | 지도 클릭 목표점 |
| `cmd/nav_cancel` | `{}` | |
| `cmd/waypoints/set` | `{"points":[...]}` | 전체 치환. 배열 순서 = 순회 순서 |
| `cmd/locations/set` | `{"locations":[...]}` | 고정 위치 전체 치환. §8 |
| `cmd/mission/start` | `{"from_index":0}` | |
| `cmd/mission/pause` | `{}` | |
| `cmd/mission/resume` | `{}` | 명시적 재개만. 자동 재개 금지 |
| `cmd/mission/stop` | `{}` | |
| `cmd/arm/preset` | `{"name":"home"\|"standby"\|"stow"}` | |
| `cmd/arm/joint_goal` | `{"positions":[7]}` | 관절 슬라이더 |
| `cmd/arm/ee_goal` | `{"x","y","z","roll","pitch","yaw","frame"}` | MoveIt2 실행 |
| `cmd/arm/stop` | `{}` | |
| `cmd/capture/trigger` | `{"metadata":{...}}` | `/inspection/trigger` 발행 |

### 3.1 고빈도 스트림 (GCS → 브릿지, `pub`, 응답 없음)

| 채널 | 주기 | 페이로드 |
|---|---|---|
| `cmd/cmd_vel` | 20 Hz | `{"vx":0.3,"vy":0.0,"wz":0.0}` |

**데드맨 래치**: 브릿지는 `cmd/cmd_vel`을 300 ms 이상 수신하지 못하면
즉시 0을 발행한다. 관제 UI가 멈추거나 링크가 끊겨도 로봇이 계속 달리지 않는다.

## 4. 안전 규정 (프로토콜 밖의 책임 분담)

과업지시서 2.2.5절 대응. **아래는 UI가 아니라 로봇측 safety 노드가 보장한다.**

| 요구사항 | 구현 위치 | 프로토콜 역할 |
|---|---|---|
| E-Stop 1초 이내 정지 | 로봇측 safety 노드 (SDK2 E-Stop API + `/cmd_vel` 차단) | `cmd/estop` 요청 전달만 |
| 통신 두절 3초 → 정지 | safety 노드의 하트비트 감시 (§5) | 하트비트 제공 |
| 수동 명령 우선권 | safety 노드의 모드 중재 | `cmd/mode` 전달 |
| 사람 접근 감지 정지 | 로봇측 인지 노드 | `evt/log` 로 통보 |
| FR3 관절/속도/저크 한계 | 로봇측 암 노드 + MoveIt2 | UI는 요청, 노드가 거부 |
| FR3 특이자세 회피 | 로봇측 암 노드 (널스페이스 활용) | `state/arm` 게이지 표시만 |

> **UI의 빨간 E-Stop 버튼은 보조 수단이다.** 주 수단은 물리/무선 하드웨어 E-Stop이어야
> 한다. 브라우저도 아니고 데스크톱 앱이라 해도, 소프트웨어 경로를 1차 안전장치로
> 제출하면 심사에서 걸린다.

## 5. 하트비트 · 연결 수명

```json
{"v":1,"t":"hb","ts":1789123456.789,"p":{"seq":8412}}
```

| 항목 | 값 |
|---|---|
| 송신 주기 | 양방향 5 Hz (200 ms) |
| GCS 하트비트 소실 판정 | 브릿지가 600 ms 미수신 → `heartbeat_ok:false` |
| 정지 발동 | safety 노드가 **3초** 미수신 시 현 위치 정지 + 경고음 (지시서 요구) |
| 브릿지 하트비트 소실 | GCS가 1.5초 미수신 → 연결 끊김 표시, 재연결 시도 |
| 재연결 | 지수 백오프 0.5s → 5s 상한 |
| 재연결 후 | **자율주행 자동 재개 금지.** 명시적 `cmd/mission/resume` 필요 |
| RTT 측정 | `hb.p.seq` 왕복으로 산출 → `state/system.net_rtt_ms` |

## 6. 영상·촬영 데이터 경로 (제어 소켓 밖)

경로가 셋이고 목적이 다르다. 하나로 합치려 하면 셋 다 나빠진다.

| 경로 | 내용 | 전송 | 목적지 |
|---|---|---|---|
| 라이브뷰 | 실시간 카메라 영상 | HTTP MJPEG `:8080` | 관제 UI, AI 분석 PC |
| 미리보기 | 촬영 직후 확인용 썸네일 (수백 KB) | 제어 소켓 `capture/preview` | 관제 UI |
| **촬영 원본** | 2D 고해상도 · 3D 포인트클라우드 (점검 증거물) | **로봇 → NAS 직접** | NAS |

### 6.1 촬영 원본은 관제 PC 를 경유하지 않는다

근거:
- 관제를 거치면 무선 구간을 두 번 탄다(로봇→관제→NAS). 대역폭이 반이 된다.
- 관제 PC 가 꺼져 있어도 촬영 데이터는 보존되어야 한다.
- 1량 12 포인트 × 3량이면 3D 포함 수 GB 규모다. 관제 UI 가 중계할 이유가 없다.

과업지시서 2.2.7 [13] "내부망 한정, NAS 직접 연동" 이 이 구조를 뜻한다.
관제 UI 의 이력 조회·다운로드도 로봇이 아니라 **NAS 를 직접** 본다.

### 6.2 전송 방식

**채택: SMB/CIFS 마운트.** NAS 대부분이 기본 지원하고, 로봇측 업로더 코드가
파일 I/O 로 끝나 별도 전송 라이브러리 의존성이 생기지 않는다(임치 의존성 목록에
아무것도 추가되지 않는다).

**FTP 는 쓰지 않는다.** 평문 인증이고, PASV 포트 레인지 때문에 방화벽 규칙이
넓어져 "포트 최소화" 원칙과 3장 패킷 캡처 검증을 모두 어렵게 만든다.
재개 동작이 서버 구현마다 다르고 무결성 검증 수단도 없다.

#### 마운트 옵션 (필수 사항)

```
//nas.internal/inspection  /mnt/nas  cifs
    credentials=/etc/shalom/nas.cred,   # 자격증명을 fstab 에 평문으로 두지 않는다
    soft,                               # ★ 아래 설명 참조
    vers=3.1.1,                         # SMB1 비활성 (보안 감사 지적 항목)
    noserverino,                        # inode 충돌 회피
    uid=shalom,gid=shalom,file_mode=0644,dir_mode=0755,
    _netdev,nofail                      # 부팅 시 NAS 부재로 기동 실패하지 않게
```

⚠️ **`soft` 마운트가 핵심이다.** 기본값인 `hard` 로 걸면 무선이 끊겼을 때 쓰기
호출이 무한 대기하고, 그 프로세스는 `SIGKILL` 로도 죽지 않는(uninterruptible)
상태가 된다. 열차 하부는 무선 품질이 나쁜 구간이라 반드시 발생한다.
`soft` 는 타임아웃 후 오류를 돌려주므로 업로더가 재시도 로직으로 처리할 수 있다.

`soft` 마운트는 부분 쓰기 가능성이 있으므로 **§6.3 의 체크섬 검증이 필수 전제**다.
검증 없이 로컬 파일을 지우면 잘린 파일이 증거물로 남는다.

#### 자격증명 취급

- `credentials` 파일은 `0600`, 소유자 root. `fstab` 에 평문으로 쓰지 않는다.
- 계정은 해당 공유에만 쓰기 권한을 가진 전용 계정을 쓴다.
- 자격증명 파일은 임치 대상에서 제외하고, 설정 항목만 설계서에 기재한다.

### 6.3 로컬 스풀 (필수)

```
촬영 → 로봇 로컬 디스크(스풀) → 업로더 → NAS(SMB) → 체크섬 검증 → 로컬 삭제
```

업로더는 NAS 마운트에 직접 쓰지 않고 **임시 파일명으로 쓴 뒤 rename** 한다.
전송 중 끊기면 부분 파일이 최종 파일명으로 남아, 나중에 정상 파일과 구분되지
않는다. rename 은 같은 공유 안에서 원자적이다.

촬영과 업로드를 분리하는 이유:

- 열차 하부는 무선 품질이 나쁘다. **링크가 끊겨도 촬영은 계속되어야 한다.**
- NAS 장애 시 데이터가 유실되지 않는다. 코드 `NAS_WRITE_FAIL` 의 조치 문구
  ("데이터는 로봇 로컬에 임시 보관됩니다")가 성립하려면 스풀이 전제다.
- **체크섬 검증 후에만 로컬을 지운다.** 점검 증거물의 무결성 요건이다.

스풀 용량이 임계에 도달하면 `SPOOL_FULL` 을 발행하고 촬영을 중단한다 —
덮어쓰기 금지. 촬영 데이터는 재취득에 로봇 재투입이 필요한 자산이다.

### 6.4 관제가 보아야 하는 것

조작자가 "전부 올라갔는가"를 확인할 수 없으면 점검 완료 여부를 판단할 수 없다.
`state/capture_spool` 채널이 이를 제공한다 (§2 표 참조).

## 7. 버전 관리

- 봉투 `v`가 다르면 브릿지는 `res {ok:false, err:{code:"E_VERSION"}}` 후 연결을 닫는다.
- 채널 추가는 마이너 변경(버전 유지). 기존 채널의 필드 삭제/의미 변경은 `v` 증가.
- 미지원 채널 수신 시 양측 모두 **조용히 무시**한다(전방 호환).

## 8. 위치 등록 (Teach)

점검포인트와 충전 스테이션 같은 위치는 두 방법으로 등록한다.

| 방법 | 용도 | 정확도 |
|---|---|---|
| **현재 위치로 등록** | 로봇을 실제로 그 자리에 세운 뒤 저장 | 높음. 도달 가능성이 이미 검증된 자세다 |
| **지도 클릭** | 대략적인 배치, 사전 계획 | 낮음. 도달 가능 여부를 알 수 없다 |

촬영 포인트는 로봇팔이 대상에 닿는지까지 확인한 뒤 **현재 위치로 등록**하는 것을
기본 절차로 한다. 지도 클릭은 초안을 잡을 때 쓰고, 현장에서 재교시한다.

### 8.1 위치 종류 (`kind`)

| kind | 설명 | 개수 |
|---|---|---|
| `inspection` | 점검포인트. `state/waypoints` 시퀀스를 구성한다 | 다수, 순서 있음 |
| `dock` | 충전 스테이션. 점검 완료·배터리 부족 시 복귀 목적지 | 1 |
| `home` | 시작 위치. 기동 시 자기 위치를 확정하는 기준점 (시나리오 1단계) | 1 |

`dock` 과 `home` 은 순회 시퀀스에 들어가지 않으므로 `state/locations` 로 분리한다.
같은 채널에 섞으면 시퀀스 편집이 충전소를 건드릴 위험이 생긴다.

### 8.2 등록 시 검증 (필수)

현재 위치를 저장하기 전에 **관제 UI가 아래를 확인하고, 실패 시 사유를 보여준다.**
잘못 저장된 위치는 시운전 때까지 드러나지 않고, 그때는 이미 로봇을 재투입해야 한다.

| 검사 | 실패 시 | 이유 |
|---|---|---|
| 로봇 정지 상태 | **차단** | 이동 중 좌표는 뭉개진다. 0.2 m/s 로 움직이면 갱신 주기(10 Hz) 사이에 2 cm 가 어긋난다 |
| 위치 신선도 | **차단** | 링크가 끊긴 동안의 마지막 좌표를 저장하면 실제와 무관한 값이 남는다 |
| 위치 추정 신뢰도 | 경고 후 진행 가능 | `LOCALIZATION_DEGRADED` 상태의 좌표는 오차가 크다 |
| Apriltag 인식 (inspection 만) | 경고 후 진행 가능 | 포인트-마커 연결이 비면 현장에서 2차 보정을 못 한다 |

검증 결과는 `LOC_CAPTURED` / `LOC_CAPTURE_BLOCKED` / `LOC_CAPTURE_DEGRADED`
코드로 이벤트 로그에 남긴다. 저장된 위치의 출처(`captured_from`: `robot` 또는 `map`)와
당시 신뢰도를 함께 기록해, 나중에 "이 포인트는 어떻게 잡았나"를 추적할 수 있게 한다.

### 8.3 재교시 (re-teach)

기존 위치를 현재 로봇 자세로 덮어쓰는 동작을 별도로 제공한다. 현장에서 포인트가
조금씩 어긋나는 것은 정상이며, 매번 삭제 후 재등록하면 순서와 ID 가 흐트러진다.
재교시는 `id` 를 유지한 채 좌표만 바꾼다.

### 8.4 영속화

위치 정보는 **로봇측이 보관한다.** 관제 PC 가 교체되거나 꺼져도 남아야 하고,
로봇이 단독으로 복귀 동작을 수행할 수 있어야 하기 때문이다.
관제 UI 는 `cmd/locations/set` · `cmd/waypoints/set` 으로 전체를 치환하고,
브릿지가 파일로 저장한 뒤 `state/*` 로 되돌려 발행한다.

## 9. 센서·링크 건강 상태

### 9.1 `state/health` 페이로드

```json
{
  "sensors": [
    {"id":"lidar",     "name":"LiDAR",      "expected_hz":10, "actual_hz":9.8,
     "last_seen_ms":102, "state":"ok",   "detail":"38.4k pts"},
    {"id":"imu",       "name":"IMU",        "expected_hz":200,"actual_hz":199.4,
     "last_seen_ms":5,   "state":"ok"},
    {"id":"aurora",    "name":"Aurora S",   "expected_hz":20, "actual_hz":0,
     "last_seen_ms":8400,"state":"lost", "detail":"USB 재연결 필요"},
    {"id":"cam_body",  "name":"본체 카메라", "expected_hz":15, "actual_hz":14.9, "state":"ok"},
    {"id":"cam_arm_2d","name":"암 2D",      "expected_hz":15, "actual_hz":7.2,
     "state":"degraded", "detail":"프레임 드롭"},
    {"id":"cam_arm_3d","name":"암 3D",      "expected_hz":10, "actual_hz":10.0, "state":"ok"},
    {"id":"joints_b2", "name":"B2 관절",     "expected_hz":50, "actual_hz":50.0, "state":"ok"},
    {"id":"joints_fr3","name":"FR3 관절",    "expected_hz":100,"actual_hz":100.0,"state":"ok"}
  ],
  "link": {
    "rtt_ms": 24, "rssi_dbm": -58,
    "rx_bytes_per_s": 41200, "tx_bytes_per_s": 1800,
    "seq_gaps": 3, "decode_errors": 0, "reconnects": 1
  }
}
```

`state` 는 `ok` / `degraded` / `lost` / `fault` 중 하나다.

### 9.2 끊김 판정은 기대 주기 대비로 한다

**고정 타임아웃을 쓰지 않는다.** 10 Hz LiDAR 가 1 초간 조용하면 고장이지만,
1 Hz 배터리 보고가 1 초 조용한 것은 정상이다. 하나의 임계값으로는 둘 다
제대로 판정할 수 없다.

| 조건 | 판정 |
|---|---|
| `last_seen_ms` > 기대 주기 × 5 | `lost` |
| `last_seen_ms` > 기대 주기 × 3 | `degraded` |
| `actual_hz` < `expected_hz` × 0.6 | `degraded` |
| 센서가 오류 플래그를 올림 | `fault` |

판정은 브릿지가 수행해 `state` 로 내려준다. 관제 UI 는 표시만 한다 —
같은 판정 로직을 양쪽에 두면 반드시 어긋난다.

### 9.3 링크 지표

프레이밍을 자체 구현했으므로(§1.1) 아래 두 값이 특히 중요하다.

- `decode_errors` — 0 이 아니면 프레이밍이나 중간 장비에 문제가 있다.
  누적값이며, 증가하면 즉시 운용을 멈추고 로그를 내보낸다.
- `seq_gaps` — `pub` 스트림의 `seq` 불연속 횟수. 손실 허용 채널이므로
  약간은 정상이지만, 급증은 링크 포화나 브릿지 과부하를 뜻한다.

`rx_bytes_per_s` 는 무선 구간 여유를 판단하는 데 쓴다. 촬영 원본은 이 경로를
타지 않으므로(§6.1) 여기서 큰 값이 나오면 설정이 잘못된 것이다.

## 10. 오류 코드

| 코드 | 의미 |
|---|---|
| `E_VERSION` | 프로토콜 버전 불일치 |
| `E_UNKNOWN_CHANNEL` | 미지원 채널 |
| `E_BAD_PAYLOAD` | 페이로드 스키마 위반 |
| `E_ESTOP_ENGAGED` | E-Stop 발동 중이라 거부 |
| `E_MODE` | 현재 모드에서 허용되지 않는 명령 |
| `E_BUSY` | 선행 동작 진행 중 |
| `E_UNREACHABLE` | IK 해 없음 / 계획 실패 |
| `E_LIMIT` | 관절·속도·작업공간 한계 초과 |
| `E_HARDWARE` | 하드웨어 응답 없음 |
