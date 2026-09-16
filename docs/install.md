# 설치 절차

Jetson Orin(AGX / Nano)에 JetPack 7.2.1(Ubuntu 24.04)을 플래시한 직후부터,
로봇 프로그램이 도는 상태까지의 절차다. 관제 PC 설치는 마지막 절에 있다.

Nano용 SSD를 AGX로 옮겨 부팅하지 않는다. 보드마다 자기 보드 대상으로
JetPack을 플래시한 뒤 이 문서를 각각 수행한다.

---

## 1. 저장소 받기

인터넷 연결이 필요하다.

```bash
mkdir -p ~/shalom_ws/src && cd ~/shalom_ws/src
git clone --branch dev --single-branch \
  --recurse-submodules \
  https://github.com/Wego-Robot-Engeering-Team/shalom.git shalom
```

`main`은 초기 저장소다. 로봇 소스는 `dev`에 있다. 이미 일반 클론을 했다면
아래 명령으로 B2·FR3와 나머지 소스 의존성을 받는다.

```bash
git -C ~/shalom_ws/src/shalom submodule update --init --recursive
```

## 2. 의존성 설치

```bash
cd ~/shalom_ws/src/shalom
./scripts/install.sh --role robot
```

ROS 2 Jazzy, Nav2, SLAM, RealSense 래퍼와 Pandar XT32 드라이버의 시스템
의존성(Boost, yaml-cpp)을 설치하고, `third_party/`의 재귀 서브모듈을 검증된
커밋으로 맞춘다. D4xx USB UDEV 규칙도 함께 설치한다.

`--role`은 `robot`(로봇) / `station`(관제 PC) / `dev`(둘 다 + 시뮬레이터)다.
로봇에 Qt를, 관제 PC에 RealSense 드라이버를 깔지 않기 위해 나눈다.

무엇을 할지 먼저 보려면 `--dry-run`을 붙인다.

## 3. 빌드

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --base-paths src/shalom --symlink-install --packages-skip inspection_hmi
source install/setup.bash
```

`inspection_hmi`는 관제 PC에서만 쓰므로 로봇에서는 건너뛴다.

확인:

```bash
ros2 pkg list | grep -E 'robot_bringup|hmi_bridge|realsense_d455|pandar_xt32|hesai_ros_driver|slamtec_aurora'
```

## 4. 전원 모드 (AGX)

AGX Orin은 기본이 `MODE_30W`다. 인식·SLAM을 함께 돌리면 모자라므로 올린다.

```bash
sudo nvpmodel -m 0      # MAXN
sudo nvpmodel -q        # 확인
```

| ID | 모드 |
|---:|---|
| 0 | MAXN |
| 1 | 15W |
| 2 | 30W (기본) |
| 3 | 50W |

Nano는 해당 없다.

---

## 5. 센서 연결

### RealSense D455

USB 3.0에 연결하고 인식을 확인한다.

```bash
lsusb | grep 8086:0b5c        # Intel RealSense Depth Camera 455
realsense-viewer              # RGB·Depth 스트림 확인
```

권한 경고가 보이면 UDEV 규칙이 막 설치된 것이다. USB를 한 번 뺐다 꽂는다.

### VLP-16 (임시 시험용 라이다)

전용 USB 랜 어댑터로 연결한다. **라이다는 DHCP를 주지 않으므로 고정 IP가
필요하다.** 이것을 빠뜨리면 인터페이스가 `activating`에서 멈추고, 드라이버는
`poll() timeout`만 남긴다.

```bash
# 인터페이스 이름 확인 (예: enx00e099010cbc)
ip -br link show | grep -E '^en'

# DHCP를 기다리는 기본 프로필을 지우고 고정 주소를 준다
sudo nmcli con delete "Wired connection 2"
sudo nmcli con add type ethernet con-name vlp16 ifname <인터페이스> \
  ipv4.method manual ipv4.addresses 192.168.1.100/24 \
  ipv4.never-default yes ipv6.method disabled connection.autoconnect yes
