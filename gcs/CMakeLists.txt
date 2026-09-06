cmake_minimum_required(VERSION 3.21)
project(inspection_gcs VERSION 0.1.0 LANGUAGES CXX)

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
set(GCS_PROTOCOL_VERSION "${CMAKE_MATCH_1}")

include(cmake/Version.cmake)

# ---------------------------------------------------------------------------
# 로그인 요구 여부
#
# 개발 중에는 매번 로그인하는 것이 번거로우므로 끌 수 있게 한다. 다만 런타임
# 플래그가 아니라 빌드 옵션으로 둔다 — 실행 인자로 끌 수 있으면 납품 빌드에서도
# 꺼진 채 나갈 수 있고, 그 사실을 아무도 눈치채지 못한다.
#
# 납품 빌드는 반드시 ON 으로 구성한다(기본값).
# ---------------------------------------------------------------------------
option(GCS_REQUIRE_LOGIN "기동 시 조작자 로그인을 요구한다 (납품 빌드는 반드시 ON)" ON)
if(NOT GCS_REQUIRE_LOGIN)
    message(WARNING
        "GCS_REQUIRE_LOGIN=OFF — 로그인을 생략하는 개발 빌드입니다. "
        "납품 빌드에 사용하지 마십시오.")
endif()

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
option(GCS_SHARED_CORE "코어를 공유 라이브러리로 빌드 (납품 형태)" OFF)
if(GCS_SHARED_CORE)
    set(GCS_CORE_TYPE SHARED)
    # Windows 는 명시적으로 내보내지 않은 심볼이 DLL 에 남지 않는다.
    # 클래스마다 __declspec(dllexport) 매크로를 붙이는 대신 CMake 가 전부
    # 내보내게 한다. 데이터 심볼은 이 방식으로 나가지 않지만, 공개 API 는
    # 전부 함수와 클래스라 문제되지 않는다.
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
    set(GCS_CORE_TYPE STATIC)
endif()

add_library(gcs_core ${GCS_CORE_TYPE}
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
    src/sim/SimRobot.cpp
    src/panels/StatusPanel.cpp
    src/panels/MissionPanel.cpp
    src/widgets/WaypointDelegate.cpp
    src/widgets/NotificationCenter.cpp
    src/widgets/IconButton.cpp
    src/widgets/ValueSlider.cpp
    src/panels/TeleopPanel.cpp
    src/panels/WaypointPanel.cpp
    src/panels/ArmPanel.cpp
    src/panels/LocationPanel.cpp
    src/widgets/HealthRow.cpp
    src/widgets/Robot3DView.cpp
    src/panels/DiagnosticsPanel.cpp
    src/MainWindow.cpp
)
target_include_directories(gcs_core PUBLIC src ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/include)
target_link_libraries(gcs_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg)

# 코드 카탈로그를 바이너리에 내장한다.
#
# .qrc 를 소스 목록에 넣는 방식은 정적 라이브러리에서 초기화가 누락된다
# (링커가 참조되지 않는 오브젝트를 버린다). qt_add_resources 는 Qt6 가
# 이 초기화 문제를 처리해 주므로 반드시 이쪽을 쓴다.
qt_add_resources(gcs_core "gcs_resources"
    PREFIX "/"
    BASE resources
    FILES resources/error_codes.json
)

# 검수고 기본 지도. SLAM Toolbox 실물이 나오기 전까지 쓰는 대역이며,
# 리소스에 넣어 두면 실행 위치와 무관하게 열린다. 현장 지도는 설정에서
# 파일로 지정한다.
qt_add_resources(gcs_core "gcs_maps"
    PREFIX "/maps"
    BASE resources/maps
    FILES resources/maps/gtxa_depot.yaml resources/maps/gtxa_depot.pgm
)

# 발주기관 로고. 자산이 없으면 BrandMark 가 중립 마크로 폴백한다
# (resources/brand/README.md 의 상표 사용 승인 절차 참조).
qt_add_resources(gcs_core "gcs_brand"
    PREFIX "/brand"
    BASE resources/brand
    FILES resources/brand/logo.svg
)

# ---------------------------------------------------------------------------
# 실행 파일
# ---------------------------------------------------------------------------
add_executable(inspection_gcs
    src/main.cpp
)
target_link_libraries(inspection_gcs PRIVATE gcs_core)
target_compile_definitions(inspection_gcs PRIVATE
    GCS_REQUIRE_LOGIN=$<BOOL:${GCS_REQUIRE_LOGIN}>)

