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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

예제가 `build/shalom_monitor` 로 나온다.

```bash
./build/shalom_monitor 192.168.210.88
```

## 내 프로그램에 붙이기

헤더 전용이라 include 경로만 잡으면 된다.

```cmake
add_subdirectory(path/to/shalom-robot-sdk/Linux)
target_link_libraries(my_app PRIVATE shalom::sdk)
```

CMake 를 쓰지 않는다면 `-I` 하나로 끝난다.

```bash
g++ -std=c++17 -I shalom-robot-sdk/Linux/include my_app.cpp -o my_app
```

## 알아 둘 것

**`SIGPIPE`** — 끊긴 연결에 쓰면 기본 동작이 프로세스 종료다. `socket.hpp` 가
`MSG_NOSIGNAL` 로 막고 있으므로 SDK 를 거치는 한 문제되지 않는다. 직접
소켓을 쓴다면 같은 처리를 해야 한다.

**방화벽** — 로봇의 `9090` 으로 나가는 TCP 연결이 열려 있어야 한다.

```bash
ss -tn | grep 9090
```

연결이 되는데 아무것도 오지 않으면 대부분 다른 관제가 이미 붙어 있는
경우다. 로봇은 클라이언트를 한 대만 받는다.
