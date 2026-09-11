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
  https://github.com/Wego-Robot-Engeering-Team/shalom.git shalom
```

`main`은 초기 저장소다. 로봇 소스는 `dev`에 있다.

## 2. 의존성 설치

```bash
cd ~/shalom_ws/src/shalom
./scripts/install.sh --role robot
```

ROS 2 Jazzy, Nav2, SLAM, RealSense 래퍼를 설치하고, `sources.repos`의 B2
드라이버·apt에 없는 ROS 패키지·RealSense SDK 소스를 검증된 커밋으로 받는다.
D4xx USB UDEV 규칙도 함께 설치한다.

`--role`은 `robot`(로봇) / `station`(관제 PC) / `dev`(둘 다 + 시뮬레이터)다.
로봇에 Qt를, 관제 PC에 RealSense 드라이버를 깔지 않기 위해 나눈다.

무엇을 할지 먼저 보려면 `--dry-run`을 붙인다.

## 3. 빌드

```bash
cd ~/shalom_ws
source /opt/ros/jazzy/setup.bash
colcon build --symlink-install --packages-skip inspection_hmi
source install/setup.bash
```

`inspection_hmi`는 관제 PC에서만 쓰므로 로봇에서는 건너뛴다.

확인:

```bash
ros2 pkg list | grep -E 'bringup|hmi_bridge|realsense_d455|velodyne_vlp16|aurora'
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
ros2 launch bringup bringup.launch.py robot:=real use_sim_time:=false \
  robot_id:=R1 robot_name:=1호기 lidar:=vlp16

# 로봇 없이 센서·관제 연동만 시험
ros2 launch bringup bringup.launch.py robot:=none use_sim_time:=false \
  robot_id:=R1 robot_name:=1호기 lidar:=vlp16
```

`robot:=none`은 로봇 계층 대신 정지 오도메트리를 올려 `odom → base_link`를
채운다. 그 한 변이 비면 TF가 끊겨 자세가 읽히지 않고 촬영이 거부된다.

로봇마다 `robot_id`를 다르게 준다. 관제가 이 값으로 엉뚱한 로봇에 보낸 명령을
거른다. 여러 대를 같은 망에 올릴 때는 `domain_id`도 다르게 준다.

종료:

```bash
~/shalom_ws/src/shalom/robot/tools/stop_stack.sh
```

## 7. 확인

```bash
source /opt/ros/jazzy/setup.bash && source ~/shalom_ws/install/setup.bash
export CYCLONEDDS_URI=file://$HOME/shalom_ws/install/bringup/share/bringup/config/cyclonedds.xml

ros2 topic hz /b2/points                  # 라이다 10 Hz
ros2 topic hz /fr3/camera_2d/image_raw    # 카메라
ros2 run tf2_ros tf2_echo map base_link   # 위치추정
ss -ltn | grep 9090                       # 관제 브릿지
```

`CYCLONEDDS_URI`를 주지 않으면 도구가 스택과 다른 DDS 설정으로 떠서 토픽이
보이지 않는다. 런치는 이 값을 스스로 설정하지만 별도로 띄운 셸은 그렇지 않다.

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

납품 구성은 `--preset release`다. 내장 모형이 빠지고 코어를 공유
라이브러리(`libhmi_core.so`)로 낸다.

실행하면 로그인 창이 먼저 뜬다. 지금은 자리표시 자격증명(`admin` / `admin`)이고,
입력한 이름이 조작 이력에 남는다.

---

## 문제 해결

**`colcon`이 `catkin_pkg`를 못 찾는다**
CMake가 시스템이 아닌 다른 Python을 잡은 것이다. `~/.local/bin`에 다른
Python이 있으면 그렇게 된다.

```bash
PATH="/usr/bin:/bin:$PATH" colcon build --symlink-install \
  --cmake-args -DPython3_EXECUTABLE=/usr/bin/python3.12
```

**패키지 이름을 바꾼 뒤 유령 패키지가 남는다**
`--symlink-install`이라 옛 이름의 심링크가 끊긴 채 남는다. 해당
`build/<이름>`과 `install/<이름>`을 지우고 다시 빌드한다.

**옮긴 패키지가 `does not match the source` 로 죽는다**
빌드 캐시가 옛 경로를 붙들고 있다. 그 패키지의 `build/` 디렉터리를 지운다.

**관제가 붙었는데 아무 것도 안 온다**
브릿지는 관제를 한 대만 받고 두 번째 연결은 즉시 닫는다. 앞서 띄운 HMI가
살아 있는지 본다.

```bash
ss -tn | grep 9090
```

**촬영이 "이동 중"으로 거부된다**
오도메트리가 끊기면 움직이는 것으로 본다. `map → base_link`가 서는지 본다.
로봇 없이 시험 중이면 `robot:=none`으로 띄웠는지 확인한다.
