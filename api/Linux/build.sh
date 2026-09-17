#!/usr/bin/env bash
# GTX-A 연동 SDK — Linux 빌드
set -euo pipefail
cd "$(dirname "$0")"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
echo
echo "완료: $(pwd)/build/shalom_monitor"
echo "사용: ./build/shalom_monitor <로봇주소> [포트]"