# ---------------------------------------------------------------------------
# 테스트
# ---------------------------------------------------------------------------
option(GCS_BUILD_TESTS "단위 테스트 빌드" ON)
if(GCS_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()

    # Qt 의 QTEST_MAIN 계열 매크로가 가변인자 매크로를 인자 없이 확장한다.
    # 우리 코드 문제가 아니고 고칠 수도 없는데, 남겨두면 실제 경고가 묻힌다.
    # 테스트 타깃에서만 억제한다.
    if(NOT MSVC)
        set(GCS_TEST_FLAGS -Wno-variadic-macro-arguments-omitted)
    endif()

    # 테스트 타깃 공통 설정.
    function(gcs_add_test name)
        add_executable(${name} tests/${name}.cpp)
        target_link_libraries(${name} PRIVATE gcs_core Qt6::Test)
        target_compile_options(${name} PRIVATE ${GCS_TEST_FLAGS})
        add_test(NAME ${name} COMMAND ${name})
    endfunction()

    gcs_add_test(test_framing)
    gcs_add_test(test_mapinfo)

    gcs_add_test(test_headers)
    # 소스 트리를 직접 스캔하는 검사라 경로가 필요하다.
    target_compile_definitions(test_headers PRIVATE GCS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

    gcs_add_test(test_catalog)
    gcs_add_test(test_theme)

    # 전 화면을 실제로 그려 보는 연기 테스트. 화면 없이 돌도록 offscreen
    # 플랫폼을 강제한다. 그리기 경로에서만 터지는 버그를 잡는 유일한 테스트다.
    gcs_add_test(test_render)
    set_tests_properties(test_render PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    gcs_add_test(test_sim)
    gcs_add_test(test_capture)
    gcs_add_test(test_session)

    gcs_add_test(test_bridge)
    gcs_add_test(test_capture_meta)
    gcs_add_test(test_records)

    gcs_add_test(test_docs)
    # 명세서 경로를 알려준다. 문서와 코드의 드리프트를 잡는 검사다.
    target_compile_definitions(test_docs PRIVATE GCS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()


# ---------------------------------------------------------------------------
# 납품 패키지
#
# 구성과 절차는 packaging/README.md 참조.
#
#   cmake --preset release
#   cmake --build --preset release
#   cmake --install build-release --prefix dist/inspection-gcs
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

set_target_properties(gcs_core PROPERTIES
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
    set(GCS_INSTALL_RPATH "@loader_path")
elseif(UNIX)
    set(GCS_INSTALL_RPATH "$ORIGIN")
endif()

set_target_properties(inspection_gcs PROPERTIES
    INSTALL_RPATH "${GCS_INSTALL_RPATH}"
    BUILD_WITH_INSTALL_RPATH OFF)

install(TARGETS inspection_gcs gcs_core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin          # Windows 는 DLL 이 실행 파일 옆에 있어야 한다
    ARCHIVE DESTINATION lib)

# ---- 공개 헤더 ----
#
# 인터페이스 계약에 해당하는 것만 내보낸다. 화면 구성이나 시뮬레이터 같은
# 구현 세부는 고객이 알 필요도, 의존할 이유도 없다.
set(GCS_PUBLIC_HEADERS
    src/net/Framing.h           # 통신 프레이밍
    src/net/Envelope.h          # JSON 봉투
    src/net/Channels.h          # 채널 이름
    src/robot/RobotLink.h       # 로봇 연동 인터페이스
    src/robot/RobotTypes.h
    src/diag/CodeCatalog.h      # 진단 코드
    src/capture/CaptureMetadata.h   # 메타데이터·파일명 규약
)
foreach(_header IN LISTS GCS_PUBLIC_HEADERS)
    get_filename_component(_dir "${_header}" DIRECTORY)
    string(REPLACE "src/" "" _dir "${_dir}")
    install(FILES "${_header}" DESTINATION include/inspection_gcs/${_dir})
endforeach()

# 프레이밍은 로봇측과 공유하는 구현이라 함께 내보낸다.
install(FILES ../protocol/include/inspection/framing.hpp
        DESTINATION include/inspection)

# ---- 참조 자료 ----
#
# 진단 코드는 바이너리에 내장되지만 사본을 함께 낸다. 비상대응 매뉴얼과
# 대조하려면 문서로 읽을 수 있어야 한다.
install(FILES resources/error_codes.json DESTINATION share/inspection_gcs)
install(FILES ../docs/bridge_protocol.md DESTINATION share/inspection_gcs)

# ---- 버전 정보 ----
configure_file(cmake/VERSION.txt.in "${CMAKE_BINARY_DIR}/VERSION.txt" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/VERSION.txt" DESTINATION .)

# ---- 라이선스 ----
#
# Qt 를 LGPL v3 로 동적 링크해 쓰므로 라이선스 전문과 재링크 안내를 함께
# 배포해야 한다. 정적 링크로 바꾸면 전체 소스 공개 의무가 생긴다.
install(DIRECTORY licenses/ DESTINATION licenses OPTIONAL)
