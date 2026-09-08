# 빌드 시점의 정확한 버전 정보를 수집한다.
#
# 과업지시서 4장이 "실제 사용된 각 컴포넌트의 정확한 버전(커밋 해시 포함)" 을
# 시스템 설계서에 기재하도록 요구한다. 손으로 적으면 반드시 어긋나므로
# 빌드가 스스로 기록하게 한다.

find_package(Git QUIET)

set(HMI_GIT_HASH "unknown")
set(HMI_GIT_DIRTY "")

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE HMI_GIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)

    # 커밋되지 않은 변경이 섞인 빌드를 납품하면 임치본과 산출물이 달라진다.
    # 그 사실이 산출물에 남아야 나중에 추적할 수 있다.
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _git_status
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT _git_status STREQUAL "")
        set(HMI_GIT_DIRTY "-dirty")
    endif()
endif()

string(TIMESTAMP HMI_BUILD_DATE "%Y-%m-%d %H:%M:%S" UTC)

message(STATUS "Inspection HMI ${PROJECT_VERSION} (${HMI_GIT_HASH}${HMI_GIT_DIRTY})")

# 납품 문서와 화면에 함께 쓸 이름. 계약 명의가 확정되면 여기만 고친다.
set(HMI_PRODUCT_NAME "철도차량 하부점검 관제 시스템" CACHE STRING "제품명")
set(HMI_VENDOR "위고로보틱스" CACHE STRING "개발사")
set(HMI_CLIENT "샬롬엔지니어링주식회사" CACHE STRING "발주기관")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildInfo.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/BuildInfo.h"
    @ONLY)
