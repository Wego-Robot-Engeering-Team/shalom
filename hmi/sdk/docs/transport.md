# TCP 프로토콜

| 항목 | 값 |
| --- | --- |
| 프로토콜·포트 | TCP `9090` |
| 동시 클라이언트 | 1 |
| 소켓 옵션 | `TCP_NODELAY` |
| 프레임 최대 body | 32 MiB |
| 하트비트 | 200 ms 간격 |
| 스레드 모델 | 단일 스레드, 명시적 poll/run |
| 요청 대기 수 | 최대 128 |
| 기본 요청 타임아웃 | 3 s |

C++ 연결은 주소 해석 후 TCP 연결 시도를 시간 제한한다. DNS 조회 시간은 OS resolver 설정을 따른다.
C++ 송신은 시간 제한하며, Python socket 송신 대기는 최대 0.5 s다.
소켓·프로토콜 오류 시 연결을 종료하고 캐시와 대기 요청을 제거한다.

## 프레임

```text
magic[4] | body_len:uint32 | header_len:uint32 | JSON header | binary payload
```

모든 정수는 little-endian이다. `magic`은 ASCII `SHLM`이며
`body_len = 4 + header_bytes + payload_bytes`다. header는 UTF-8 JSON이다.

SDK는 부분 프레임과 여러 프레임이 합쳐진 TCP 수신을 처리한다.
magic·길이·JSON 봉투가 잘못되면 연결을 종료한다.
C++ 내부 구현은 `<OS>/cpp/src/transport/`, Python은 `<OS>/python/robot_sdk/framing.py`·`client.py`다.

## 봉투

```json
{"v":1,"t":"req","ch":"cmd/maps/list","id":"c1","ts":1791302400.0,"robot":"SE-0001","p":{}}
```

| 필드 | 타입 | 의미 |
| --- | --- | --- |
| `v` | int | 프로토콜 버전 1 |
| `t` | string | `hb`·`pub`·`evt`·`req`·`res` |
| `ch` | string | 채널. hb에서 생략 |
| `id` | string | 요청·응답 상관 ID |
| `ts` | float64 | Unix epoch 초 |
| `robot` | string | 연결한 로봇 ID |
| `p` | object | JSON 데이터 |

첫 수신 로봇 ID를 연결에 고정하고 이후 송신에도 포함한다.
같은 연결에서 다른 ID가 오면 종료한다. 재연결 시 ID를 새로 확인한다.

## 하트비트와 수신

```json
{"v":1,"t":"hb","ts":1791302400.0,"p":{"seq":1}}
```

`connect` 직후 하트비트를 전송하고 `poll`·`run`·`request`에서 유지한다.
유효한 하트비트가 끊기면 로봇의 safety 계층이 통신 단절을 처리한다.
재연결 후 주행·미션 재개는 명시적으로 요청한다.

Python poll은 Message 배열을 반환한다. run과 request의 콜백은 그 메시지를 처리한다.
C++ run 콜백에서 false를 반환하면 연결을 종료한다.
Python run 콜백에서 종료하려면 `client.close()`를 호출한다.

## 요청·응답

- 요청 ID는 Client 수명 동안 재사용하지 않는다.
- 응답 ID와 채널을 함께 검증한다.
- 비동기 응답은 `take_response`·`takeResponse`로 한 번 소비한다.
- 타임아웃·완료·종료 시 해당 대기 기록을 제거한다.
- 늦은 응답과 요청하지 않은 응답은 메시지로 전달하지만 대기 캐시에 추가하지 않는다.
- 요청 타임아웃 후 실행 여부는 상태 채널에서 확인한다. SDK는 재전송하지 않는다.

## 배포 제약

TLS·사용자 인증은 없다. [보안](security.md)의 전용 제어망 기준을 따른다.
TCP 수동 속도 채널은 비활성이다. HMI의 UDP teleop는 별도 경로다.
상태는 연결 직후와 변경·주기 발행으로 수신하며 구독 명령 없이 모두 전달된다.
