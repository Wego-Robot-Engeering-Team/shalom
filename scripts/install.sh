#!/usr/bin/env bash
#
# 개발·운용 환경을 한 번에 구성한다.
#
# 과업지시서 7.3 절이 임치 대상으로 "설치·환경구성 스크립트: install.sh 등
# 환경을 새로 구성할 때 필요한 설치 자동화 스크립트" 를 요구한다. 문서에
# 흩어진 명령을 사람이 순서대로 옮겨 치면 반드시 하나를 빠뜨리므로, 그
# 목록을 여기 한 곳에 둔다.
#
#   ./scripts/install.sh --role dev        모두 (기본값)
#   ./scripts/install.sh --role robot      로봇: ROS·주행·카메라·영상 송신
#   ./scripts/install.sh --role station    관제 PC: Qt·영상 수신
#   ./scripts/install.sh --role dev --dry-run
#
# 역할을 나누는 이유는 로봇에 Qt 가, 관제 PC 에 RealSense 드라이버가 필요
# 없기 때문이다. 안 쓰는 것을 깔아 두면 납품 시 의존성 목록(과업지시서
# 7.3)만 길어지고, 그만큼 라이선스 고지 대상도 늘어난다.

set -euo pipefail

ROLE=dev
DRY_RUN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --role) ROLE="${2:?--role 뒤에 dev|robot|station 이 와야 한다}"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "모르는 인자: $1" >&2; exit 2 ;;
  esac
done

case "$ROLE" in
  dev|robot|station) ;;
  *) echo "--role 은 dev, robot, station 중 하나여야 한다 (받은 값: $ROLE)" >&2; exit 2 ;;
esac

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

# ROS 2 packages are not in Ubuntu's standard archive.  Keep this here rather
# than making a freshly flashed Jetson rely on a developer having remembered a
# one-off, machine-global setup step from the ROS web site.
setup_ros_apt_source() {
  if [ -f /etc/apt/sources.list.d/ros2.list ]; then
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
  printf 'deb [arch=%s signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu %s main\n' \
    "$(dpkg --print-architecture)" "$VERSION_CODENAME" \
    | sudo tee /etc/apt/sources.list.d/ros2.list >/dev/null
}

# ---------------------------------------------------------------------------
# 환경 확인
#
# 과업지시서 0.5 절이 Ubuntu 22.04 + ROS 2 Humble 을 지정하고, 변경 시
# 발주기관의 서면 승인을 요구한다. 현재 개발은 24.04 + Jazzy 에서 하고
# 있으므로 그 사실을 여기서 눈에 띄게 알린다 — 조용히 넘어가면 검수
# 자리에서야 드러난다.
# ---------------------------------------------------------------------------
say "환경 확인"
. /etc/os-release
note "OS      $PRETTY_NAME"
ROS_DISTRO_FOUND="$(ls /opt/ros 2>/dev/null | head -1 || true)"
note "ROS     ${ROS_DISTRO_FOUND:-없음}"
note "역할    $ROLE"

if [ "${VERSION_CODENAME:-}" != "jammy" ] || [ "$ROS_DISTRO_FOUND" != "humble" ]; then
  note ""
  note "주의: 과업지시서 0.5 절의 지정 버전은 Ubuntu 22.04 + ROS 2 Humble 이다."
  note "      다른 조합으로 개발·납품하려면 발주기관의 서면 승인이 필요하다."
fi

ROS="${ROS_DISTRO_FOUND:-jazzy}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKSPACE="$(cd "$REPO_ROOT/../.." && pwd)"

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
  ros-dev-tools python3-vcstool python3-dev

# The application repository deliberately keeps vendor and third-party ROS
# packages outside itself.  Import their pinned revisions only after vcs has
# been installed above, so a clean Jetson needs just clone + this script.
if { [ "$ROLE" = dev ] || [ "$ROLE" = robot ]; } \
    && [ -f "$REPO_ROOT/sources.repos" ] \
    && [ ! -d "$WORKSPACE/src/b2_driver" ]; then
  say "로봇 소스 의존성"
  if [ "$DRY_RUN" = 1 ]; then
    note "vcs import < $REPO_ROOT/sources.repos"
  else
    (cd "$WORKSPACE/src" && vcs import < "$REPO_ROOT/sources.repos")
  fi
