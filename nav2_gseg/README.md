# Nav2 Ground Consistency Layer 데모

ROS 2 Jazzy에서 3D LiDAR 지면 분할(GSeg3D), KISS-ICP, Nav2 Ground Consistency
costmap layer를 빌드하고 실행하는 워크스페이스다.

## macOS Apple Silicon (Conda / RoboStack)

### 1. Jazzy 환경 만들기

Humble과 섞지 않고 전용 Conda 환경을 사용한다.

```zsh
conda create -n ros2-jazzy --override-channels \
  -c conda-forge -c robostack-jazzy \
  ros-jazzy-desktop \
  ros-jazzy-navigation2 \
  ros-jazzy-nav2-bringup \
  ros-jazzy-ros-gz-sim \
  ros-jazzy-ros-gz-bridge \
  ros-jazzy-ros-gz-interfaces \
  ros-jazzy-pcl-ros \
  ros-jazzy-perception-pcl \
  ros-jazzy-pointcloud-to-laserscan \
  ros-jazzy-slam-toolbox \
  ros-jazzy-sdformat-urdf \
  ros-jazzy-xacro \
  sophus nanoflann tsl_robin_map \
  vcstool colcon-common-extensions rosdep

conda activate ros2-jazzy
```

> `ros-jazzy-sophus` 패키지는 RoboStack에 없다. 이 프로젝트에는 Conda Forge의
> `sophus`를 사용한다.

### 2. 소스 가져오기

```zsh
cd /Users/juno/wego/3d_navigation
mkdir -p src

git clone -b jazzy https://github.com/ros-navigation/navigation2_tutorials.git src/navigation2_tutorials
vcs import src < src/navigation2_tutorials/nav2_lidar_ground_segmentation_demo/dependencies.repos
```

### 3. 빌드

macOS에서는 이 데모에 `rosdep install`을 실행하지 않는다. `rosdep`의 Conda 매핑이
데모와 무관한 튜토리얼 패키지까지 해석해 오류를 낼 수 있다. 필요한 패키지는 1단계에서
이미 Conda로 설치했다.

```zsh
conda activate ros2-jazzy
cd /Users/juno/wego/3d_navigation

colcon build --symlink-install \
  --packages-up-to nav2_lidar_ground_segmentation_demo robot_sim_tools \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

### 4. 실행

macOS의 Gazebo Sim은 서버와 GUI를 한 명령으로 동시에 실행할 수 없다. 첫 번째 터미널은
시뮬레이션 서버와 ROS 2/Nav2/RViz를 실행하고, 두 번째 터미널은 Gazebo GUI를 연결한다.

**터미널 1 — 서버, Nav2, RViz2**

```zsh
conda activate ros2-jazzy
cd /Users/juno/wego/3d_navigation
source "$CONDA_PREFIX/setup.zsh"
source install/setup.zsh
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ros2 daemon stop

ros2 launch nav2_lidar_ground_segmentation_demo lidar_ground_segmentation_demo.launch.py
```

**터미널 2 — Gazebo GUI**

```zsh
conda activate ros2-jazzy
gz sim -g --gui-config \
  /Users/juno/wego/3d_navigation/install/nav2_lidar_ground_segmentation_demo/share/nav2_lidar_ground_segmentation_demo/config/gazebo_gui.config \
  --render-engine-gui-api-backend metal \
  --force-version 8
```

종료는 터미널 1에서 `Ctrl+C`를 누른 뒤, 터미널 2의 Gazebo GUI를 닫는다.

### 5. 장애물만으로 2D SLAM 맵 만들기

기본 데모 대신 아래 명령 하나로 실행한다. 지면 분할 결과 중 장애물만 `/obstacle_scan`으로
투영하고, `slam_toolbox`가 `/map` 및 `map → odom` 변환을 만든다. 따라서 기본 데모를 이미
실행 중이면 먼저 `Ctrl+C`로 종료해야 한다.

```zsh
conda activate ros2-jazzy
cd /Users/juno/wego/3d_navigation
source "$CONDA_PREFIX/setup.zsh"
source install/setup.zsh
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
ros2 daemon stop

ros2 launch robot_sim_tools slam.launch.py
```

RViz의 **SLAM Map**에서 `/map`을 볼 수 있다. 로봇이 이동하면서 맵이 채워지며, 고정 장애물은
맵에 남고 움직이는 물체는 잔상을 만들 수 있다. SLAM 입력은 각 위치의 `ground_points`를
기준으로 지면 위 `0.15 ~ 1.20 m`인 장애물만 사용하므로, 언덕의 절대 높이와 무관하게 높은
나뭇잎·수관은 제외된다. 이 범위는
`src/robot_sim_tools/config/ground_filter.yaml`에서 조정한다.

### 6. Python GUI로 로봇 조종

시뮬레이터가 실행 중인 별도 터미널에서 다음 명령을 실행한다.

```zsh
conda activate ros2-jazzy
cd /Users/juno/wego/3d_navigation
source "$CONDA_PREFIX/setup.zsh"
source install/setup.zsh

ros2 run robot_sim_tools control_gui
```

방향 버튼은 누르고 있는 동안에만 움직이고, 떼면 즉시 정지한다. 키보드 `W` / `A` /
`S` / `D`와 스페이스바 정지도 지원한다. 기본 `/cmd_vel` 대신 다른 로봇의 토픽을
사용하려면 `--topic`을 지정한다.

```zsh
ros2 run robot_sim_tools control_gui --topic /go2/cmd_vel
```

이 인터페이스는 `geometry_msgs/Twist`를 발행한다. Nav2에 목표점을 보낸 상태에서는
Nav2도 같은 토픽을 사용하므로, 수동 조종 중에는 RViz의 Nav2 목표 전송을 멈춘다.
짧은 스크립트 조종은 다음처럼 실행한다.

```zsh
ros2 run robot_sim_tools drive forward --duration 2
```

## Ubuntu 24.04 (Linux)

먼저 [ROS 2 Jazzy 설치 문서](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html)에
따라 ROS apt 저장소를 설정한다.

### 1. ROS, Nav2, Gazebo 도구 설치

```bash
sudo apt update
sudo apt install -y \
  ros-jazzy-desktop \
  ros-jazzy-navigation2 \
  ros-jazzy-ros-gz \
  ros-dev-tools \
  python3-vcstool

# 시스템당 한 번만 실행
sudo rosdep init
rosdep update
```

### 2. 소스, 의존성, 빌드

```bash
mkdir -p ~/3d_navigation/src
cd ~/3d_navigation/src

git clone -b jazzy https://github.com/ros-navigation/navigation2_tutorials.git
vcs import . < navigation2_tutorials/nav2_lidar_ground_segmentation_demo/dependencies.repos

cd ..
source /opt/ros/jazzy/setup.bash
rosdep install --from-paths src --ignore-src --rosdistro jazzy -y

colcon build --symlink-install \
  --packages-up-to nav2_lidar_ground_segmentation_demo \
  --cmake-args -DCMAKE_BUILD_TYPE=Release
```

### 3. 실행

```bash
source /opt/ros/jazzy/setup.bash
cd ~/3d_navigation
source install/setup.bash

ros2 launch nav2_lidar_ground_segmentation_demo lidar_ground_segmentation_demo.launch.py
```
