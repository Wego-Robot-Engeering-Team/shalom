#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: deploy/scripts/build_robot_runtime.sh --version <version> [--allow-dirty]

Builds a non-symlinked, arm64 Release install tree and wraps it in
shalom-runtime_<version>_arm64.deb under dist/robot/.  Run only on the
dedicated Jetson release runner (Ubuntu 24.04 + ROS 2 Jazzy).
EOF
}

VERSION=""
ALLOW_DIRTY=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --version) VERSION="${2:-}"; shift 2 ;;
    --allow-dirty) ALLOW_DIRTY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "알 수 없는 인자: $1" >&2; usage >&2; exit 2 ;;
  esac
done

if [[ -z "$VERSION" || ! "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][A-Za-z0-9]+)*$ ]]; then
  echo "--version <major.minor.patch>가 필요합니다." >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
workspace_root="$(cd "$repo_root/../.." && pwd)"
template_root="$repo_root/deploy/packaging/robot-runtime"
dist_root="$repo_root/dist/robot"
build_root="$workspace_root/build/release-$VERSION"
install_root="$workspace_root/install/release-$VERSION"
stage_root="$workspace_root/build/package-root-$VERSION"
parallel_workers="${SHALOM_RELEASE_PARALLEL_WORKERS:-2}"

if [[ ! "$parallel_workers" =~ ^[1-9][0-9]*$ ]]; then
  echo "SHALOM_RELEASE_PARALLEL_WORKERS는 1 이상의 정수여야 합니다." >&2
  exit 2
fi

if [[ "$(dpkg --print-architecture)" != "arm64" ]]; then
  echo "이 스크립트는 Jetson arm64 release runner에서만 실행합니다." >&2
  exit 1
fi
if [[ ! -r /opt/ros/jazzy/setup.bash ]]; then
  echo "/opt/ros/jazzy/setup.bash가 없습니다." >&2
  exit 1
fi
if [[ $ALLOW_DIRTY -eq 0 ]] && [[ -n "$(git -C "$repo_root" status --porcelain --untracked-files=all)" ]]; then
  echo "Git 작업 트리가 깨끗하지 않습니다. 릴리스에는 --allow-dirty를 사용하지 마십시오." >&2
  exit 1
fi
if git -C "$repo_root" submodule status --recursive | grep -q '^[+-]'; then
  echo "submodule commit 또는 초기화 상태가 고정되지 않았습니다." >&2
  exit 1
fi

rm -rf "$build_root" "$install_root" "$stage_root"
mkdir -p "$dist_root" "$stage_root/DEBIAN" "$stage_root/opt/shalom/releases/$VERSION/bin"

set +u
source /opt/ros/jazzy/setup.bash
set -u

# Aurora upstream은 Jazzy 이전 cv_bridge 헤더명을 사용한다. release runner의
# 고정 checkout에만 적용하는 호환 보정이며, upstream submodule commit을 바꾸지 않는다.
aurora_server="$repo_root/robot/third_party/aurora_ros/src/slamware_ros_sdk/src/server"
if [[ -f "$aurora_server/server_workers.cpp" ]]; then
  sed -i 's|<cv_bridge/cv_bridge\.hp*>|<cv_bridge/cv_bridge.hpp>|g' \
    "$aurora_server/server_workers.cpp" "$aurora_server/slamware_ros_sdk_server.cpp"
fi

# robot_bringup의 package.xml dependency graph를 기준으로 production runtime을
# 빌드한다. simulation_bringup, velodyne_vlp16은 root가 아니므로
# 이 릴리스 install tree에 들어가지 않는다.
colcon build \
  --base-paths "$repo_root" \
  --build-base "$build_root" \
  --install-base "$install_root" \
  --merge-install \
  --parallel-workers "$parallel_workers" \
  --packages-up-to robot_bringup \
  --cmake-args -DCMAKE_BUILD_TYPE=Release

install -m 0755 "$template_root/bin/shalom-robot.in" \
  "$stage_root/opt/shalom/releases/$VERSION/bin/shalom-robot"
sed -i "s/@VERSION@/$VERSION/g" \
  "$stage_root/opt/shalom/releases/$VERSION/bin/shalom-robot"

cp -a "$install_root/." "$stage_root/opt/shalom/releases/$VERSION/"
install -D -m 0644 "$template_root/systemd/shalom-robot.service" \
  "$stage_root/lib/systemd/system/shalom-robot.service"
install -m 0755 "$template_root/debian/postinst.in" "$stage_root/DEBIAN/postinst"
sed -i "s/@VERSION@/$VERSION/g" "$stage_root/DEBIAN/postinst"
sed "s/@VERSION@/$VERSION/g" "$template_root/debian/control.in" \
  > "$stage_root/DEBIAN/control"

dpkg-deb --root-owner-group --build "$stage_root" \
  "$dist_root/shalom-runtime_${VERSION}_arm64.deb"

printf '%s\n' "Built: $dist_root/shalom-runtime_${VERSION}_arm64.deb"