sudo nmcli con up vlp16
```

`never-default`를 주는 이유는 이 망이 인터넷 경로가 되면 안 되기 때문이다.

확인:

```bash
ip -br -4 addr show <인터페이스>              # 192.168.1.100/24
sudo tcpdump -c 3 -i <인터페이스> udp port 2368
```

패킷이 없으면 라이다 전원과 케이블을 먼저 본다. IP만 있고 패킷이 없는 상태와
IP가 없는 상태는 증상이 같다.

### Pandar XT32 (실기 외장 라이다)

프로젝트에는 공식 Hesai ROS 2 드라이버를 `third_party/hesai_lidar_ros2`에
고정해 두었다. 설치 스크립트가 그 드라이버가 요구하는 `libboost-all-dev`와
`libyaml-cpp-dev`를 설치하므로, 별도로 Git clone하거나 Hesai의 예제 launch를
실행하지 않는다.

Pandar의 장치 IP, 데이터 UDP 포트, PTC 포트는 **장비의 웹 설정값이 기준**이다.
아래에서 `<...>`을 실제 값으로 바꾼다. `192.168.1.201`, UDP `2368`, PTC `9347`은
Hesai 예제에 쓰이는 흔한 값일 뿐, 이 로봇의 확정값이 아니다.

```bash
# LiDAR 전용 NIC를 같은 서브넷으로 설정한다. 기본 인터넷 경로는 가져가지 않는다.
sudo nmcli con add type ethernet con-name pandar-xt32 ifname <인터페이스> \
  ipv4.method manual ipv4.addresses <호스트-IP>/<prefix> \
  ipv4.never-default yes ipv6.method disabled connection.autoconnect yes
sudo nmcli con up pandar-xt32

# 장치 웹 설정의 데이터 포트로 패킷이 오는지 확인
sudo tcpdump -c 3 -i <인터페이스> udp port <데이터-UDP-포트>
```

기본 템플릿은 설치 후
`~/shalom_ws/install/pandar_xt32/share/pandar_xt32/config/xt32.yaml`에 있다.
이를 로봇별 파일로 복사하여 `device_ip_address`, `udp_port`, `ptc_port`, 보정 정책을
장치 설정과 맞춘다. 드라이버가 PTC에서 보정값을 받는 구성을 쓰지 않는다면
`use_ptc_connected`와 `correction_file_path`도 그 설치 방식에 맞춰 함께 바꾼다.

```bash
sudo install -d -m 755 /etc/shalom
sudo cp ~/shalom_ws/install/pandar_xt32/share/pandar_xt32/config/xt32.yaml \
  /etc/shalom/pandar_xt32.yaml
sudoedit /etc/shalom/pandar_xt32.yaml
```

장착 위치와 자세는 반드시 실측한다. 아래 `xt32_*` 값은 예시이며, `base_link →
pandar_xt32` 정적 TF를 설정한다.

### Aurora S

전용 유선 포트에 연결한다. 장치 주소는 `192.168.11.1`이다.

```bash
ping -c 2 192.168.11.1
```

---

## 6. 실행

```bash
cd ~/shalom_ws && source install/setup.bash

# 실기
ros2 launch robot_bringup inspection.launch.py robot:=real use_sim_time:=false \
  robot_id:=R1 robot_name:=1호기 lidar:=xt32 \
  xt32_config_file:=/etc/shalom/pandar_xt32.yaml

```

로봇마다 `robot_id`를 다르게 준다. 관제가 이 값으로 엉뚱한 로봇에 보낸 명령을
거른다. 여러 대를 같은 망에 올릴 때는 `domain_id`도 다르게 준다.

종료:

```bash
~/shalom_ws/src/shalom/robot/tools/stop_stack.sh
```

## 7. 확인

```bash
source /opt/ros/jazzy/setup.bash && source ~/shalom_ws/install/setup.bash
export CYCLONEDDS_URI=file://$HOME/shalom_ws/install/robot_bringup/share/robot_bringup/config/cyclonedds.xml

