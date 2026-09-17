# Shalom Robot SDK

Shalom 로봇 브릿지(protocol `v:1`)를 외부 application에서 연동하는 고객 SDK다.
HMI와 같은 TCP `9090` API를 사용하며, 별도의 숨은 제어 경로는 없다.

현재 SDK 버전은 [`VERSION`](VERSION)의 `0.2.0`이다.

## 시작

사용 OS의 `cpp/` 또는 `python/`만 선택한다.

```bash
# Linux C++
cd <SDK_ROOT>/Linux
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/shalom_monitor <robot-host>

# Linux Python
cd <SDK_ROOT>/Linux/python
python3 -m pip install .
python3 examples/monitor.py <robot-host>
```

- C++: 각 OS의 [`cpp/BUILD.md`](Linux/cpp/BUILD.md)
- Python: 각 OS의 [`python/README.md`](Linux/python/README.md)
- Windows: Developer Command Prompt에서 `Windows\build.bat`

## 고객에게 전달하는 공개 경계

```text
<OS>/cpp/include/       C++17 public headers (declarations only)
<OS>/cpp/lib/           delivered shared library (.so/.dylib/.dll + import library)
<OS>/cpp/examples/      shared-library link example
<OS>/python/            Python 3.9+ package와 예제
docs/                   protocol 계약·보안·오류 기준
LICENSE, NOTICE         사용권과 고지
```

```text
framing                 프레임 byte layout
socket / Client          TCP, heartbeat, raw request/response
types / errors           공개 값과 오류
RobotApi                 명령별 facade (safety/navigation/mission/configuration/inspection)
```

일반 C++ 연동은 `<shalom/api.hpp>`를 포함한다. Python은 `Client`를 만들고
`RobotApi(client)`로 명령을 보낸다. 두 언어 모두 facade 내부를 safety,
navigation, mission, configuration, inspection 도메인으로 나눈다. TCP, framing,
heartbeat와 명령 구현은 C++ shared library 안에 있으며 공개 헤더에는 포함되지
않는다. 모든 sample은 read-only 또는 조회 명령만 사용한다.

## 지원 범위와 제약

- C++/Python은 같은 protocol channel과 명령 의미를 제공한다.
- 상태·이벤트의 소유자와 모든 안전 판단은 로봇이다.
- `cmd/arm/ee_goal`은 아직 구현되지 않았다. 나머지 arm API는 커미셔닝 전용이다.
- 현재 브릿지는 client 한 대만 허용한다. HMI와 고객 application을 동시에 연결할 수 없다.
- TCP `9090`은 TLS·사용자 인증이 없는 전용 제어망 API다. 공개망에 노출하면 안 된다.

## 문서

| 문서 | 필요한 때 |
| --- | --- |
| [API](docs/API.md) | C++/Python API와 command parity 확인 |
| [Transport](docs/transport.md) | 연결, heartbeat, framing 구현 |
| [State](docs/state.md) | 상태·이벤트 payload 표시 |
| [Command](docs/command.md) | 승인된 명령 payload와 결과 처리 |
| [Errors](docs/errors.md) · [catalog](docs/error_codes.json) | 오류 코드와 운용 조치 표시 |
| [Security](docs/security.md) | 네트워크·권한·배포 기준 |
| [Changelog](docs/CHANGELOG.md) | SDK/protocol 호환성 확인 |

릴리스 파일을 수정하거나 임의로 섞지 말고, `VERSION`과 robot runtime의 protocol
version을 함께 확인한다.
