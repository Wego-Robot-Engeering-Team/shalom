# HMI

Qt 6 기반 관제 GUI. 로봇과는 raw TCP로 통신하며 규약은
[bridge_protocol.md](../docs/bridge_protocol.md)를 따른다.

팔은 **FAIRINO FR3** 다. 같은 모델명을 쓰는 Franka Research 3 와는 다른 로봇으로,
이쪽은 6 축에 리치 622 mm, 자중 13.7 kg 이다 (Franka 는 7 축, 855 mm). 관절 한계와
링크 치수는 FAIRINO 가 공개한 `fairino3_v6.urdf`
(FAIR-INNOVATION/frcobot_ros2) 에서 그대로 가져왔고, `src/RobotDef.h` 한 곳에만 둔다 —
예전에는 같은 표가 기구학·자세검사·3D뷰 세 곳에 복제돼 있었다.
`test_kinematics` 가 정기구학 결과를 그 URDF 값과 직접 대조한다.

## 3D 자세 뷰

관제 화면의 3D 뷰는 실제 로봇 형상을 그린다. 다만 **OpenGL 이 아니라 소프트웨어
깊이 버퍼** 다 — 납품 장비는 GPU 드라이버를 알 수 없는 산업용 PC 이고, 관제 화면이
그것 하나 때문에 안 그려지면 안 된다 (`src/widgets/Robot3DView.h`).

예전에는 면을 카메라 거리로 정렬해 그렸다(화가 알고리즘). 그 방식은 서로 파고드는
형상을 못 그려서 각 마디를 볼록껍질로 뭉개야 했고, 화면의 로봇은 실제와 눈에 띄게
달랐다. 픽셀마다 깊이를 재면 그 제약이 사라진다. **더 느리지도 않다** — 정렬이
없어져서 납품 빌드 기준 640×480 한 프레임 7.6 ms 로, 볼록껍질 시절(7.9 ms)과 같다.

형상은 `tools/make_robot_mesh.py` 가 `resources/robot_mesh.bin` 으로 만든다.
Unitree 의 MJCF(B2)와 FAIRINO 의 URDF 메시(FR3)를 정점 격자 병합으로 59.5 만 면에서
1.6 만 면까지 줄인 것이다. 나란히 놓고 봐도 구분되지 않는다 — 이 크기에서는 한 면이
이미 픽셀보다 작다.

```bash
python3 tools/make_robot_mesh.py --fr3-meshes <fairino3_v6 STL 디렉터리>
```

격자는 부품별이 아니라 **로봇 전체에 하나** 다. 부품마다 자기 최대 치수로 나누면
가느다란 정강이(0.055 m 두께)가 0.07 m 격자를 받아 단면이 통째로 뭉개진다 —
화면에서 다리가 부서져 보인다.

B2 는 관절값이 프로토콜에 없으므로 `FixStand` 기립 자세로 구워 넣는다. 그 자세와
링크 위치는 MuJoCo 가 실제 MJCF 로 계산한 값이고, 정점도 MuJoCo 배열에서 읽는다 —
`.obj` 를 직접 읽으면 컴파일러가 메시를 관성 좌표계로 옮겨 놓은 것을 놓쳐 발이
16 cm 아래로 내려간다.

`test_robotview` 가 형상 적재, 면 예산, 발이 지면에 닿는지, 팔 밑동이 LiDAR 마스트를
피하는지, 프리셋이 경고 없이 도달하는지, 한 프레임이 16 ms 안에 그려지는지를 지킨다.

## 빌드·테스트

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`release` 프리셋은 로그인 필수이며 testbed를 포함하지 않는다.

## 실행

```bash
./build/inspection_hmi          # 개발용 testbed
./build/inspection_hmi --live   # 로봇 브릿지 연결
```

주요 경로: `src/` GUI, `testbed/` 개발용 mock, `tests/` 테스트,
`resources/` 자산.
