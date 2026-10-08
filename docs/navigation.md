# 내비게이션

Nav2가 경로를 계획하고 속도 명령을 낸다. FSM·BT·navigation·HMI bridge는
`robot/`의 공통 코드이며, 실기와 시뮬레이터가 동일하게 사용한다.

## 실행

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash

# 실기: B2와 현장 지도
ros2 launch robot_bringup bringup.launch.py \
  network_interface:=enp3s0 maps_dir:=/var/lib/shalom/maps map:=auto

# 시뮬레이터: MuJoCo와 저장된 지도
ros2 launch simulation_bringup bringup.launch.py viewer:=false map:=auto
```

실기에는 MuJoCo·VLP-16이 들어가지 않는다. 시뮬레이터의 플랫폼 구현은
`robot/third_party/b2_simulation/`에, 시뮬레이션 실행 조립과 지도는
`simulation/simulation_bringup/`에 있다.

드라이버가 이미 실행 중이면 내비게이션만 별도로 실행할 수 있다.

```bash
ros2 launch navigation navigation.launch.py pointcloud_topic:=/b2/points
```

시뮬 데이터에는 `use_sim_time:=true`, 저장 지도에는 `map:=<map.yaml 절대 경로>`를 지정한다.
`map:=auto`가 기본이며 `maps_dir:=<지도 폴더>`의 기본 지도를 사용한다.
실기·시뮬의 `bringup.launch.py`가 내비게이션·시스템 관리자·명령 선택·안전 게이트·HMI 통신을 직접 실행한다.

## 지도 전환

지도 번들은 아래처럼 지도 이미지와 그 지도에만 속하는 운용 상태를 함께 가진다.

```text
<maps_dir>/<map_id>/
├── map.yaml
├── map.pgm
├── metadata.json
├── waypoints.json
├── locations.json
└── markers.json
```

실기·시뮬 모두 `map:=auto`가 기본이며 `maps_dir/default_map.json`의 `map_id`를
`navigation.launch.py`가 읽고 지도 경로를 검증한다. 선택한 경로는 브리지에도
전달되어 같은 지도의 웨이포인트·미션을 불러온다.
기본 지도 설정 파일이 없거나 ID가 비어 있으면 SLAM으로 시작한다. 설정된 지도 파일이
없거나 설정 형식이 잘못되면 기동을 중단한다. 명시적으로
SLAM을 선택하려면 `map:=none`을 쓴다. HMI에서 SLAM 중 저장 지도를 고르면
SLAM을 비활성화하고 map_server·AMCL을 활성화해 해당 지도를 불러온다.
주행·미션 중에는 전환 요청이 거절된다.

## 구성

플래너는 `robot/l3_control/navigation/config/nav2.yaml`의 NavFn이고, 컨트롤러는 MPPI다.
local costmap은 GroundConsistencyLayer가 지면·장애물 점군을 받아 구성하며,
global costmap은 저장 지도 또는 SLAM의 `/map`을 사용한다.