ros2 topic hz /b2/points                  # Pandar 설정에 맞는 라이다 주기
ros2 run tf2_ros tf2_echo base_link pandar_xt32
ros2 topic hz /fr3/camera_2d/image_raw    # 카메라
ros2 run tf2_ros tf2_echo map base_link   # 위치추정
ss -ltn | grep 9090                       # 관제 브릿지
```

`RMW_IMPLEMENTATION`과 `CYCLONEDDS_URI`를 주지 않으면 도구가 스택과 다른 DDS로
떠서 토픽이 보이지 않는다. 런치는 두 값을 스스로 설정하지만 별도로 띄운 셸은
그렇지 않다.

```bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
```

### 외부 통신 차단 확인 (과업지시서 7.1)

로봇의 ROS 2 통신은 루프백에 가둔다. 밖으로 나가는 것은 관제 TCP(9090)뿐이다.
같은 망의 다른 PC에서 확인한다.

```bash
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ROS_DOMAIN_ID=0
ros2 daemon stop && ros2 node list      # 아무것도 나오지 않아야 한다
```

`ros2 daemon stop`을 빼면 이전에 발견한 노드가 캐시에서 나와 샌 것처럼 보인다.

관제 PC에서 붙어 확인한다.

```bash
inspection_hmi --host <로봇-IP>
```

---

## 8. 관제 PC (Ubuntu)

```bash
cd ~/shalom_ws/src/shalom
./scripts/install.sh --role station

cd hmi
cmake --preset dev && cmake --build --preset dev
ctest --preset dev
./build/inspection_hmi
```

납품 구성은 `--preset release`다. 내장 모형이 빠지고 실행 파일 하나로 나온다.

실행하면 로그인 창이 먼저 뜬다. 지금은 자리표시 자격증명(`admin` / `admin`)이고,
입력한 이름이 조작 이력에 남는다.

---

## 문제 해결

**`colcon`이 `catkin_pkg`를 못 찾는다**
CMake가 시스템이 아닌 다른 Python을 잡은 것이다. `~/.local/bin`에 다른
Python이 있으면 그렇게 된다.

```bash
PATH="/usr/bin:/bin:$PATH" colcon build --base-paths src/shalom --symlink-install \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3.12
```

**패키지 이름을 바꾼 뒤 유령 패키지가 남는다**
`--symlink-install`이라 옛 이름의 심링크가 끊긴 채 남는다. 해당
`build/<이름>`과 `install/<이름>`을 지우고 다시 빌드한다.

**`Duplicate package names` 로 빌드가 시작조차 안 된다**
서브모듈로 옮기기 전 레이아웃이 남아 있는 워크스페이스다. `src/third_party/`,
`src/b2_driver/`, `src/b2_simulation/` 의 옛 복사본이 `src/shalom/third_party/`
의 서브모듈과 같은 패키지를 두 번 제공한다. 서브모듈을 받은 뒤 옛 복사본을
지운다.

```bash
cd ~/shalom_ws
git -C src/shalom submodule update --init --recursive
rm -rf src/third_party src/b2_driver src/b2_simulation
rm -rf build install log
colcon build --base-paths src/shalom --symlink-install
```

**옮긴 패키지가 `does not match the source` 로 죽는다**
빌드 캐시가 옛 경로를 붙들고 있다. 그 패키지의 `build/` 디렉터리를 지운다.

**apt 로 ROS 를 올린 뒤 `No rule to make target ...so.<버전>` 으로 죽는다**
빌드 캐시에 라이브러리 경로가 버전까지 포함된 절대 경로로 박혀 있다. apt 가
그 라이브러리를 올리면 옛 파일이 사라져 빌드가 멈춘다.

```
gmake[2]: *** No rule to make target '/opt/ros/jazzy/lib/libfastcdr.so.2.2.7',
          needed by 'bridge_node'.  Stop.
```

오류가 가리키는 파일명으로 걸린 패키지를 모두 찾아 캐시를 지운다. 한 패키지만
지우면 다음 패키지에서 같은 오류가 이어진다.

```bash
cd ~/shalom_ws
SO=libfastcdr.so.2.2.7          # 오류 메시지의 파일명
PKGS=$(grep -rl "$SO" build/*/ 2>/dev/null | cut -d/ -f2 | sort -u)
echo $PKGS
for p in $PKGS; do rm -rf build/$p install/$p; done
colcon build --base-paths src/shalom --symlink-install
```

ROS 를 올릴 때마다 재발한다.

**관제가 붙었는데 아무 것도 안 온다**
브릿지는 관제를 한 대만 받고 두 번째 연결은 즉시 닫는다. 앞서 띄운 HMI가
살아 있는지 본다.

```bash
ss -tn | grep 9090
```

**촬영이 "이동 중"으로 거부된다**
오도메트리가 끊기면 움직이는 것으로 본다. `map → base_link`가 서는지 본다.
실기 또는 시뮬레이터에서 `map → base_link`가 정상인지 확인한다.
