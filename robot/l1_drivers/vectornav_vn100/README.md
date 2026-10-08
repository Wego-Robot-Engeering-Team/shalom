# VN-100 IMU

VN-100의 가속도·각속도를 ROS 2로 전달하는 패키지다. 센서 통신은
[`vectornav_driver`](../../third_party/vectornav/vectornav_driver)가 담당한다.

## 실행

```bash
source /opt/ros/jazzy/setup.bash
source ~/shalom_ws/install/setup.bash
ros2 launch vectornav_vn100 vn100.launch.py       # 센서
# 또는
ros2 launch vectornav_vn100 vn100.launch.py rviz:=true  # 센서 + 자동차 자세 RViz
```

센서가 이미 실행 중이면 `rviz:=true start_sensor:=false`로 시각화만 실행한다.
포트 변경은 `port:=/dev/ttyUSB0`, 실기 bringup에서 활성화는 `vn100:=true`를 사용한다.

## 데이터 흐름

```text
VN-100 → vectornav_driver → imu_adapter → /vn100/imu/data_ned
```

드라이버는 시리얼 Binary Output 1로 가속도·각속도·자기장·온도·기압을 요청한다.
Binary Output 2·3은 비활성화한다. 어댑터는 IMU 메시지를 복사하고 자세 필드를
미제공 상태로 설정한다. 측정값·타임스탬프·프레임은 유지한다.

| 토픽 | 메시지 | 내용 |
| --- | --- | --- |
| `/vn100/imu/data_ned` | `sensor_msgs/Imu` | 로봇에서 사용할 가속도·각속도 |
| `/vn100/vectornav_driver_node/imu/data` | `sensor_msgs/Imu` | 드라이버의 원본 IMU 출력 |
| `/vn100/vectornav_driver_node/imu/mag` | `sensor_msgs/MagneticField` | 자기장, T |
| `/vn100/vectornav_driver_node/temperature` | `sensor_msgs/Temperature` | 온도, °C |
| `/vn100/vectornav_driver_node/pressure` | `sensor_msgs/FluidPressure` | 기압, Pa |

드라이버는 구독자가 있고 해당 측정값이 수신될 때 토픽을 발행한다.
어댑터 입출력 QoS는 `SensorDataQoS`로, Best Effort·Volatile·깊이 5다.

## IMU 메시지

| 필드 | 단위·형식 | 현재 동작 |
| --- | --- | --- |
| `header.frame_id` | string | `vn100_link_ned` |
| `header.stamp` | ROS time | 호스트가 패킷을 수신한 시각 |
| `linear_acceleration` | m/s² | 정지 시 중력에 대응하는 성분을 포함한 가속도 |
| `angular_velocity` | rad/s | 센서 각 축의 각속도 |
| `orientation` | quaternion | `[x,y,z,w] = [0,0,0,1]`로 채움 |
| `orientation_covariance[0]` | float64 | `-1`: 자세 미제공 |
| 가속도·각속도 covariance | float64[9] | 현재 기본값은 모두 0: 공분산 미설정 |

가속도·각속도는 센서 보정이 적용된 측정값이며, 온보드 필터의 동적 바이어스
보상은 적용되지 않는다. 공분산은 드라이버의 `linear_acceleration_stddev`,
`angular_velocity_stddev`를 설정하면 각 표준편차의 제곱이 대각 원소에 들어간다.

## 좌표계와 로봇 장착

측정값은 센서 몸체 축 기준이다. 현재 시각화는 센서 축을 전방·우측·아래
방향으로 가정하고, `(x, -y, -z)`로 전방·좌측·위 방향으로 바꿔 표시한다.
`vn100_link_ned`라는 프레임 이름 자체는 데이터에 회전을 적용하지 않는다.

로봇에 연결할 때는 실제 장착 방향을 확인해 `base_link`와 센서 프레임 사이의
TF를 정의한다. 소비 노드에서 변환할 때 가속도·각속도와 공분산을 함께 변환한다.
공분산의 회전식은 `C_out = R × C_in × Rᵀ`다.

`set_reference_frame: false`는 기동 시 센서 내부 축 변환 설정을 변경하지 않는다는
뜻이다. 센서에 이전 설정이 남아 있다면 설치 방향과 함께 확인한다.

