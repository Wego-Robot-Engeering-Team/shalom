cmake_minimum_required(VERSION 3.21)
project(inspection_hmi VERSION 0.1.0 LANGUAGES CXX)

# 납품 시 시스템 설계서에 기재할 버전 정보를 빌드에 박아 넣는다.
# (과업지시서 4장 — 실제 사용 버전 기재 요구)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Svg)

# 통신 규약 버전을 헤더에서 읽어 온다. CMake 에 따로 적어 두면 언젠가 어긋난다.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/net/Envelope.h" _envelope_header)
string(REGEX MATCH "kProtocolVersion = ([0-9]+)" _ "${_envelope_header}")
set(HMI_PROTOCOL_VERSION "${CMAKE_MATCH_1}")

include(cmake/Version.cmake)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# 코어 라이브러리
#
# UI 실행 파일과 테스트가 함께 링크한다. 과업지시서 4장이 관제 소프트웨어를
# "라이브러리 모듈 파일" 로 요구하므로, 납품 구성에서는 공유 라이브러리로 낸다.
# ---------------------------------------------------------------------------
option(HMI_SHARED_CORE "코어를 공유 라이브러리로 빌드 (납품 형태)" OFF)
if(HMI_SHARED_CORE)
    set(HMI_CORE_TYPE SHARED)
    # Windows 는 명시적으로 내보내지 않은 심볼이 DLL 에 남지 않는다.
    # 클래스마다 __declspec(dllexport) 매크로를 붙이는 대신 CMake 가 전부
    # 내보내게 한다. 데이터 심볼은 이 방식으로 나가지 않지만, 공개 API 는
    # 전부 함수와 클래스라 문제되지 않는다.
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    set(HMI_CORE_TYPE STATIC)
endif()

