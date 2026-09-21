#!/usr/bin/env bash
#
#   ./scripts/install.sh
#   ./scripts/install.sh --dry-run

set -euo pipefail

# 역할 분기는 더 이상 CLI로 노출하지 않는다. 전체 개발·로봇·관제 환경을
# 항상 설치하므로, 기존 조건문은 모두 참이 되는 dev로 고정한다.
ROLE=dev
DRY_RUN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "모르는 인자: $1" >&2; exit 2 ;;
  esac
done

say()  { printf '\n\033[1m== %s\033[0m\n' "$*"; }
note() { printf '   %s\n' "$*"; }

run() {
  if [ "$DRY_RUN" = 1 ]; then
    printf '   [dry-run] %s\n' "$*"
  else
    "$@"
  fi
}

apt_install() {
  [ $# -eq 0 ] && return 0
  run sudo apt-get install -y --no-install-recommends "$@"
}

# ROS 2 packages are not in Ubuntu's standard archive. A machine may already
# have the official ros2-apt-source package, which uses a .sources file and an
# embedded key. Do not add a second entry with a different Signed-By value.
ros_apt_source_in_other_file() {
  local source_file
  for source_file in /etc/apt/sources.list \
    /etc/apt/sources.list.d/*.list /etc/apt/sources.list.d/*.sources; do
    [ -f "$source_file" ] || continue
    [ "$source_file" = /etc/apt/sources.list.d/ros2.list ] && continue
    if grep -Eq '^[[:space:]]*(deb([[:space:]]|\[)|URIs:[[:space:]]).*https?://packages[.]ros[.]org/ros2/ubuntu/?([[:space:]]|$)' "$source_file"; then
      return 0
    fi
  done
  return 1
}

setup_ros_apt_source() {
  local ros_list=/etc/apt/sources.list.d/ros2.list
  local expected_ros_list
  printf -v expected_ros_list \
    'deb [arch=%s signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu %s main' \
    "$(dpkg --print-architecture)" "$VERSION_CODENAME"

  if ros_apt_source_in_other_file; then
    if [ -f "$ros_list" ]; then
      if [ "$(cat "$ros_list")" != "$expected_ros_list" ]; then
        echo "기존 ROS 2 저장소 설정을 확인해야 합니다: $ros_list" >&2
        exit 1
      fi
      if [ -e "$ros_list.disabled" ]; then
        echo "백업 파일이 이미 있습니다: $ros_list.disabled" >&2
        exit 1
      fi
      say "중복 ROS 2 apt 저장소 비활성화"
      run sudo mv "$ros_list" "$ros_list.disabled"
    fi
    note "ROS 2 apt 저장소가 이미 설정됨"
    return
  fi

  if [ -f "$ros_list" ]; then
    note "ROS 2 apt 저장소가 이미 설정됨"
    return
  fi

  say "ROS 2 apt 저장소"
  run sudo apt-get update
  apt_install ca-certificates curl gnupg software-properties-common
  run sudo add-apt-repository -y universe

  if [ "$DRY_RUN" = 1 ]; then
    note "packages.ros.org 키와 ${VERSION_CODENAME} 저장소를 등록"
    return
  fi

  curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
    | sudo gpg --dearmor --yes -o /usr/share/keyrings/ros-archive-keyring.gpg
  printf '%s\n' "$expected_ros_list" | sudo tee "$ros_list" >/dev/null
}

# ---------------------------------------------------------------------------
# 환경 확인
#
# 이 저장소의 설치·빌드 대상은 Ubuntu 24.04 + ROS 2 Jazzy다.
# ---------------------------------------------------------------------------
say "환경 확인"
. /etc/os-release
note "OS      $PRETTY_NAME"
ROS_DISTRO_FOUND="$(ls /opt/ros 2>/dev/null | head -1 || true)"
note "ROS     ${ROS_DISTRO_FOUND:-없음}"

if [ "${VERSION_CODENAME:-}" != "noble" ]; then
  echo "지원 OS가 아닙니다: Ubuntu 24.04 (noble)가 필요합니다. 현재: $PRETTY_NAME" >&2
  exit 1
fi

if [ -n "$ROS_DISTRO_FOUND" ] && [ "$ROS_DISTRO_FOUND" != "jazzy" ]; then
  echo "지원 ROS 배포판이 아닙니다: ROS 2 Jazzy가 필요합니다. 현재: $ROS_DISTRO_FOUND" >&2
  exit 1
fi

ROS=jazzy
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE_ROOT="$(cd "$REPO_ROOT/../.." && pwd)"

setup_ros_apt_source

say "패키지 목록 갱신"
run sudo apt-get update

# ---------------------------------------------------------------------------
# 공통 — ROS 기반과 빌드 도구
# ---------------------------------------------------------------------------
say "ROS 기반"
apt_install \
  "ros-$ROS-desktop" \
  "ros-$ROS-rmw-cyclonedds-cpp" \
  ros-dev-tools python3-dev

# shalom이 최상위 저장소이며, 독립 이력을 가진 모든 소스 의존성은
# third_party/ 아래의 고정 커밋 서브모듈이다. b2_driver 안의 Unitree SDK와
# 메시지도 서브모듈이므로 반드시 recursive로 초기화한다.
if [ "$ROLE" = dev ] || [ "$ROLE" = robot ]; then
  say "Git 서브모듈"
  if git -C "$REPO_ROOT" rev-parse --show-toplevel >/dev/null 2>&1; then
    run git -C "$REPO_ROOT" submodule sync --recursive
    run git -C "$REPO_ROOT" submodule update --init --recursive
  else
    note "Git 메타데이터가 없는 배포본이므로 포함된 third_party 소스를 사용한다."
  fi

  if [ "$DRY_RUN" = 0 ]; then
    for required_source in \
      third_party/b2_driver/README.md \
      third_party/b2_simulation/README.md \
      third_party/frcobot_ros2/README.md \
      third_party/aurora_ros/README.md \
      third_party/hesai_lidar_ros2/README.md; do
      if [ ! -f "$REPO_ROOT/$required_source" ]; then
        echo "서브모듈 소스가 없음: $required_source" >&2
        echo "git submodule update --init --recursive 를 실행해야 한다." >&2
        exit 1
      fi
    done
  fi
fi

# Aurora 공식 ROS2 드라이버는 Jazzy에서 cv_bridge 헤더 확장자가 바뀐 전
# 배포본이다. 이전 설치 시 반복 치환된 .hpppp 경로도 함께 복구한다.
if [ "$ROLE" = dev ] || [ "$ROLE" = robot ]; then
  AURORA_SRC="$REPO_ROOT/third_party/aurora_ros/src/slamware_ros_sdk/src/server"
  if [ -f "$AURORA_SRC/server_workers.cpp" ]; then
    run sed -i 's|<cv_bridge/cv_bridge\.hp*>|<cv_bridge/cv_bridge.hpp>|g' \
      "$AURORA_SRC/server_workers.cpp" "$AURORA_SRC/slamware_ros_sdk_server.cpp"
  fi
fi

if [ "$ROLE" = dev ] || [ "$ROLE" = robot ]; then
  say "RealSense USB 권한"
  REALSENSE_RULE="$REPO_ROOT/third_party/librealsense/config/99-realsense-libusb.rules"
  if [ -f "$REALSENSE_RULE" ]; then
    run sudo install -m 644 "$REALSENSE_RULE" /etc/udev/rules.d/99-realsense-libusb.rules
    run sudo udevadm control --reload-rules
    run sudo udevadm trigger
    # The ROS binary packages already provide librealsense 2.58.1 and the
    # viewer.  Keep the pinned SDK source for rules/reference, but do not let
    # colcon build a second SDK into this workspace and override that runtime.
    run install -m 644 /dev/null "$REPO_ROOT/third_party/librealsense/COLCON_IGNORE"
    note "D455를 이미 꽂아 두었다면 한 번 뺐다가 다시 연결한다."
  else
    note "RealSense 소스가 없어 UDEV 규칙 설치를 건너뜀"
  fi
fi

# frcobot_ros2는 FAIRINO 전 기종을 한 저장소에 담고 있다. GTX-A는 FR3만
# 쓰므로 설명(description), 메시지, 현재 펌웨어(v3.9.9) 하드웨어 패키지만
# 빌드하고 나머지 기종/구버전은 colcon에서 제외한다. 서브모듈 소스를
# 지우지 않고 COLCON_IGNORE로만 처리하므로 업스트림은 깨끗하게 유지된다.
if [ "$ROLE" = dev ] || [ "$ROLE" = robot ]; then
  FRCOBOT_ROOT="$REPO_ROOT/third_party/frcobot_ros2"
  if [ -d "$FRCOBOT_ROOT" ]; then
    say "FAIRINO 빌드 범위"
    for pkg_dir in "$FRCOBOT_ROOT"/*/; do
      [ -f "$pkg_dir/package.xml" ] || continue
      case "$(basename "$pkg_dir")" in
        fairino_description|fairino_msgs|fairino_hardware_v3_9_9) continue ;;
      esac
      run install -m 644 /dev/null "$pkg_dir/COLCON_IGNORE"
    done
  fi
fi

# ---------------------------------------------------------------------------
# 로봇 — 주행, 인식, 카메라
# ---------------------------------------------------------------------------
if [ "$ROLE" = dev ] || [ "$ROLE" = robot ]; then
  say "자율주행·SLAM"
  apt_install \
    "ros-$ROS-navigation2" "ros-$ROS-nav2-bringup" \
    "ros-$ROS-slam-toolbox" "ros-$ROS-pointcloud-to-laserscan" \
    "ros-$ROS-rosidl-generator-dds-idl" "ros-$ROS-cv-bridge" \
    "ros-$ROS-twist-mux"

  say "Hesai Pandar XT32 드라이버"
  # HesaiLidar_ROS_2.0가 직접 찾는 시스템 라이브러리다. 공식 드라이버는
  # third_party의 고정 서브모듈로 제공하고, 로봇 전용 설정은 pandar_xt32가 갖는다.
  apt_install libboost-all-dev libyaml-cpp-dev

  say "카메라 (RealSense D455)"
  note "librealsense SDK 를 따로 빌드했더라도 ROS 래퍼는 있어야 토픽이 나온다."
  apt_install \
    "ros-$ROS-realsense2-camera" "ros-$ROS-realsense2-description"

fi

# ---------------------------------------------------------------------------
# 관제 PC — Qt
# ---------------------------------------------------------------------------
if [ "$ROLE" = dev ] || [ "$ROLE" = station ]; then
  say "관제 HMI (Qt6)"
  note "HMI 는 ROS 를 쓰지 않는 Qt 프로그램이다. colcon 이 아니라 CMake 로 짓는다."
  apt_install qt6-base-dev qt6-base-dev-tools qt6-svg-dev ninja-build
fi

# ---------------------------------------------------------------------------
# 시뮬레이터 — 실기 없이 관제·주행을 돌리기 위한 것
# ---------------------------------------------------------------------------
if [ "$ROLE" = dev ]; then
  say "시뮬레이터 (MuJoCo)"
  VENV="$WORKSPACE_ROOT/.venv-b2sim"
  note "apt 가 관리하는 시스템 Python 을 건드리지 않도록 전용 venv 에 넣는다."
  apt_install python3-venv
  if [ -x "$VENV/bin/pip" ]; then
    note "이미 있음: $VENV"
  else
    # ensurepip 누락으로 디렉터리만 남은 경우에도 venv를 다시 완성한다.
    run python3 -m venv --system-site-packages "$VENV"
  fi
  run "$VENV/bin/pip" install --quiet mujoco onnxruntime
fi

# ---------------------------------------------------------------------------
# rosdep
# ---------------------------------------------------------------------------
say "rosdep"
# rosdep must see ROS_DISTRO in a non-interactive shell.  A fresh Jetson runs
# this script before the user has opened a new terminal, so source it here
# rather than relying on ~/.bashrc.
if [ -f "/opt/ros/$ROS/setup.bash" ]; then
  # shellcheck disable=SC1090
  # ROS's generated setup script reads optional variables directly; suspend
  # nounset just while sourcing it, then restore this script's strict mode.
  set +u
  source "/opt/ros/$ROS/setup.bash"
  set -u
else
  echo "ROS 환경을 찾지 못함: /opt/ros/$ROS/setup.bash" >&2
  exit 1
fi

if [ -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
  note "이미 초기화됨"
else
  run sudo rosdep init
fi
run rosdep update

if [ -d "$REPO_ROOT" ]; then
  say "워크스페이스 의존성 해석"
  run rosdep install --from-paths "$REPO_ROOT" --ignore-src -r -y \
    --skip-keys "unitree_go unitree_api"
fi

say "ROS 워크스페이스 빌드"
# colcon과 CMake가 각각 CPU 수만큼 병렬 작업을 만들면 메모리가 고갈된다.
# 패키지는 최대 두 개, 각 패키지 내부 컴파일은 최대 두 개로 제한한다.
if [ "$DRY_RUN" = 1 ]; then
  note "[dry-run] cd $WORKSPACE_ROOT && MAKEFLAGS=-j2 colcon build --executor parallel --parallel-workers 2 --base-paths src/shalom --symlink-install"
else
  (
    cd "$WORKSPACE_ROOT"
    MAKEFLAGS=-j2 colcon build --executor parallel --parallel-workers 2 --base-paths src/shalom --symlink-install
  )
fi

say "관제 HMI 빌드"
if [ "$DRY_RUN" = 1 ]; then
  note "[dry-run] cd $REPO_ROOT/hmi && cmake --preset default && cmake --build --preset default --parallel 4"
else
  (
    cd "$REPO_ROOT/hmi"
    cmake --preset default
    cmake --build --preset default --parallel 4
  )
fi

say "완료"
note "현재 셸에서 실행 환경을 불러온 뒤 로봇을 기동한다:"
note "  source /opt/ros/$ROS/setup.bash && source $WORKSPACE_ROOT/install/setup.bash"
note "  ros2 launch robot_bringup bringup.launch.py network_interface:=<B2-NIC> maps_dir:=/var/lib/shalom/maps map:=latest"