## 통신 설정

설정 파일: [`config/vn100.yaml`](config/vn100.yaml)

| 항목 | 현재 값 | 의미 |
| --- | --- | --- |
| `sensor_family` | `1` | VN-100 계열 |
| `baud_rate` | `921600` | 시리얼 통신 속도, bit/s |
| `async_mode` | `1` | 센서 Port 1 출력 |
| `async_rate_divisor` | `4` | 기본 800 Hz 샘플링 기준 출력 설정 200 Hz |
| `adjust_timestamp` | `false` | 호스트 수신 시각 사용 |
| `set_reference_frame` | `false` | 센서 내부 축 변환 설정 유지 |
| `write_to_flash` | `false` | 기동 마지막에 설정을 Flash에 저장하는 호출 생략 |
| `factory_reset_before_start` | `false` | 기동 시 공장 초기화 생략 |

기본 장치 경로는 `/dev/serial/by-id/usb-FTDI_USB-RS232-WE_AV0LFM92-if00-port0`이다.
포트는 launch의 `port` 인자가 YAML 값보다 우선한다. 실행 계정에 시리얼 장치
접근 권한이 필요하다. 실제 출력 주기는 다음 명령으로 확인한다.

```bash
ros2 topic hz /vn100/imu/data_ned
ros2 topic echo /vn100/imu/data_ned --once --qos-reliability best_effort
```

LiDAR 등 다른 센서와 결합할 때는 타임스탬프 기준을 맞춘다.
`adjust_timestamp: true`는 센서 시작 시각을 이용한 호스트 시간 보정을 활성화한다.
센서 간 하드웨어 동기화는 별도의 SyncIn·SyncOut 설정으로 구성한다.

## 자세 출력 확장

VN-100 온보드 필터 자세를 사용하려면 드라이버의 Binary Output 설정에
quaternion·보상 가속도·보상 각속도를 추가한다. 현재 설정은 `ATTITUDEGROUP_NONE`이며,
`filter/data` 발행 조건에 필요한 필드를 요청하지 않는다.

드라이버는 세 필드를 모두 받으면 `/vn100/vectornav_driver_node/filter/data`를
발행한다. 자세를 로봇에 전달할 때는 어댑터의 입력과 자세 처리도 함께 변경해야 한다.
현재 어댑터는 입력 메시지의 quaternion을 항상 `[0,0,0,1]`로 덮어쓴다.

## RViz 표시

자동차 모형은 X 전방·Y 좌측·Z 위쪽을 기준으로 표시한다. 흰 전조등이 있는 쪽이
차량 앞쪽(+X)이며, 뒤쪽에는 빨간 후미등이 있다. 현재 축 변환은 센서 +X가 차량
전방, +Y가 우측, +Z가 아래쪽으로 장착된 경우를 가정한다.
가속도에 저역통과 필터
`a_filtered += 0.15 × (a - a_filtered)`를 적용하고 다음 식으로 기울기를 계산한다.
`ax, ay, az`는 전방·좌측·위 방향으로 변환한 가속도다.

```text
roll  = atan2(ay, az)
pitch = atan2(-ax, sqrt(ay² + az²))
```

화면 각도는 degree로 표시한다. 정지 상태에서 기울기를 확인하는 용도이며,
센서를 이동시키면 선형가속도가 계산값에 영향을 준다.
시각화 결과는 `/vn100/attitude_markers`로만 발행한다.

## 수정할 파일

| 파일 | 역할 |
| --- | --- |
| [`src/imu_adapter.cpp`](src/imu_adapter.cpp) | 로봇용 IMU 메시지 처리 |
| [`config/vn100.yaml`](config/vn100.yaml) | 통신·시간·센서 설정 |
| [`launch/vn100.launch.py`](launch/vn100.launch.py) | 드라이버·어댑터·선택 RViz 실행 |
| [`src/attitude_visualizer.cpp`](src/attitude_visualizer.cpp) | 기울기 계산·RViz 모형 |
| [`rviz/vn100.rviz`](rviz/vn100.rviz) | RViz 화면 설정 |
| [`vectornav_driver.cpp`](../../third_party/vectornav/vectornav_driver/src/vectornav_driver.cpp) | 센서 패킷 설정·ROS 메시지 발행 |
