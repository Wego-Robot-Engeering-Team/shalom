# Linux 빌드

## 필요한 것

컴파일러와 CMake 뿐이다. 외부 라이브러리는 쓰지 않는다.

```bash
sudo apt install build-essential cmake
```

C++17 을 지원하는 컴파일러면 된다 (GCC 7 이상, Clang 5 이상).

## 빌드

```bash
cd shalom-robot-sdk/Linux
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

예제가 `build/shalom_monitor` 로 나온다.

```bash
./build/shalom_monitor 192.168.210.88
./build/shalom_api_example 192.168.210.88   # SDK API 조회 예제
```

## 내 프로그램에 붙이기

릴리스에서는 `include/`와 `lib/libshalom_sdk.so`만 고객에게 제공한다. 소스
`src/`는 배포물에 넣지 않는다.

```cmake
find_package(ShalomSdk CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE shalom::sdk)
```

SDK가 시스템 경로 밖에 있으면 고객 application을 구성할 때 설치 prefix를 준다.

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/shalom-sdk
```

개발·릴리스 패키지 설치는 다음과 같다.

```bash
cmake --install build --prefix "$PWD/shalom-sdk-linux"
```

## 알아 둘 것

**연결 끊김** — SDK가 `SIGPIPE`를 막고 socket error로 돌려준다. 고객 application은
`Client::run()`의 오류를 표시·재연결 정책에 따라 처리한다.

**방화벽** — 로봇의 `9090` 으로 나가는 TCP 연결이 열려 있어야 한다.

```bash
ss -tn | grep 9090
```

연결이 되는데 아무것도 오지 않으면 대부분 다른 관제가 이미 붙어 있는
경우다. 로봇은 클라이언트를 한 대만 받는다.
