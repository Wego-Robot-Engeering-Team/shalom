# Robot SDK

Python 3.9+와 C++17에서 로봇의 TCP 브리지에 연결한다. SDK `0.4.0`, protocol `v:1`.

## 구성

```text
sdk/
├── Linux/
│   ├── cpp/
│   └── python/
├── MacOS/
│   ├── cpp/
│   └── python/
├── Windows/
│   ├── cpp/
│   └── python/
└── docs/
```

각 OS 폴더에 C++·Python 소스, 예제, 테스트를 둔다.
Linux·MacOS에는 `build.sh`, Windows에는 `build.bat`이 있다.

## 실행

아래 명령은 SDK 루트 기준 Linux 예시다. macOS는 `Linux`를 `MacOS`로 바꾼다.

```bash
python3 -m pip install ./Linux/python
python3 Linux/python/examples/monitor.py <robot-ip> --channel state/pose
python3 Linux/python/examples/catalogs.py <robot-ip>

cmake -S Linux/cpp -B Linux/build -DCMAKE_BUILD_TYPE=Release
cmake --build Linux/build --parallel 4
./Linux/build/robot_monitor <robot-ip>
./Linux/build/robot_api_example <robot-ip>
```

Windows에서는 SDK 루트에서 실행한다.

```bat
python -m pip install .\Windows\python
python Windows\python\examples\monitor.py <robot-ip>
Windows\build.bat
Windows\build\Release\robot_monitor.exe <robot-ip>
```

C++ 빌드에는 CMake 3.16+, C++17 컴파일러, `nlohmann_json` 3.9+가 필요하다.
Python 런타임은 표준 라이브러리만 사용한다.

## 연동 기준

- 기본 포트: `9090/tcp`. HMI와 SDK 중 한 클라이언트만 연결한다.
- `poll()`·`run()`을 200 ms 간격으로 호출한다. 동기 `request()`도 하트비트를 유지한다.
- 요청 전송 → `res` 결과 확인 → `state/*`에서 실제 동작 상태 확인 순서로 처리한다.
- 저장 데이터의 원본은 로봇이다. 목록 교체에는 편집 전 스냅샷, 미션·팔 자세 수정에는 `expected_revision`을 보낸다.
- 요청 타임아웃은 실행 여부가 불확실한 상태다. 상태를 확인하고 후속 명령을 결정한다.
- 팔 실행은 `state/system.arm_execution_enabled`와 로봇의 안전·권한 조건을 따른다.
- TCP는 TLS·사용자 인증을 제공하지 않는다. 전용 제어망에서 사용한다.

## 문서

[API](docs/API.md) · [명령](docs/command.md) · [상태](docs/state.md) ·
[통신](docs/transport.md) · [오류](docs/errors.md) · [보안](docs/security.md) ·
[변경 이력](docs/CHANGELOG.md)

| OS | C++ | Python |
| --- | --- | --- |
| Linux | [빌드·설치](Linux/cpp/BUILD.md) | [실행](Linux/python/README.md) |
| macOS | [빌드·설치](MacOS/cpp/BUILD.md) | [실행](MacOS/python/README.md) |
| Windows | [빌드·설치](Windows/cpp/BUILD.md) | [실행](Windows/python/README.md) |

## 테스트

SDK 루트에서 해당 OS 경로를 사용한다.

```bash
PYTHONPATH=Linux/python python3 -m unittest discover -s Linux/python/tests -v
cmake -S Linux/cpp -B Linux/build -DBUILD_TESTING=ON
cmake --build Linux/build --parallel 4
ctest --test-dir Linux/build --output-on-failure
```

ROS 브리지 연동 테스트는 임시 데이터와 격리된 ROS domain을 사용한다.

```bash
source /opt/ros/jazzy/setup.bash
source <workspace>/install/setup.bash
python3 Linux/python/tests/integration_bridge.py <workspace>/build/hmi_bridge/hmi_bridge_node
```
