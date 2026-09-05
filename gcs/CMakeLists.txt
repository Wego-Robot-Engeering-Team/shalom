cmake_minimum_required(VERSION 3.21)
project(shalom_gcs VERSION 0.1.0 LANGUAGES CXX)

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

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# 코어 라이브러리
#
# UI 실행 파일과 테스트가 함께 링크한다. 납품 형태를 .so/.dll 로 요구받는
# 경우(과업지시서 4장) BUILD_SHARED_LIBS=ON 으로 공유 라이브러리를 낸다.
# ---------------------------------------------------------------------------
add_library(gcs_core
    src/Config.cpp
    src/auth/Session.cpp
    src/views/WelcomeDialog.cpp
    src/views/SettingsDialog.cpp
    src/theme/Tokens.cpp
    src/theme/Style.cpp
    src/net/Framing.cpp
    src/net/Envelope.cpp
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
    src/sim/SimRobot.cpp
    src/panels/StatusPanel.cpp
    src/panels/TeleopPanel.cpp
    src/panels/WaypointPanel.cpp
    src/panels/ArmPanel.cpp
    src/panels/LocationPanel.cpp
    src/widgets/HealthRow.cpp
    src/widgets/Robot3DView.cpp
    src/panels/DiagnosticsPanel.cpp
    src/MainWindow.cpp
)
target_include_directories(gcs_core PUBLIC src)
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
add_executable(shalom_gcs
    src/main.cpp
)
target_link_libraries(shalom_gcs PRIVATE gcs_core)

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
    gcs_add_test(test_sim)
    gcs_add_test(test_capture)
    gcs_add_test(test_session)

    gcs_add_test(test_docs)
    # 명세서 경로를 알려준다. 문서와 코드의 드리프트를 잡는 검사다.
    target_compile_definitions(test_docs PRIVATE GCS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()
