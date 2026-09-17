# Windows 빌드

납품 대상인 관제 PC 가 Windows 이므로 실제 연동은 대개 여기서 이뤄진다.

## 필요한 것

Visual Studio 2019 이상(“C++를 사용한 데스크톱 개발” 구성 요소)과 CMake.
외부 라이브러리는 쓰지 않는다.

## 빌드

개발자 명령 프롬프트에서:

```bat
cd shalom-robot-sdk\Windows
cmake -S cpp -B build -A x64
cmake --build build --config Release
```

예제가 `build\Release\shalom_monitor.exe` 로 나온다.

```bat
build\Release\shalom_monitor.exe 192.168.210.88
build\Release\shalom_api_example.exe 192.168.210.88
```

## 내 프로그램에 붙이기

```cmake
add_subdirectory(path/to/shalom-robot-sdk/Windows/cpp)
target_link_libraries(my_app PRIVATE shalom::sdk)
```

`shalom::sdk` 가 `ws2_32` 를 함께 걸어 주므로 따로 적지 않아도 된다.

CMake 를 쓰지 않는다면 포함 디렉터리에 `shalom-robot-sdk\Windows\cpp\include` 를 넣고 `ws2_32.lib` 를
링크한다.

## 알아 둘 것

**Winsock 초기화** — `WSAStartup` 은 `TcpClient` 가 처음 연결할 때 한 번
부른다. 직접 소켓을 여는 코드가 따로 있다면 `shalom::SocketLibrary::ensureStarted()`
를 먼저 부른다.

**한글 출력** — 콘솔 예제의 한글이 깨지면 코드 페이지를 바꾼다.

```bat
chcp 65001
```

소스는 UTF-8 이고 `/utf-8` 로 컴파일한다. MSVC 는 이 옵션이 없으면 원본
인코딩을 시스템 코드 페이지로 짐작해 문자열을 깨뜨린다.

**방화벽** — 첫 실행에서 Windows 방화벽이 묻는다. 로봇이 있는 망의 프로필
(대개 “개인”)에 허용해야 한다. 거부한 뒤에는 다시 묻지 않으므로, 연결이
안 되면 고급 보안 방화벽에서 규칙을 확인한다.

**연결이 되는데 아무것도 오지 않을 때** — 로봇은 클라이언트를 한 대만 받는다.
다른 관제 화면이 이미 붙어 있는지 본다.

```bat
netstat -an | findstr 9090
```
