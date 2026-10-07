# C++ 빌드·설치

CMake 3.16+, C++17, `nlohmann_json` 3.9+가 필요하다. JSON 파서는 라이브러리 내부에서
사용하며 공개 헤더에는 JSON 라이브러리 의존성이 없다.

| OS | 개발 환경 |
| --- | --- |
| Linux | GCC/Clang, CMake, `nlohmann-json3-dev` |
| macOS | Xcode Command Line Tools, CMake, `nlohmann-json` |
| Windows | Visual Studio C++ 도구, CMake, vcpkg의 `nlohmann-json` |

해당 OS 디렉터리(`Linux/`, `MacOS/`, `Windows/`)에서 실행한다.

```bash
cmake -S cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
cmake --install build --config Release --prefix <install-prefix>
```

Windows에서 vcpkg을 사용하면 구성 명령에 다음 옵션을 추가한다.

```text
-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
```

해당 OS 디렉터리의 `build.sh` 또는 `build.bat`으로도 빌드할 수 있다.
DLL·공유 라이브러리와 실행 파일은 대상 OS에서 빌드한다.

## 외부 프로젝트

```cmake
find_package(RobotSdk CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE robot_sdk::sdk)
```

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<install-prefix>
```

설치 구성은 `include/robot_sdk/`, `lib/`, `bin/`이다. Windows에서는 DLL과 실행 파일이
`bin/`에 설치된다. 공유 라이브러리와 헤더는 같은 SDK 버전을 사용한다.
SDK 0.4의 ABI는 `SOVERSION 1`이며 기존 C++ 프로그램을 다시 빌드해야 한다.

## 검증

```bash
cmake -S cpp -B build -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release --output-on-failure
```