add_library(hmi_core ${HMI_CORE_TYPE}
    src/Config.cpp
    src/auth/Session.cpp
    src/views/WelcomeDialog.cpp
    src/views/SettingsDialog.cpp
    src/theme/Tokens.cpp
    src/theme/Style.cpp
    src/net/Framing.cpp
    src/net/Envelope.cpp
    src/net/BridgeClient.cpp
    src/capture/CaptureMetadata.cpp
    src/data/InspectionRecord.cpp
    src/widgets/PreviewView.cpp
    src/widgets/Toast.cpp
    src/panels/CapturePanel.cpp
    src/panels/DataPanel.cpp
    src/mapview/MapInfo.cpp
    src/diag/CodeCatalog.cpp
    src/diag/LogStore.cpp
    src/widgets/Primitives.cpp
    src/widgets/BrandMark.cpp
    src/widgets/NavRail.cpp
    src/widgets/MapCard.cpp
    src/diag/CodeInfoPopup.cpp
    src/panels/EventLogPanel.cpp
    src/widgets/Gauges.cpp
    src/widgets/EStopButton.cpp
    src/mapview/MapItems.cpp
    src/mapview/MapView.cpp
    src/mapview/MapRender.cpp
    src/mapview/MapFile.cpp
    src/robot/RobotLink.cpp
    src/robot/PoseCheck.cpp
    src/robot/Kinematics.cpp
    src/panels/StatusPanel.cpp
    src/panels/MissionPanel.cpp
    src/widgets/WaypointDelegate.cpp
    src/widgets/NotificationCenter.cpp
    src/widgets/IconButton.cpp
    src/widgets/MapLegend.cpp
    src/widgets/ValueSlider.cpp
    src/panels/TeleopPanel.cpp
    src/panels/WaypointPanel.cpp
    src/panels/ArmPanel.cpp
    src/panels/LocationPanel.cpp
    src/widgets/HealthRow.cpp
    src/widgets/Robot3DView.cpp
    src/widgets/RobotMesh.cpp
    src/panels/DiagnosticsPanel.cpp
    src/MainWindow.cpp
)
target_include_directories(hmi_core PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/../common/protocol/include
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(hmi_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg)

# 코드 카탈로그를 바이너리에 내장한다.
#
# .qrc 를 소스 목록에 넣는 방식은 정적 라이브러리에서 초기화가 누락된다
# (링커가 참조되지 않는 오브젝트를 버린다). qt_add_resources 는 Qt6 가
# 이 초기화 문제를 처리해 주므로 반드시 이쪽을 쓴다.
qt_add_resources(hmi_core "hmi_resources"
    PREFIX "/"
    BASE resources
    FILES resources/error_codes.json
    # 3D 자세 뷰의 형상. 생성 방법은 tools/make_robot_mesh.py 참조.
    FILES resources/robot_mesh.bin
)

# 발주기관 로고. 자산이 없으면 BrandMark 가 중립 마크로 폴백한다
# (resources/brand/README.md 의 상표 사용 승인 절차 참조).
qt_add_resources(hmi_core "hmi_brand"
    PREFIX "/brand"
    BASE resources/brand
    FILES resources/brand/logo.svg
)

# ---------------------------------------------------------------------------
# 테스트베드 — 로봇 없이 화면을 돌리기 위한 것들
# ---------------------------------------------------------------------------
#
# 시뮬레이터와 대역 지도는 관제 UI 가 아니다. 개발과 검수 때 로봇 없이
# 화면을 띄우기 위한 것이고, 납품물에는 들어가면 안 된다 — 들어가면 현장에서
# 로봇이 안 붙었을 때 조용히 가짜 데이터로 도는 화면이 만들어진다.
#
# 그래서 별도 타깃으로 두고 release 프리셋에서는 끈다.
option(HMI_WITH_TESTBED "Build the robot-free simulator and stand-in map" ON)

if(HMI_WITH_TESTBED)
    add_library(hmi_testbed STATIC testbed/sim/SimRobot.cpp)
    target_link_libraries(hmi_testbed PUBLIC hmi_core)
    target_include_directories(hmi_testbed PUBLIC testbed)
    set_target_properties(hmi_testbed PROPERTIES AUTOMOC ON)

    # 브랜드 아이콘 생성기. 로고가 바뀔 때 사람이 한 번 돌리는 도구이고,
    # 결과물은 저장소에 커밋한다 — 빌드 환경에 SVG 래스터라이저를 요구하지
    # 않기 위해서다.
    add_executable(make_brand_icon testbed/brand/make_icon.cpp)
    target_link_libraries(make_brand_icon PRIVATE Qt6::Gui Qt6::Svg)

    qt_add_resources(hmi_testbed "testbed_maps"
        PREFIX "/maps"
        BASE testbed/maps
        FILES testbed/maps/gtxa_depot.yaml testbed/maps/gtxa_depot.pgm
    )
endif()

# ---------------------------------------------------------------------------
# 실행 파일
# ---------------------------------------------------------------------------
# 윈도우 실행 파일 아이콘. .rc 는 MSVC/MinGW 리소스 컴파일러가 처리하며,
# 다른 플랫폼에서는 빈 목록이라 무시된다.
set(HMI_PLATFORM_SOURCES "")
if(WIN32)
    list(APPEND HMI_PLATFORM_SOURCES resources/brand/app.rc)
endif()

add_executable(inspection_hmi
    src/main.cpp
    ${HMI_PLATFORM_SOURCES}
)
if(HMI_WITH_TESTBED)
    target_link_libraries(inspection_hmi PRIVATE hmi_testbed)
    target_compile_definitions(inspection_hmi PRIVATE HMI_WITH_TESTBED=1)
else()
    target_link_libraries(inspection_hmi PRIVATE hmi_core)
endif()
# ---------------------------------------------------------------------------
# 테스트
# ---------------------------------------------------------------------------
option(HMI_BUILD_TESTS "단위 테스트 빌드" ON)
if(HMI_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    # Qt 의 QTEST_MAIN 계열 매크로가 가변인자 매크로를 인자 없이 확장한다.
    # 우리 코드 문제가 아니고 고칠 수도 없는데, 남겨두면 실제 경고가 묻힌다.
    # 테스트 타깃에서만 억제한다.
    if(NOT MSVC)
        set(HMI_TEST_FLAGS -Wno-variadic-macro-arguments-omitted)
    endif()

    # 테스트 타깃 공통 설정.
    function(hmi_add_test name)
        add_executable(${name} tests/${name}.cpp)
        # hmi_testbed 는 hmi_core 를 PUBLIC 으로 물고 있다. 둘을 같이 적으면
        # 링커가 같은 라이브러리를 두 번 받아 매 링크마다 경고를 낸다.
        if(HMI_WITH_TESTBED)
            target_link_libraries(${name} PRIVATE hmi_testbed Qt6::Test)
        else()
            target_link_libraries(${name} PRIVATE hmi_core Qt6::Test)
        endif()
        target_compile_options(${name} PRIVATE ${HMI_TEST_FLAGS})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    hmi_add_test(test_framing)
    hmi_add_test(test_mapinfo)

    hmi_add_test(test_headers)
    # 소스 트리를 직접 스캔하는 검사라 경로가 필요하다.
    target_compile_definitions(test_headers PRIVATE HMI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    hmi_add_test(test_catalog)
    hmi_add_test(test_theme)

    hmi_add_test(test_kinematics)

    # 시뮬레이터가 있어야 도는 검사들. 납품 구성은 시뮬레이터를 빼고
    # 빌드하므로 (HMI_WITH_TESTBED=OFF) 여기서도 빠져야 한다 — 그러지
    # 않으면 납품 구성 자체가 컴파일되지 않는다.
    #
    # 화면을 그려 보는 검사가 납품 구성에서 빠지는 것은 감수한다. 위젯은
    # 두 구성에서 같고, 다른 것은 로그인 요구와 코어를 공유 라이브러리로
    # 내는지뿐이다. 로봇 없이 창을 띄우려면 RobotLink 구현이 하나는
    # 있어야 하는데, 납품 빌드에는 브릿지 클라이언트밖에 없다.
    if(HMI_WITH_TESTBED)
        # 전 화면을 실제로 그려 보는 연기 테스트. 화면 없이 돌도록 offscreen
        # 플랫폼을 강제한다. 그리기 경로에서만 터지는 버그를 잡는 유일한 검사다.
        hmi_add_test(test_render)
        set_tests_properties(test_render PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

        hmi_add_test(test_sim)
    endif()

    # 3D 뷰는 테스트베드에 기대지 않으므로 납품 구성에서도 돈다 — 그려지는
    # 속도를 재는 검사가 정작 납품 빌드를 재지 않으면 의미가 없다.
    hmi_add_test(test_mapview)
    set_tests_properties(test_mapview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    hmi_add_test(test_robotview)
    set_tests_properties(test_robotview PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    hmi_add_test(test_capture)

    hmi_add_test(test_bridge)
    hmi_add_test(test_capture_meta)
    hmi_add_test(test_records)
    hmi_add_test(test_config)
    hmi_add_test(test_session)

    hmi_add_test(test_docs)
    # 명세서 경로를 알려준다. 문서와 코드의 드리프트를 잡는 검사다.
    target_compile_definitions(test_docs PRIVATE HMI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()


# ---------------------------------------------------------------------------
# 납품 패키지
#
# 구성과 절차는 packaging/README.md 참조.
#
#   cmake --preset release
#   cmake --build --preset release
#   cmake --install build-release --prefix dist/inspection-hmi
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

set_target_properties(hmi_core PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

# 실행 파일이 자기 옆의 공유 라이브러리를 찾을 수 있어야 한다.
#
# 이것이 없으면 설치본이 아예 뜨지 않는다 — 개발 트리에서는 빌드 디렉터리
# 경로가 박혀 있어 잘 돌기 때문에, 실제로 패키지를 풀어 실행해 보기 전까지
# 드러나지 않는 종류의 고장이다.
#
# Windows 는 실행 파일과 같은 디렉터리의 DLL 을 자동으로 찾으므로 불필요하다.
if(APPLE)
    set(HMI_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(HMI_INSTALL_RPATH "$ORIGIN")
endif()

set_target_properties(inspection_hmi PROPERTIES
    INSTALL_RPATH "${HMI_INSTALL_RPATH}"
    BUILD_WITH_INSTALL_RPATH OFF)

install(TARGETS inspection_hmi hmi_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin          # Windows 는 DLL 이 실행 파일 옆에 있어야 한다
    ARCHIVE DESTINATION lib)

# ---- 공개 헤더 ----
#
# 인터페이스 계약에 해당하는 것만 내보낸다. 화면 구성이나 시뮬레이터 같은
# 구현 세부는 고객이 알 필요도, 의존할 이유도 없다.
set(HMI_PUBLIC_HEADERS
    src/net/Framing.h           # 통신 프레이밍
    src/net/Envelope.h          # JSON 봉투
    src/net/Channels.h          # 채널 이름
    src/robot/RobotLink.h       # 로봇 연동 인터페이스
    src/robot/RobotTypes.h
    src/diag/CodeCatalog.h      # 진단 코드
    src/capture/CaptureMetadata.h   # 메타데이터·파일명 규약
)
foreach(_header IN LISTS HMI_PUBLIC_HEADERS)
    get_filename_component(_dir "${_header}" DIRECTORY)
    string(REPLACE "src/" "" _dir "${_dir}")
    install(FILES "${_header}" DESTINATION include/inspection_hmi/${_dir})
endforeach()

# 프레이밍은 로봇측과 공유하는 구현이라 함께 내보낸다.
install(FILES ../common/protocol/include/inspection/framing.hpp
        DESTINATION include/inspection)

# ---- 참조 자료 ----
#
# 진단 코드는 바이너리에 내장되지만 사본을 함께 낸다. 비상대응 매뉴얼과
# 대조하려면 문서로 읽을 수 있어야 한다.
# 리눅스 런처 등록물. 압축을 풀어 쓰는 구조라 자동 설치는 하지 않고,
# 필요할 때 관리자가 복사할 수 있게 함께 넣는다.
if(UNIX AND NOT APPLE)
    install(FILES resources/brand/inspection-hmi.desktop DESTINATION share/applications)
    install(FILES resources/brand/app-256.png
            DESTINATION share/icons/hicolor/256x256/apps
            RENAME inspection-hmi.png)
    install(FILES resources/brand/app-128.png
            DESTINATION share/icons/hicolor/128x128/apps
            RENAME inspection-hmi.png)
    install(FILES resources/brand/app-48.png
            DESTINATION share/icons/hicolor/48x48/apps
            RENAME inspection-hmi.png)
endif()

install(FILES resources/error_codes.json DESTINATION share/inspection_hmi)
install(FILES ../docs/bridge_protocol.md DESTINATION share/inspection_hmi)

# ---- 버전 정보 ----
configure_file(cmake/VERSION.txt.in "${CMAKE_BINARY_DIR}/VERSION.txt" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/VERSION.txt" DESTINATION .)

# ---- 라이선스 ----
#
# Qt 를 LGPL v3 로 동적 링크해 쓰므로 라이선스 전문과 재링크 안내를 함께
# 배포해야 한다. 정적 링크로 바꾸면 전체 소스 공개 의무가 생긴다.
install(DIRECTORY licenses/ DESTINATION licenses OPTIONAL)
