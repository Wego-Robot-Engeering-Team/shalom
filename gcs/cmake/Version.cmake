# 빌드 시점의 정확한 버전 정보를 수집한다.
#
# 과업지시서 4장이 "실제 사용된 각 컴포넌트의 정확한 버전(커밋 해시 포함)" 을
# 시스템 설계서에 기재하도록 요구한다. 손으로 적으면 반드시 어긋나므로
# 빌드가 스스로 기록하게 한다.

find_package(Git QUIET)

set(GCS_GIT_HASH "unknown")
set(GCS_GIT_DIRTY "")

if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GCS_GIT_HASH
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
        set(GCS_GIT_DIRTY "-dirty")
    endif()
endif()

string(TIMESTAMP GCS_BUILD_DATE "%Y-%m-%d %H:%M:%S" UTC)

message(STATUS "SHALOM GCS ${PROJECT_VERSION} (${GCS_GIT_HASH}${GCS_GIT_DIRTY})")