fi

# ---------------------------------------------------------------------------
# 로봇 — 주행, 인식, 카메라, 영상 송신
# ---------------------------------------------------------------------------
if [ "$ROLE" = dev ] || [ "$ROLE" = robot ]; then
  say "자율주행·SLAM"
  apt_install \
    "ros-$ROS-navigation2" "ros-$ROS-nav2-bringup" \
    "ros-$ROS-slam-toolbox" "ros-$ROS-pointcloud-to-laserscan" \
    "ros-$ROS-rosidl-generator-dds-idl"

  say "카메라 (RealSense D455)"
  note "librealsense SDK 를 따로 빌드했더라도 ROS 래퍼는 있어야 토픽이 나온다."
  apt_install \
    "ros-$ROS-realsense2-camera" "ros-$ROS-realsense2-description"

  say "영상 송신 (RTSP/H.264)"
  note "인코딩은 젯슨 NVENC(JetPack 동봉)로 한다. x264 는 깔지 않는다 —"
  note "libx264 가 GPL-2+ 라 납품 파이프라인에 들어가면 안 된다."
  apt_install \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    libgstrtspserver-1.0-dev "gstreamer1.0-rtsp" gstreamer1.0-plugins-good
fi

# ---------------------------------------------------------------------------
# 관제 PC — Qt 와 영상 수신
# ---------------------------------------------------------------------------
if [ "$ROLE" = dev ] || [ "$ROLE" = station ]; then
  say "관제 HMI (Qt6)"
  note "HMI 는 ROS 를 쓰지 않는 Qt 프로그램이다. colcon 이 아니라 CMake 로 짓는다."
  apt_install qt6-base-dev qt6-base-dev-tools qt6-svg-dev

  say "영상 수신 (소프트웨어 디코드)"
  note "avdec_h264 는 LGPL 이고 720p 15fps 에 충분하다. NVDEC 를 쓰면 납품 PC 의"
  note "GPU·드라이버 상태를 전제하게 되므로 쓰지 않는다."
  apt_install \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-libav gstreamer1.0-plugins-good
fi

# ---------------------------------------------------------------------------
# 시뮬레이터 — 실기 없이 관제·주행을 돌리기 위한 것
# ---------------------------------------------------------------------------
if [ "$ROLE" = dev ]; then
  say "시뮬레이터 (MuJoCo)"
  VENV="$HOME/shalom_ws/.venv-b2sim"
  note "apt 가 관리하는 시스템 Python 을 건드리지 않도록 전용 venv 에 넣는다."
  if [ -d "$VENV" ]; then
    note "이미 있음: $VENV"
  else
    run python3 -m venv --system-site-packages "$VENV"
  fi
  run "$VENV/bin/pip" install --quiet mujoco onnxruntime
fi

# ---------------------------------------------------------------------------
# rosdep
# ---------------------------------------------------------------------------
say "rosdep"
if [ -f /etc/ros/rosdep/sources.list.d/20-default.list ]; then
  note "이미 초기화됨"
else
  run sudo rosdep init
fi
run rosdep update

if [ -d "$WORKSPACE/src" ]; then
  say "워크스페이스 의존성 해석"
  run rosdep install --from-paths "$WORKSPACE/src" --ignore-src -r -y
fi

say "완료"
note "빌드:"
note "  cd ~/shalom_ws && colcon build --symlink-install"
if [ "$ROLE" = dev ] || [ "$ROLE" = station ]; then
  note "  cd ~/shalom_ws/src/shalom/hmi && cmake --preset dev && cmake --build --preset dev"
fi
note ""
note "colcon 이 catkin_pkg 를 못 찾는다고 하면 CMake 가 다른 Python 을 잡은 것이다:"
note "  PATH=\"/usr/bin:/bin:\$PATH\" colcon build --symlink-install \\"
note "    --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3.12"
