# Bridge monitor — C++ sample

공개 TCP 통신 규약 v1을 이용하는 최소 C++ client다. ROS 2, Qt, JSON library 없이
공유 프레이밍 헤더 하나만 사용한다. 연결 후 5 Hz heartbeat를 보내고, 브릿지에서
수신한 JSON header와 binary payload 크기를 출력한다.

이 sample은 **상태 관찰 전용**이다. 주행, 미션, 팔, E-Stop 명령은 보내지 않는다.
그런 명령은 인증·권한·안전 UX·감사 로그를 갖춘 응용 프로그램에서만 구현해야 한다.

## Build

```bash
cmake -S cicd/samples/bridge_monitor_cpp -B build/bridge_monitor
cmake --build build/bridge_monitor
```

Linux/POSIX socket API를 쓰므로 관제 PC 또는 개발 환경에서 빌드한다.

## Run

```bash
./build/bridge_monitor/shalom_bridge_monitor --host 192.168.0.10 --port 9090
```

기본 연결 주소는 `127.0.0.1:9090`이다. 정상 연결이면 `hb`, `state/*`, `evt/log`
등 수신 프레임의 JSON header가 출력된다. 지도·촬영 미리보기처럼 binary payload가
있는 프레임은 본문을 저장하지 않고 크기만 출력한다.

## Protocol ownership

- 규약: [docs/bridge_protocol.md](../../../docs/bridge_protocol.md)
- 공유 framing 구현: [common/protocol/include/inspection/framing.hpp](../../../common/protocol/include/inspection/framing.hpp)
- 지원 protocol version: `v: 1`

서버는 한 번에 하나의 관제 연결만 허용한다. 운영 HMI가 연결되어 있다면 이
sample은 접속 직후 닫히는 것이 정상이다. 실기에서 기존 HMI 연결을 끊기 위해
사용하지 말고, 별도 시험 환경에서 실행한다.
