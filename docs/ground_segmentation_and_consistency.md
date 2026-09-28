# GSeg3D 지면 분리와 Nav2 Ground Consistency Layer

## 개요

LiDAR의 한 스캔은 여러 표면에서 반사된 3차원 점들의 집합이다. 점 하나의 `(x, y, z)`만으로는 경사로, 상자, 벽, 천장 중 어디에 속하는지 알 수 없다. **GSeg3D**는 점들의 국소 형상과 지면 후보의 공간 관계를 분석해 지면 점과 비지면 점을 분리한다. **Ground Consistency Layer**는 두 결과를 Nav2의 2D local costmap 셀에 모아, 비지면이 주행을 막는지 판단한다. [GSeg3D 논문](https://arxiv.org/pdf/2603.04208), [Nav2 튜토리얼](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/navigation2_with_ground_consistency_layer/)

```text
PointCloud2 (+ 선택적 IMU 자세)
  → 좌표 변환 → 선택적 다운샘플링 → 관심 영역 자르기
  → GSeg3D 1차: 큰 높이의 3D 복셀
       ├─ 1차 비지면 ────────────────────────────────┐
       └─ 1차 지면 → GSeg3D 2차: 작은 높이의 3D 복셀  │
                       ├─ 최종 지면                  │
                       └─ 2차 비지면 ────────────────┤
                                                     ↓
                     ground_points / obstacle_points
                                     ↓
                       Ground Consistency Layer
                     2D 셀별 점수·지역 높이 판정
                                     ↓
                      Nav2 local costmap → Inflation
```

![분리 전 포인트 클라우드](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/assets/raw_points.png)

![분리 후 지면(초록색)과 비지면(자홍색)](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/assets/segmented_points.png)

GSeg3D의 셀은 `(x,y,z)`를 갖는 **3D 복셀**이고, Ground Consistency의 셀은 `(x,y)`를 갖는 **2D costmap 칸**이다. 전처리의 PCL `VoxelGrid`는 포인트 수를 줄이는 별도의 다운샘플링이다.

## 1. 입력 준비

### 좌표계와 점 필터링

ROS 2 래퍼는 `PointCloud2`를 `pcl::PointXYZ`로 변환한다. 메시지 시각의 TF로 LiDAR 프레임의 점을 `robot_frame`으로 옮기며, TF를 얻지 못하면 해당 스캔을 처리하지 않는다. `downsample=true`이면 PCL `VoxelGrid`로 점을 줄이고, 그 결과에 `minX/maxX` 등으로 지정된 CropBox를 적용한다. 코드의 순서는 **로봇 프레임 변환 → 선택적 다운샘플링 → 관심 영역 자르기**다. 출력 지면·비지면 클라우드는 원본 스캔의 시각과 `robot_frame`을 사용한다. [ROS 2 래퍼](https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L164-L300), [전처리 코드](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/pointcloud_processor.hpp#L54-L93)

`lidar_to_ground`는 LiDAR 원점에서 예상 지면까지의 **부호 있는 수직 거리**다. 래퍼는 LiDAR→로봇 TF의 Z 이동량을 더해 로봇 프레임의 예상 지면 높이 `distToGround`를 만든다. 이 값은 지면 후보 확장의 시작 위치를 정하는 데 쓰인다. [예상 지면 높이](https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L187-L201)

### IMU orientation

`use_imu_orientation=true`이면 점군과 IMU 메시지를 근사 시각 동기화한다. 래퍼는 `Imu.orientation`과 IMU↔로봇 TF를 조합해 로봇 자세 쿼터니언 `R`을 만든다. 분리 라이브러리는 **점 좌표가 아니라 추정한 선·평면의 방향 벡터에 `R`을 적용**해 중력축 대비 각도를 구한다. 따라서 이 구현에는 포인트 클라우드 전체를 중력 수평으로 회전하는 단계나 스캔 motion deskew가 없다. IMU를 사용하지 않으면 항등 회전을 사용해 로봇 프레임 Z축을 경사 기준으로 삼는다. [IMU 처리](https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L203-L255), [방향 벡터 회전](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L372-L390)

## 2. GSeg3D: 한 단계 안의 포인트 처리

1차와 2차는 입력 점과 복셀 높이가 다르지만 **복셀 배정 → 국소 형상 검사 → 지면 후보 확장 → 점별 출력** 절차를 공유한다. [논문 §III](https://arxiv.org/pdf/2603.04208), [핵심 구현](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp)

### 2.1 3D 복셀에 점 배정

점 `p=(x,y,z)`의 복셀 인덱스는 축별로 `floor(좌표 / 셀 크기)`를 적용한 값이다. 예를 들어 `cellSizeX=1 m`이면 `x=2.4 m`와 `x=2.8 m`는 X 인덱스 2를 공유한다. Y와 Z에도 같은 규칙을 적용한다. **원래 점들은 복셀의 포인트 목록에 그대로 보관**되므로, 격자화 자체는 포인트를 대표점 하나로 치환하는 다운샘플링이 아니다. [복셀 인덱싱](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L353-L370)

### 2.2 복셀 속 점의 형상 검사

복셀의 점이 충분하면 중심 `μ`와 공분산 `C`를 계산한다. 공분산은 점들이 중심에서 어느 방향으로 퍼져 있는지 나타낸다.

```text
μ = (1/N) Σ pᵢ
C = (1/N) Σ (pᵢ - μ)(pᵢ - μ)ᵀ
```

고유벡터는 점 구름의 주요 방향, 고유값은 그 방향의 퍼짐 정도다. 큰 고유값 하나가 지배하면 **선형**, 둘이 크고 나머지 하나가 작으면 **평면형**, 세 방향의 퍼짐이 비슷하면 **불규칙한 형상**으로 이해할 수 있다. 가장 작은 고유값의 고유벡터는 면에 수직인 **법선 후보**다. 실제 코드는 오름차순 고유값 `λ₀≤λ₁≤λ₂`에서 `r=λ₂/(λ₀+λ₁+λ₂)`를 사용한다. [논문 §III-B](https://arxiv.org/pdf/2603.04208), [고유값 분류](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L549-L610)

| 복셀 내 점 수·조건 | 코드의 처리 |
|---|---|
| 1–2개 | 형상 추정 없이 건너뜀 |
| 3–5개 | 고유분해 대신 X·Y·Z 분산을 비교. Z 분산이 X와 Y보다 모두 크면 비지면 후보, 아니면 지면 후보 |
| 6개 이상, `r>0.95` | `LINE`. 선의 주방향이 중력축에 수평에 가까우면 지면 후보, 수직에 가까우면 비지면 후보 |
| 6개 이상, `0.4<r≤0.95` | `PLANE` 후보. 법선 경사 검사를 거쳐 평면 피팅 수행 |
| 6개 이상, `r≤0.4` | `NOISE` 비지면 후보 |

즉 **모든 복셀에 평면을 피팅하지 않는다.** 적은 점의 복셀과 선형 복셀에는 각각 다른 판정 분기가 있다. [복셀별 분기](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L510-L630)

### 2.3 평면 후보의 피팅과 경사 검사

먼저 PCA에서 얻은 법선의 경사를 검사한다. 이 검사를 통과한 평면 후보에만 PCL `SACSegmentation`의 `SAC_PROSAC`을 적용한다. 한 가설은 서로 일직선에 있지 않은 점 3개로 평면 `ax+by+cz+d=0`을 정한다. 복셀의 각 점 `pᵢ=(xᵢ,yᵢ,zᵢ)`에서 그 평면까지의 거리를 계산해 `groundInlierThreshold=τ` 이내인 점을 **inlier**, 밖의 점을 **outlier**로 센다.

```text
거리(pᵢ, 평면) = |axᵢ + byᵢ + czᵢ + d| / √(a²+b²+c²)
inlier 조건      = 거리(pᵢ, 평면) ≤ τ
```

PROSAC은 샘플링에 쓸 점의 인덱스 범위를 점차 넓혀 여러 평면 가설을 시험하고, inlier가 가장 많은 가설을 남긴다. 이 구현은 PCL에 최대 반복 횟수 `1000`을 설정하고, 선택된 inlier로 평면 계수를 최적화한 뒤 최적화된 평면에 대해 inlier를 다시 선정한다. 최종 inlier 인덱스는 복셀에 보관해 뒤의 점별 분리에 사용한다. 단, **이 코드에는 점을 신뢰도 순으로 정렬하는 단계가 없다.** 따라서 일반적인 PROSAC 설명처럼 '품질이 높은 점부터 뽑아 정확도가 높아진다'는 효과를 이 구현에서 보장할 수는 없다. inlier가 하나도 없으면 복셀은 비지면 후보가 된다. 마지막으로 피팅된 평면의 법선을 이용해 경사를 다시 계산한다. [평면 피팅 설정·실행](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L222-L243), [GSeg3D의 평면 후보 처리](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L473-L630), [PCL PROSAC 구현](https://github.com/PointCloudLibrary/pcl/blob/pcl-1.14.1/sample_consensus/include/pcl/sample_consensus/impl/prosac.hpp), [PCL 평면 계수 최적화](https://github.com/PointCloudLibrary/pcl/blob/pcl-1.14.1/segmentation/include/pcl/segmentation/impl/sac_segmentation.hpp)

```text
경사각 α = arccos( | (R · 법선) · 중력축 Z | )
```

평지의 법선은 중력축과 가까워 `α≈0°`, 수직 벽의 법선은 `α≈90°`다. `R`은 위의 IMU 자세에서 온다. **완만한 평면은 지면 후보일 뿐 최종 지면이 아니다.** 수평인 상자 윗면도 경사 검사만으로는 제외할 수 없기 때문이다. [법선 경사 계산](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L372-L390)

### 2.4 로봇 아래에서 지면 후보 확장

1. `(x=0,y=0,z=distToGround)`에 시작 복셀과 KD-tree 탐색용 중심점을 만든다. 실제 LiDAR 점이 그 위치에 없어도 시작점은 생성하지만, 합성 측정점을 출력 클라우드에 넣지는 않는다.
2. 지면 후보 복셀의 중심점들을 KD-tree에 등록한다. 현재 중심점으로부터 3D 거리 `centroidSearchRadius` 이내의 후보를 찾는다.
3. 빈 복셀, 이미 처리·대기 중인 복셀, 지면 후보가 아닌 복셀은 제외한다. 통과한 복셀을 큐에 넣고, 큐에서 꺼내 같은 탐색을 반복한다.
4. **2차 처리에서만** 현재 복셀과 이웃 복셀 중심점의 Z 차이가 `maxGroundHeightDeviation`보다 크면 그쪽으로 확장하지 않는다.

이 방법은 **복셀 중심점의 근접성에 따른 영역 확장**이다. 격자 인덱스가 바로 붙지 않아도 탐색 반경 안이면 이어질 수 있다. 실제 두 표면이 물리적으로 접촉하는지 또는 이웃 면의 법선이 일치하는지를 직접 증명하지는 않는다. [논문 §III-D](https://arxiv.org/pdf/2603.04208), [KD-tree 확장 구현](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L635-L748)

### 2.5 복셀 속 점의 최종 배정

영역 확장에 포함된 복셀도 점마다 다른 출력으로 갈 수 있다. [점별 출력 코드](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L775-L895)

| 복셀 상태 | 점별 처리 |
|---|---|
| 연결된 지면 후보이며 3–5점 또는 `LINE` | 복셀의 모든 점을 지면 출력으로 보냄 |
| 연결된 평면 후보 | 피팅된 평면의 inlier와 outlier로 점을 분리 |
| inlier/outlier의 희소도 등급이 다름 | inlier는 지면, outlier는 비지면으로 보냄 |
| 희소도 등급이 같음 | inlier의 높이를 주변 지면 후보와 비교하고 같은 XY 열의 아래쪽에 비지면 복셀이 있는지 검사. 실패하면 **복셀 전체**를 비지면으로 보냄 |
| 처음에 비지면으로 분류된 복셀 — 1차 | 인접한 *확장 완료 지면 복셀*을 찾고, 그 평면에 가까운 점만 **1차 지면 출력으로 복구** |
| 처음에 비지면으로 분류된 복셀 — 2차 | 복셀의 모든 점을 비지면으로 보냄 |

희소도 등급은 복셀 **전체 점의** bounding-box 부피를 inlier 수 또는 outlier 수로 나눈 값으로 정한다. 값이 작을수록 같은 공간 안에 점이 조밀하다. 코드는 두 값을 `Low/Medium/High`로 양자화하지만, **어느 쪽 점수가 더 높은지로 다수결을 하지는 않는다.** 두 *등급이 같을 때*만 판정이 모호하다고 보고 주변 높이 검사와 아래쪽 비지면 복셀 검사를 실행한다. 1차의 점별 복구에서는 인접 지면 복셀의 중심 `c`와 법선 `n`을 이용해 `|n·(p−c)| ≤ groundInlierThreshold`인지 검사한다. [희소도 계산](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L392-L416), [인접 지면·점-평면 거리](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L266-L335), [최종 배정](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L785-L895)

### 2.6 점이 출력까지 도달하지 못하는 경우

위의 표는 출력 루프에 들어온 복셀의 처리다. 입력의 모든 점이 반드시 그 루프까지 도달하는 것은 아니다.

| 단계 | 점의 가능한 경로 |
|---|---|
| 전처리 | CropBox 밖의 점은 GSeg3D 입력에서 제외된다. 다운샘플링이 켜져 있으면 여러 원본 점이 대표점으로 합쳐질 수 있다. |
| 복셀 분류 | 점이 1–2개인 복셀은 지면 후보·비지면 후보 목록 양쪽에 등록되지 않는다. |
| 지면 후보 확장 | 지면 후보로 분류돼도 시작점에서 KD-tree 영역 확장으로 닿지 않으면 `ground_cells` 출력 루프에 포함되지 않는다. 비지면 목록에도 없으므로 해당 점은 출력되지 않는다. |
| 연결된 지면 후보 | 3–5점·선형 복셀은 전체가 지면으로 간다. 평면 복셀은 inlier와 outlier가 나뉘거나, 모호성 검사 실패 시 전체가 비지면으로 간다. |
| 처음부터 비지면 후보 | 1차에서는 인접 지면 평면에 가까운 점만 지면으로 복구하고 나머지를 비지면으로 보낸다. 2차에서는 전체를 비지면으로 보낸다. |

즉 `전처리 후 입력 점 수 = 지면 출력 점 수 + 비지면 출력 점 수`가 일반적으로 성립하지 않는다. 특히 **1–2점 복셀과 연결되지 않은 지면 후보의 점은 비지면으로 자동 전환되는 것이 아니라 출력에서 빠질 수 있다.** 2차는 1차 지면 출력만 다시 처리하므로 1차에서 빠진 점을 되찾지도 못한다. [복셀 분류와 후보 목록](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L510-L630), [영역 확장](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L635-L748), [출력 루프](https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L775-L895)

## 3. 1차와 2차의 데이터 흐름

1차는 큰 `cellSizeZ`로 전체 입력을 분석한다. 1차 내부에서 비지면 복셀의 일부 점이 인접 지면 평면 검사로 **1차 지면 출력에 복구될 수 있다**. 2차는 그 **1차 지면 출력만** 더 작은 `cellSizeZPhase2`로 다시 격자화한다. 최종 지면은 2차 지면 결과, 최종 비지면은 **1차 비지면 + 2차 비지면**이다. [ROS 2 래퍼의 두 단계 호출](https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L251-L267)

큰 높이 복셀은 넓은 수직 범위를 함께 살펴 높은 구조를 먼저 거르는 데 유리하지만, 한 복셀에 지면과 낮은 물체가 섞일 수 있다. 작은 높이 복셀로 1차 지면 후보를 다시 나누면 이런 점들의 수직 구조를 더 세밀하게 검사할 수 있다. 2차에서는 영역 확장에도 **복셀 중심점 간 높이 차이 제한**이 추가된다. 따라서 2차는 단순히 같은 검사를 한 번 더 하는 것이 아니라, 입력 집합·복셀 높이·연결 조건이 바뀐 재검사다. [논문 §III 및 Fig. 3](https://arxiv.org/pdf/2603.04208), [2차 설정과 호출](https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L84-L101)

```text
전처리된 전체 점군
  ├─ 1차 지면점 ─→ 2차 재검사 ─→ 최종 ground_points
  │                            └→ 2차 비지면점 ─┐
  └─ 1차 비지면점 ───────────────────────────────┴→ 최종 obstacle_points
```

따라서 **1차 비지면 출력은 2차 입력으로 들어가지 않는다.** 논문은 2차가 1차 오류를 보정한다고 설명하지만, 현재 ROS 2 래퍼가 2차에서 직접 재검사하는 것은 1차 지면 출력뿐이다. **1차 처리 내부의 비지면점 복구**와 **2차 재검사**를 혼동하면 안 된다. [논문 §III](https://arxiv.org/pdf/2603.04208), [래퍼 코드](https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L251-L267)

## 4. Ground Consistency: 비지면점에서 costmap 비용까지

GSeg3D의 `obstacle_points`는 **비지면점**이다. 로봇과 충돌할 높이인지 여부는 Ground Consistency가 별도로 판단한다. 이 플러그인은 두 클라우드 토픽을 제공하는 다른 지면 분리기와도 사용할 수 있다. [플러그인 개요](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin), [Nav2 튜토리얼](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/navigation2_with_ground_consistency_layer/)

### 4.1 3D 점을 2D 셀 상태에 집계

지면·비지면 클라우드는 **서로 다른 구독 콜백**에서 받는다. 각 메시지의 시각으로 costmap 좌표계 TF를 조회해 점을 변환한다. TF 조회가 실패하면 해당 클라우드는 반영되지 않는다. 변환된 점의 `x,y`를 costmap 해상도로 나눠 `floor`한 2D 셀에 넣는다. 같은 XY에 있는 점은 Z가 달라도 같은 셀의 관측이다. 두 토픽을 동일 스캔으로 묶는 별도 동기화는 없다. [클라우드 수신·TF 변환](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L468-L580), [셀 인덱스](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/include/nav2_ground_consistency_costmap_plugin/ground_consistency_layer.hpp#L49-L65)

셀에는 지면 점수 `g`, 비지면 점수 `ng`, 지면 높이의 합계·점 개수, 최신 비지면 관측의 최소·최대 높이, 이번 갱신 주기의 각 점 개수가 저장된다. 지면점은 지면 카운트·높이 통계를 늘리고, 비지면점은 비지면 카운트·최소·최대 높이를 갱신한다. 비지면점의 `maximum_height_filter`는 **TF 변환 전 입력 프레임의 Z값**에 적용된다. [셀 상태](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/include/nav2_ground_consistency_costmap_plugin/ground_consistency_layer.hpp#L109-L124), [점 집계](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L499-L576)

### 4.2 증거 누적·감쇠와 장애물 점수

costmap 갱신 때 이번 주기의 **포인트 개수 × 각 증분값**을 이전 점수에 더한 뒤 `max_score`로 제한한다. 새 센서 데이터가 들어온 갱신 주기에는 그 결과에 감쇠 계수를 곱한다. `N_g`, `N_ng`는 해당 주기에 쌓인 각 종류의 포인트 개수다. [점수 갱신](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L296-L337)

```text
g'  = min(max_score, g  + N_g  × ground_inc)    × ground_decay
ng' = min(max_score, ng + N_ng × nonground_inc) × nonground_decay
p_occ = ng' / (ng' + g' + ε)
```

감쇠는 초당 비율이 아니라 **새 데이터가 있는 costmap 갱신 주기당 비율**이다. `p_occ`는 이름과 달리 센서 오차 모델로 보정된 베이지안 확률이 아니라 **두 점수의 비율**이다. 코드의 예시값 `ground_decay=0.80`, `nonground_decay=0.93`에서는 지면 증거가 더 빨리 감소한다. [감쇠·비율 계산](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L317-L352)

튜토리얼 예시값 `ground_inc=1.0`, `nonground_inc=1.5`를 사용해 한 셀에 처음에는 지면점 4개·비지면점 3개, 다음 갱신에는 비지면점 3개가 추가됐다고 하자.

| 갱신 | 새 지면/비지면점 | 지면 점수 `g` | 비지면 점수 `ng` | `p_occ` |
|---|---:|---:|---:|---:|
| 1 | 4 / 3 | `(0+4×1.0)×0.80=3.20` | `(0+3×1.5)×0.93=4.185` | 약 0.567 |
| 2 | 0 / 3 | `3.20×0.80=2.56` | `(4.185+3×1.5)×0.93≈8.077` | 약 0.759 |

`nonground_occ_thresh=6.0` 및 `nonground_prob_thresh=0.75`이면 첫 갱신은 높이 검사에 들어가지 않고, 둘째 갱신은 **두 기준을 모두 통과**한다. 이는 점 6개가 반드시 서로 다른 스캔 6개에서 와야 한다는 뜻이 아니다. 점 밀도·다운샘플링·셀 크기에 따라 점수 증가 속도가 달라진다. [임계값 판정](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L343-L355)

### 4.3 지역 지면 높이와 충돌 높이

두 점수 기준을 통과한 셀에서만 높이를 검사한다. 같은 셀의 지면점 Z 평균이 있으면 이를 지역 지면 높이 `z_ground`로 사용한다. 없으면 `ground_neighbor_search_cells` 범위의 **이웃 셀별 평균 높이를 다시 평균**한다. 끝내 찾지 못하면 기준 높이가 불명확하므로 `LETHAL`이다. [지면 높이 추정](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L350-L383)

```text
min_relative_height = obstacle_min_z - z_ground
max_relative_height = obstacle_max_z - z_ground

min_relative_height > robot_height  → FREE   (비지면점이 전부 로봇 위)
max_relative_height < min_clearance → FREE   (비지면점이 전부 낮음)
그 외                                → LETHAL (충돌 높이와 겹침)
지면 기준을 못 찾음                 → LETHAL
```

예를 들어 `z_ground=0 m`, `robot_height=0.92 m`, `min_clearance=0.10 m`이고 점수 기준을 이미 통과했다면, 비지면 높이 0.04–0.08 m는 `FREE`, 0.12–0.50 m는 `LETHAL`, 1.20–1.50 m는 `FREE`다. 지면 분리에서는 모두 비지면이지만 최종 주행 비용은 다르다. [높이 판정](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L386-L405)

### 4.4 이전 관측과 새 관측이 섞일 때

점수와 높이에는 서로 다른 시간 규칙이 적용된다. `g`, `ng`는 새 점을 누적한 뒤 감쇠한다. 반면 지면 높이의 합계·점 개수는 **셀을 삭제할 때까지 누적**되며 감쇠하지 않는다. 비지면 최소·최대 높이는 그 갱신 주기에 새 비지면점이 있으면 **그 주기의 최소·최대로 교체**되고, 없으면 이전 값이 유지된다. 두 클라우드 콜백에 스캔 시각을 맞추는 절차도 없으므로, 같은 갱신 주기의 지면점과 비지면점이 같은 원본 스캔에서 왔다고 볼 수 없다. [높이·점수 갱신](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L296-L337), [두 클라우드 콜백](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L468-L580)

4.2절의 수치를 그대로 사용해, 첫 갱신의 비지면 3점은 지면보다 0.20–0.50 m 높은 상자이고 둘째 갱신의 비지면 3점은 1.20–1.50 m 높은 구조물이라고 가정하자. 지면점 4개의 평균 높이는 0 m다.

| 갱신 | 누적 점수로 계산한 `p_occ` | 판정에 쓰는 지면 높이 | 판정에 쓰는 비지면 높이 | 결과 |
|---|---:|---:|---:|---|
| 1 | 0.567 | 0 m | 0.20–0.50 m | 점수 임계값 미달: 높이 검사 없음 |
| 2 | 0.759 | 이전 지면점 4개의 평균 0 m | **둘째 주기의** 1.20–1.50 m | 점수 임계값 통과, `1.20 > robot_height(0.92)`이므로 `FREE` |

둘째 결과에서 비지면 **점수**에는 첫 상자 관측이 남아 있지만, 비지면 **높이 범위**는 둘째 관측으로 교체돼 있다. 반대로 둘째 주기에 비지면점이 없었다면 이전 높이 범위가 유지된다. 따라서 이 레이어는 과거 모든 비지면점의 높이 분포를 함께 보관하거나, 점수와 높이를 같은 시간 창으로 동기화해서 판단하는 방식이 아니다. 위 표는 코드의 시간 규칙을 설명하는 계산 예시이며, 실제 두 물체가 같은 셀에서 번갈아 관측될 때의 안전성은 센서·셀 크기·관측 순서에 따라 달라진다.

### 4.5 비용 기록과 미관측 셀

높이 판정 전 셀의 임시 비용은 `p_occ×252`로 계산된다. 높이 판정 결과에 따라 `LETHAL_OBSTACLE` 또는 `FREE_SPACE`로 덮어쓴다. `discretize_costs=true`이면 그 밖의 중간 비용도 `FREE_SPACE`로 바꾼다. `updateCosts()`는 **플러그인의 저장 셀에 대응하는 위치에만** 계산한 비용을 Nav2 master costmap에 기록한다. 옵션이 켜져 있으면 로봇 footprint의 증거를 지우고 그 영역을 `FREE_SPACE`로 만든다. rolling window 밖이나 `max_data_range` 밖의 저장 셀도 제거한다. [비용 계산·셀 정리](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L250-L428), [costmap 기록](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L583-L614)

이 플러그인의 코드에서 **점이 관측되지 않은 셀을 `FREE_SPACE`로 만드는 광선 추적(raytracing) 단계는 없다.** 점수가 감쇠해 내부 셀을 삭제해도 `updateCosts()`가 그 위치에 별도의 자유 공간 값을 쓰는 것은 아니다. 따라서 '관측이 없다', '플러그인 내부 셀이 삭제됐다', 'master costmap이 자유 공간이 됐다'는 서로 다른 상태다. 미관측 셀이 `NO_INFORMATION`으로 남는지, 이전 비용이 지워지는지, 다른 레이어가 자유 공간·장애물로 덮어쓰는지는 Nav2의 master costmap 초기화·갱신과 함께 사용하는 레이어 설정에 달려 있다. **이 플러그인 코드만으로는 최종 master 셀의 미관측 상태를 단정할 수 없다.** [플러그인의 비용 기록 루프](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L583-L614)

![지면·비지면점과 local costmap의 RViz 표시](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/assets/rviz_window.png)

## 5. 논문 설명과 공개 구현의 차이

| 항목 | 논문·개념 설명 | 확인한 구현 |
|---|---|---|
| IMU와 중력 기준 | 지면 경사를 중력 기준으로 평가 | IMU orientation을 **방향 벡터**에 적용. 점군 전체는 회전하지 않음 |
| 지면 시작점 | 로봇 아래에 합성 지면점 사용 | 실제 점군에 합성 측정점을 추가하지 않고 **시작 복셀과 KD-tree용 중심점**을 생성 |
| 2차 오류 보정 | 1차 결과를 작은 수직 해상도로 보정 | 2차 입력은 **1차 지면 출력만**. 1차 비지면 출력은 2차에 들어가지 않음 |
| 적은 점의 복셀 | 국소 형상 분석 | 1–2점은 건너뛰고, 3–5점은 PCA·평면 피팅 대신 분산 비교 |
| 선형 복셀 | 논문 §III-B의 각도 부등식 | 코드에서는 선 주방향이 중력축에 **수평에 가까울 때** 지면 후보. 논문에 적힌 부등호와 다름 |
| 지면 연결 | 공간적으로 연결된 지면 추출 | 복셀 중심점의 **반경 탐색**. 실제 표면 접촉을 증명하지 않음 |
| 증거 감쇠 | 오래된 관측의 영향 감소 | `g`, `ng` 점수만 감쇠. 지면 높이의 **합계·개수는 셀이 남는 동안 누적** |

## 6. 설정값과 실패 조건

| 조건·파라미터 | 처리에 미치는 영향 |
|---|---|
| `lidar_to_ground` 및 LiDAR↔로봇 TF | 시작 복셀의 높이를 정한다. 잘못되면 실제 지면 근처에서 영역 확장을 시작하지 못할 수 있다. |
| `use_imu_orientation` 및 IMU↔로봇 TF | 경사각의 기준을 정한다. 이 래퍼는 `Imu.orientation`을 직접 읽으므로 각속도·가속도만 들어 있는 메시지와는 구분해야 한다. |
| `cellSizeX/Y/Z` | 한 복셀에 들어오는 점 수와 구조의 혼합 정도를 바꾼다. 너무 작으면 1–2점 복셀이 늘고, 너무 크면 지면과 물체가 같은 복셀에 섞일 수 있다. |
| `groundInlierThreshold` | 평면에서 어느 거리까지 점을 inlier로 볼지, 1차 인접 지면 평면에서 점을 복구할지 결정한다. |
| `slopeThresholdDegrees` | 국소 면·선이 지면 후보가 될 수 있는 경사 범위를 제한한다. IMU 자세가 틀리면 이 판정도 틀어진다. |
| `centroidSearchRadius` 및 `maxGroundHeightDeviation` | 후보 영역이 끊길지, 지나치게 멀거나 높이가 다른 후보까지 이어질지에 영향을 준다. 후자는 2차 영역 확장에서 적용된다. |
| costmap 해상도와 `ground_inc`/`nonground_inc` | 한 셀에 모이는 포인트 수와 증거 증가 속도를 함께 바꾼다. 점수 임계값은 센서 밀도와 독립적인 ‘스캔 횟수’가 아니다. |
| `ground_neighbor_search_cells` | 같은 2D 셀에 지면점이 없을 때 이웃 지면 높이를 사용할 범위다. 0이면 이웃 탐색을 하지 않는다. |

GSeg3D는 **한 스캔의 기하 구조**를 분리하고, Ground Consistency는 **여러 관측의 셀별 증거**를 누적한다. 후자의 시간적 안정성이 전자의 오분류를 언제나 고친다는 뜻은 아니다. 지면점 누락, 잘못된 TF/IMU 자세, 지면 높이 추정 실패는 최종 비용에도 영향을 준다. [GSeg3D 설정](https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/config/parameters.yaml), [Ground Consistency 파라미터](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin#parameters)

## 참고 자료

- [GSeg3D 논문: *A High-Precision Grid-Based Algorithm for Safety-Critical Ground Segmentation in LiDAR Point Clouds*](https://arxiv.org/pdf/2603.04208)
- [GSeg3D 핵심 라이브러리](https://github.com/dfki-ric/ground_segmentation)
- [GSeg3D ROS 2 래퍼](https://github.com/dfki-ric/ground_segmentation_ros2)
- [PCL 1.14.1 PROSAC 구현](https://github.com/PointCloudLibrary/pcl/blob/pcl-1.14.1/sample_consensus/include/pcl/sample_consensus/impl/prosac.hpp)
- [Nav2 Ground Consistency Costmap Plugin](https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin)
- [Nav2 튜토리얼: Ground Terrain Segmentation using 3D Lidar](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/navigation2_with_ground_consistency_layer/)
