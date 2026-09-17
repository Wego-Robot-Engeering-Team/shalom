# macOS 빌드

## 필요한 것

Xcode 명령행 도구와 CMake. 외부 라이브러리는 쓰지 않는다.

```bash
xcode-select --install
brew install cmake
```

## 빌드

```bash
cd shalom-robot-sdk/MacOS
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

```bash
./build/shalom_monitor 192.168.210.88
./build/shalom_api_example 192.168.210.88   # header API 조회 예제
```

Xcode 프로젝트로 열려면 생성기를 바꾼다.

```bash
cmake -S cpp -B build-xcode -G Xcode
open build-xcode/shalom_sdk.xcodeproj
```

## 내 프로그램에 붙이기

```cmake
add_subdirectory(path/to/shalom-robot-sdk/MacOS/cpp)
target_link_libraries(my_app PRIVATE shalom::sdk)
```

```bash
clang++ -std=c++17 -I shalom-robot-sdk/MacOS/cpp/include my_app.cpp -o my_app
```

## 알아 둘 것

**Apple Silicon** — 소켓 코드는 아키텍처와 무관하다. 유니버설 바이너리가
필요하면 CMake 에 알린다.

```bash
cmake -S cpp -B build -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
```

**연결 끊김** — SDK는 macOS의 `SO_NOSIGPIPE`를 설정해 끊긴 peer를 socket error로
처리한다. 고객 application은 `Client::run()`이 반환한 오류를 표시·재연결 정책에
따라 처리하면 된다.

**로컬 네트워크 권한** — Sonoma 이후 앱이 같은 망의 장치에 붙을 때 권한을
묻는다. 거부하면 연결이 조용히 실패한다. 시스템 설정 ▸ 개인정보 보호 및
보안 ▸ 로컬 네트워크에서 확인한다.
