# GSeg3D 지면 분리와 Nav2 Ground Consistency Layer

## 1. 구성과 분석 기준

GSeg3D는 LiDAR 점군의 국소 형상과 공간적 연결성을 분석해 지면점과 비지면점을 분리한다. Ground Consistency Layer는 이 결과를 2D 격자에 누적하고, 지역 지면 높이를 기준으로 충돌 비용을 계산한다.

| 구성 요소 | 입력 | 처리 | 출력 |
|---|---|---|---|
| ROS 2 래퍼 | `PointCloud2`, 선택적 `Imu.orientation`, TF | 좌표 변환·필터링·두 단계 분리 호출 | `ground_points`, `obstacle_points` |
| GSeg3D | 한 스캔의 XYZ 점, 로봇 자세 | 복셀 분류·평면 피팅·영역 확장·점별 분리 | 지면점·비지면점 |
| Ground Consistency | 지면·비지면 클라우드, TF | 셀별 증거 누적·감쇠·상대 높이 판정 | Nav2 costmap 비용 |

```text
PointCloud2 + TF (+ IMU orientation)
  → robot_frame 변환 → 선택적 VoxelGrid → CropBox
  → GSeg3D 1차
      ├─ 지면점 → GSeg3D 2차 ─┬─ 지면점 ─────────→ ground_points
      │                      └─ 비지면점 ─┐
      └─ 비지면점 ────────────────────────┴──────→ obstacle_points
  → costmap 좌표계 변환 → XY 셀 집계
  → 증거 누적·감쇠 → 지역 지면 높이·충돌 높이 판정
  → master costmap → Inflation Layer
```

설명은 다음 공개 소스의 동작을 기준으로 한다. 논문과 구현의 차이는 6절에 정리한다.

| 소스 | 분석 기준 |
|---|---|
| [GSeg3D 핵심 라이브러리][core] | `e5aa4c2` |
| [ROS 2 래퍼][wrapper] | `c0d60fd` |
| [Ground Consistency Layer][layer] | `41cec62` |
| [PCL 평면 모델][pcl-plane]·[PROSAC][prosac] | `1.14.1` |
| [Nav2 master costmap 갱신][master] | Jazzy 계열 `645abd9` |

![입력 LiDAR 점군](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/assets/raw_points.png)

![지면점(초록색)과 비지면점(자홍색)](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/assets/segmented_points.png)

---

## 2. 좌표계와 전처리

### 2.1 점 좌표 변환과 필터링

`L`을 LiDAR 프레임, `B`를 `robot_frame`으로 정의한다. `T_BL`은 L 좌표를 B 좌표로 변환하는 강체 변환이다.

```text
p_B = R_BL p_L + t_BL
```

래퍼는 `lookupTransform(B, L, cloud_stamp)`로 이 변환을 구한다. 스캔 시각의 TF를 얻지 못하면 해당 스캔을 건너뛴다. 이후 처리 순서는 다음과 같다. [코드: 전처리][wrapper-pre]

| 순서 | 처리 | 데이터 변화 |
|---:|---|---|
| 1 | `PointCloud2` → `pcl::PointXYZ` | XYZ를 사용한다. intensity·ring·점별 시각은 이후 결과에 전달하지 않는다. |
| 2 | LiDAR → `robot_frame` 변환 | 점 좌표를 로봇 기준으로 통일한다. |
| 3 | 선택적 PCL `VoxelGrid` | 각 다운샘플링 복셀의 점들을 평균 좌표 하나로 줄인다. |
| 4 | PCL `CropBox` | 로봇 프레임의 `minX/maxX`, `minY/maxY`, `minZ/maxZ` 범위만 남긴다. |

GSeg3D의 격자화는 3절에서 수행하며, 복셀 안의 점들을 그대로 보관한다. 전처리 `VoxelGrid`의 다운샘플링과는 역할이 다르다. 출력 클라우드는 원본 스캔의 timestamp와 `robot_frame`을 사용한다.

### 2.2 예상 지면 높이

영역 확장의 시작 높이는 다음 식으로 계산한다.

```text
z_seed = t_BL.z + lidar_to_ground
seed   = (0, 0, z_seed)
```

`lidar_to_ground`는 로봇 프레임 Z축을 따라 LiDAR 원점에서 예상 지면까지 내려가는 부호 있는 거리(m)다. 예를 들어 LiDAR가 로봇 원점보다 0.6 m 높고 예상 지면이 로봇 원점보다 0.4 m 낮으면 `lidar_to_ground=-1.0`, `z_seed=-0.4`다.

이 값은 탐색 시작점의 높이를 정한다. 실제 지면 평면과 경사는 주변 측정점에서 추정한다. [코드: 시작 높이][wrapper-height]

### 2.3 IMU 자세와 중력 기준

`Imu.orientation`은 IMU의 방향을 나타내는 쿼터니언이다. 래퍼는 이 자세를 직접 사용하며, 각속도·가속도에서 자세를 추정하는 필터는 포함하지 않는다.

IMU 자세의 기준 프레임을 `G`, `Imu.header.frame_id`의 센서 프레임을 `I`라고 하면 회전의 조합은 다음과 같다. G의 Z축은 중력축과 평행해야 한다. IMU 메시지는 G의 프레임 이름을 별도 필드로 전달하지 않는다.

```text
R_GB = R_GI R_IB
q_GB = q_GI ⊗ q_IB
```

`q_GI`는 I 좌표의 방향을 G 좌표로 회전하는 IMU orientation, `q_IB`는 `lookupTransform(I, B, imu_stamp)`의 회전이다. 이 조합으로 로봇 프레임에서 구한 법선·선 방향을 중력 기준으로 표현한다. IMU의 센서축 정의와 설치 TF가 일치해야 한다. 아래 각도 식은 Z 내적의 절댓값을 사용하므로 중력축의 위·아래 부호는 같은 경사각을 만든다. [코드: 자세 조합][wrapper-imu]

| 항목 | IMU 적용 범위 |
|---|---|
| PCA 법선·피팅 평면 법선·선 주방향 | `R_GB`로 회전한 뒤 중력축과의 각도를 계산한다. |
| XYZ 점 좌표·복셀 인덱스·복셀 중심 | 로봇 프레임을 유지한다. |
| 3–5점 복셀의 축별 분산·이웃 중심의 Z 차이 | 로봇 프레임에서 계산한다. |

따라서 IMU 보정은 **경사 판정의 기준을 중력축으로 맞추는 처리**다. 경사면은 그 경사각을 유지하며, 하나의 수평면으로 펴지지 않는다.

점군과 IMU는 `ApproximateTime`으로 짝을 맞춘다. 큐 크기 `50`은 메시지 보관 개수다. 스캔 전체에 자세 하나를 사용하며, IMU 보간이나 점별 취득 시각에 따른 motion deskew는 수행하지 않는다. `transform_tolerance`는 TF 조회의 대기시간이며 두 센서의 동기화 허용 시각차와 별개다. IMU를 사용하지 않으면 `R_GB=I`로 두어 로봇 Z축을 기준으로 경사를 계산한다. [코드: 동기화][wrapper-sync]

래퍼는 점군·IMU 메시지 timestamp 차이의 최대값을 별도로 설정하지 않는다. `use_imu_orientation=true`일 때는 두 입력이 동기화 콜백에 도달해야 처리하며, IMU가 끊겼을 때 IMU 없는 처리로 자동 전환하지 않는다.

---

## 3. GSeg3D 처리 알고리즘

한 단계의 처리 순서는 `복셀 배정 → 국소 형상 분류 → 지면 후보 확장 → 점별 분리`다. 라이브러리는 입력 스캔마다 격자를 새로 구성한다.

### 3.1 3D 복셀 배정

복셀 크기를 `(s_x, s_y, s_z)`라고 하면 점 `p=(x,y,z)`의 인덱스는 다음과 같다.

```text
v(p) = (floor(x/s_x), floor(y/s_y), floor(z/s_z))
```

`s_x=1 m`일 때 `x=2.4`와 `x=2.8`은 X 인덱스 2에, `x=-0.2`는 -1에 들어간다. 점이 존재하는 복셀만 해시맵에 저장한다. 각 복셀은 점 목록, 점들의 평균 좌표, 형상 분류, 법선, 평면 inlier 인덱스와 확장 상태를 보관한다. [코드: 복셀 배정][core-voxel]

이후 사용하는 `centroid`는 **복셀 안의 측정점 평균**이다. 복셀의 기하학적 중앙 좌표와 다르다.

### 3.2 공분산과 PCA 형상 분류

점 수가 `N≥6`인 복셀에서 평균 좌표 `μ`와 정규화 공분산 `C`를 계산한다.

```text
μ = (1/N) Σ p_i
C = (1/N) Σ (p_i − μ)(p_i − μ)^T
C e_k = λ_k e_k,  λ_0 ≤ λ_1 ≤ λ_2
```

`λ_k`는 방향 `e_k`로 퍼진 정도(m²)다. 선형 구조는 한 방향의 분산이 지배하고, 평면은 면 안의 두 방향으로 퍼지면서 수직 방향의 분산이 작다. PCA 법선은 최소 고유값에 대응하는 `e_0`, 선 주방향은 최대 고유값에 대응하는 `e_2`다.

이 법선 선택은 최소제곱 평면과 연결된다. `μ`를 지나는 평면의 단위 법선을 `n`으로 두면 점들의 수직 거리 제곱 평균은 다음과 같다.

```text
평면: n · (p−μ) = 0,  ||n||=1
거리 제곱 평균 = (1/N) Σ [n · (p_i−μ)]² = n^T C n
```

이 값을 최소화하는 방향이 `e_0`이고 최소값이 `λ_0`다. PCA가 법선을 계산하는 이유는 점들의 수직 오차가 가장 작은 면을 찾기 위해서다. PROSAC은 전체 점 대신 선택된 inlier로 이 계산을 다시 수행해 outlier의 영향을 줄인다.

코드는 고유값 비율 `η=λ_2/(λ_0+λ_1+λ_2)` 하나로 형상을 분기한다. 예를 들어 `(0.001,0.001,0.1)`이면 `η≈0.980`으로 LINE, `(0.001,0.05,0.1)`이면 `η≈0.662`로 PLANE, `(0.1,0.1,0.1)`이면 `η≈0.333`으로 NOISE다. PLANE 분기는 이후 평면 피팅으로 검증한다. [코드: 형상 분류][core-classify]

| 조건 | 국소 분류 |
|---|---|
| `N<3` | 형상 분류를 건너뛴다. 시작 복셀의 예외는 3.8절에 정리한다. |
| `3≤N≤5` | `var_z>var_x`와 `var_z>var_y`가 모두 참이면 비지면, 아니면 지면 후보다. PCA·평면 피팅은 생략한다. |
| `N≥6`, `η>0.95` | LINE. 선 주방향의 경사를 검사한다. |
| `N≥6`, `0.4<η≤0.95` | PLANE. PCA 법선 검사 후 평면을 피팅한다. |
| `N≥6`, `η≤0.4` | NOISE 비지면으로 분류한다. |

### 3.3 법선과 경사각

단위 법선 `n`, 단위 선 방향 `v`, 중력 기준 Z축 `e_z=(0,0,1)`에 대해 다음 각도를 계산한다.

```text
α_plane = acos(|(R_GB n) · e_z|)
α_line  = acos(|(R_GB v) · e_z|)
```

절댓값은 법선의 부호 `n`과 `−n`을 같은 면으로 처리한다. 평지는 `α_plane≈0°`, 수직 벽은 `α_plane≈90°`다. 수평선은 `α_line≈90°`, 수직선은 `α_line≈0°`다.

경사 한계 `β=slopeThresholdDegrees`를 기준으로 실제 비교는 다음과 같다. 아래 표는 도 단위 표기이며, 코드는 `β×π/180`으로 변환해 라디안 각도와 비교한다. [코드: 경사 계산][core-slope]·[분류 조건][core-classify]

| 검사 | 통과 조건 |
|---|---|
| LINE 주방향 | `α_line > 90°−β` |
| PLANE의 PCA 법선 | `α_plane ≤ β` |
| PLANE의 피팅 법선 | `α_plane < β` |

수평인 상자 윗면도 이 검사를 통과한다. 지면 여부는 시작 지면과의 연결성 및 3.6절의 주변 높이 검사까지 거쳐 결정한다.

### 3.4 PROSAC 평면 피팅과 점별 거리

PCA 법선 검사를 통과한 PLANE 복셀에 PCL `SAC_PROSAC`을 적용한다.

1. 일직선에 있지 않은 점 3개로 평면 가설을 만든다.
2. 복셀의 모든 점과 가설 평면 사이의 수직 거리를 계산한다.
3. 거리 임계값 `τ=groundInlierThreshold`를 통과한 inlier 수로 가설을 평가한다.
4. 샘플링 대상 인덱스 범위를 점차 넓히면서 가장 많은 inlier를 갖는 가설을 유지한다. 반복 제한 설정은 `1000`이다.
5. 선택된 inlier가 4개 이상이면 최소제곱 평면을 다시 추정한다. inlier 평균·공분산에서 법선을 구하고 `d=−n·μ_inlier`로 평면을 정한다.
6. 최적화된 평면으로 inlier를 다시 선정하고, 피팅 법선의 경사를 재검사한다.

```text
가설 법선 = normalize((p_2−p_1) × (p_3−p_1))
평면       = ax + by + cz + d = 0
거리       = |ax_i + by_i + cz_i + d| / sqrt(a²+b²+c²)
inlier     = 거리 < τ
```

PCL 1.14.1의 inlier 비교는 엄격한 `<`다. GSeg3D는 입력점을 품질 순으로 정렬하지 않으므로 PROSAC의 초기 샘플링 순서는 복셀의 입력 순서를 따른다. 최종 inlier가 없거나 피팅 법선의 경사가 한계 이상이면 비지면 복셀로 분류한다. [코드: 피팅 호출][core-fit]·[PCL PROSAC][prosac]·[계수 최적화][pcl-plane]

예를 들어 가설이 `z=0.2x+0.1y`, `τ=0.1 m`이면 다음과 같다. 아래 계산은 `R_GB=I`를 사용한다.

| 점 | 평면까지 거리 | 분류 |
|---|---:|---|
| `(1,0,0.20)` | 0 m | inlier |
| `(1,0,0.16)` | 약 0.039 m | inlier |
| `(1,0,0.60)` | 약 0.390 m | outlier |

평면의 경사는 약 `12.6°`다. 지면의 거친 표면은 거리 임계값 안에 남기고, 평면에서 충분히 돌출한 점은 outlier로 분리하는 구조다.

### 3.5 시작 지면에서의 영역 확장

지면 후보로 분류된 복셀의 측정점 평균을 KD-tree에 넣는다. 탐색 시작 복셀은 `(0,0,floor(z_seed/s_z))`이고, KD-tree의 시작 좌표는 `(0,0,z_seed)`다. 이 좌표는 탐색용으로 추가하며 출력 측정점에는 추가하지 않는다.

영역 확장은 큐 기반 탐색이다.

```text
Q ← [시작 복셀]
while Q가 비어 있지 않음:
    현재 복셀을 꺼내 확장 완료로 표시
    현재 centroid 주변의 3D 반경 ρ 안에서 후보를 검색
    빈 복셀·확장 완료·큐 대기 중·비지면 후보를 제외
    2차이면 |현재 centroid.z − 이웃 centroid.z| > H인 후보 제외
    남은 후보를 Q에 추가
    현재 복셀을 ground_cells에 등록
```

`ρ=centroidSearchRadius`, `H=maxGroundHeightDeviation`다. 그래프 관점에서는 지면 후보가 정점이고, 평균 좌표 간 거리가 반경 안에 드는 쌍이 연결 후보다. 2차에는 높이 차이 조건이 추가된다. 시작 정점에서 도달 가능한 복셀만 점별 지면 분리로 넘어간다. [코드: 시작 복셀·KD-tree 확장][core-grow]

예를 들어 2차에서 `H=0.1 m`이고 후보의 높이가 순서대로 `0.00, 0.08, 0.16 m`라면, 반경 조건을 만족하는 인접 쌍을 따라 확장할 수 있다. 시작점 대비 총 높이 변화가 0.1 m를 넘어도 각 확장 단계의 차이가 작으면 연결된다.

연결 기준은 centroid의 거리와 높이 차이다. 격자 인덱스의 직접 인접, 표면 접촉, 이웃 법선 사이의 각도는 이 탐색의 검사 항목에 포함되지 않는다.

### 3.6 연결된 복셀의 inlier·outlier 분리

영역 확장에 포함된 복셀에서 다음 처리를 수행한다. [코드: 점별 분리][core-output]

| 복셀 | 출력 처리 |
|---|---|
| 점 수가 5개 이하 또는 LINE이며 GROUND | 전체 점을 지면으로 출력한다. |
| PLANE | 저장된 inlier 인덱스로 inlier와 outlier를 나눈다. |
| PLANE의 두 희소도 등급이 다름 | inlier는 지면, outlier는 비지면으로 출력한다. |
| PLANE의 두 희소도 등급이 같음 | 주변 높이와 아래쪽 복셀을 검사한다. 통과하면 inlier·outlier를 분리하고, 실패하면 전체를 비지면으로 출력한다. |

희소도는 **복셀 전체 점의 축 정렬 bounding box** 부피 `V`를 각 부분집합의 점 수로 나눈 값이다.

```text
V = (x_max−x_min)(y_max−y_min)(z_max−z_min)
s_in  = V / N_in
s_out = V / N_out
```

| 값(m³/point) | 코드의 희소도 등급 |
|---|---|
| `<0.001` | Low sparsity |
| `0.001≤s<0.01` | Medium sparsity |
| `s≥0.01` | High sparsity |
| `V≤0` | Degenerate |
| 부분집합이 비어 있음 | Empty |

등급 계산은 Empty → Degenerate → 수치 범위 순서로 검사한다. 비교하는 것은 등급의 동일 여부다. `N_in>N_out` 같은 다수결이나 두 희소도의 대소 비교는 수행하지 않는다. [코드: 희소도][core-sparsity]

등급이 같으면 다음 추가 검사를 수행한다.

1. inlier들의 평균 높이 `z_in`을 계산한다.
2. 주변 GROUND 후보 복셀의 centroid 높이 중 최솟값을 `z_ref`로 정한다.
3. 주변 후보가 없거나 `|z_in−z_ref|>H`이면 전체 복셀을 비지면으로 보낸다.
4. 같은 X·Y 인덱스에서 Z 인덱스를 한 칸씩 낮춘다. `nonempty && terrain_type != GROUND`인 복셀을 만나면 떠 있는 표면으로 판정해 전체를 비지면으로 보낸다. 첫 번째 누락 복셀에서 탐색을 끝낸다.

주변 후보의 인덱스 범위는 1차에서 `Δx,Δy∈[−1,1], Δz∈{−1,0}`, 2차에서 `Δx,Δy,Δz∈[−1,1]`이며 자기 자신을 제외한다. 이 높이 검사에는 두 단계 모두 `H`를 사용한다. 주변 후보의 확장 완료 여부는 검사하지 않는다.

### 3.7 1차 비지면 복셀의 점별 복구

1차에서는 처음에 비지면으로 분류한 복셀에도 주변 지면 평면을 적용한다.

1. 3.6절의 1차 주변 범위에서 점이 있고 확장 완료된 GROUND 복셀을 찾는다.
2. `Δi_x²+Δi_y²+0.25Δi_z²`가 가장 작은 이웃 하나를 선택한다. 이 거리는 격자 인덱스 기준이다.
3. 이웃의 PCA 법선 `n_PCA`와 **전체 측정점 평균** `μ_all`로 기준 평면을 만든다.
4. 비지면 복셀의 각 점을 다음 거리로 검사한다.

```text
기준 평면: n_PCA · (p − μ_all) = 0
복구 조건: |normalize(n_PCA) · (p − μ_all)| ≤ τ
```

통과한 점은 1차 지면, 나머지는 1차 비지면으로 보낸다. 적절한 이웃을 찾지 못하면 전체를 비지면으로 보낸다. 2차에서는 이 복구를 생략한다. [코드: 이웃 선택·복구 평면][core-recover]

**평면 피팅과 복구의 평면은 구분해야 한다.** 피팅 결과는 inlier 인덱스와 경사값에 사용하고, `cell.normal`과 `cell.centroid`를 피팅 결과로 교체하지 않는다. 따라서 복구에는 PROSAC 평면 계수 대신 PCA 법선·전체 점 평균이 사용된다.

### 3.8 두 단계 결합과 출력 예외

```text
(G_1, O_1) = Segment(P, 1차 설정)
(G_2, O_2) = Segment(G_1, 2차 설정)
최종 지면   = G_2
최종 비지면 = O_1에 O_2를 이어 붙인 클라우드
```

두 단계는 XY 복셀 크기를 공유하고 Z 크기를 다르게 사용한다. 2차는 1차 지면점만 다시 격자화하며, 영역 확장에 높이 연속성 조건을 추가한다. 1차 내부의 비지면점 복구와 2차 지면 재검사는 별도 처리다. [코드: 두 단계 결합][wrapper-passes]

출력 개수를 해석할 때는 다음 구현 예외를 함께 확인해야 한다.

| 경우 | 실제 출력 동작 |
|---|---|
| 일반 복셀에 1–2점만 존재 | 후보 목록에 등록되지 않아 출력에서 빠진다. |
| 시작점에서 도달하지 못한 지면 후보 | 비지면 목록으로 이동하지 않고 출력에서 빠진다. |
| 시작 복셀에 기존 측정점이 존재 | 형상 분류 후 시작 복셀의 상태를 GROUND로 강제 변경한다. 1–2점도 지면 출력에 들어갈 수 있다. |
| 시작 복셀이 원래 비지면으로 분류됨 | 기존 비지면 목록에도 남는다. 같은 점이 중복 출력되거나 지면·비지면 양쪽에 포함될 수 있다. |

지면·비지면 출력의 점 수 검사에서는 누락, 중복, 양쪽 출력의 교집합을 각각 측정해야 한다. [코드: 후보 등록][core-classify]·[시작 상태 변경][core-grow]·[출력 루프][core-output]

---

## 4. Ground Consistency 처리 알고리즘

GSeg3D의 비지면점에는 벽·물체뿐 아니라 낮은 돌출부와 머리 위 구조물도 포함된다. Ground Consistency는 지면·비지면 증거와 높이 구간을 결합해 2D 셀의 비용을 정한다.

### 4.1 XY 격자와 관측 집계

두 클라우드는 별도 구독 콜백에서 수신한다. 각 클라우드의 timestamp로 costmap의 `global_frame`에 변환한 뒤 XY 격자에 배정한다. 격자 해상도를 `r`이라고 하면 다음과 같다.

```text
p_C = T_CB p_B
cell(p_C) = (floor(p_C.x/r), floor(p_C.y/r))
```

동일 XY 셀에 들어온 점은 Z값이 달라도 하나의 셀 상태에 집계한다. Z는 이후 높이 판정을 위해 별도로 보관한다. 이 단계가 XY 평면으로의 격자 투영이다. 3D 복셀을 여러 높이층으로 유지하는 방식과 구분된다.

내부 격자의 원점은 global frame의 `(0,0)`이다. rolling costmap의 이동 원점과 독립적으로 셀 키를 유지하고, 비용 기록 시 master 셀의 중심 좌표로 대응 키를 조회한다. [코드: 수신·집계][layer-input]·[셀 키][layer-key]

비지면점만 `maximum_height_filter`를 적용하며, 조건은 **TF 변환 전 입력 프레임의 `z > maximum_height_filter`**다. 지면점에는 적용하지 않는다. 이 값은 지역 지면 대비 장애물 높이가 아니라 입력 프레임 원점 기준 Z값이다.

ground·nonground를 같은 스캔으로 묶는 동기화는 없다. 아래 표의 ‘갱신 구간’은 이전 `updateBounds()` 이후 다음 호출까지이며, 여러 클라우드가 포함될 수 있다.

| 셀 상태 | 의미 | 갱신·유지 방식 |
|---|---|---|
| `N_g`, `N_ng` | 이번 구간의 지면·비지면 점 수 | 콜백에서 증가, 점수 반영 후 0으로 초기화 |
| `g`, `ng` | 누적 지면·비지면 증거 | 점 수에 비례해 증가, 새 데이터 갱신 시 감쇠 |
| `S_z`, `M_z` | 지면 Z 합계·표본 수 | 셀을 삭제할 때까지 누적, 감쇠 없음 |
| `z_min`, `z_max` | 비지면 Z 범위 | 새 비지면점이 있는 구간의 min/max로 교체, 없으면 이전 값 유지 |
| `computed_cost` | 이번 갱신에서 계산한 비용 | `updateCosts()`에서 master에 기록 |

### 4.2 증거 누적과 전역 감쇠

`a_g`, `a_ng`를 점당 증분, `d_g`, `d_ng`를 감쇠 계수, `S_max`를 점수 상한으로 정의한다.

```text
g_acc  = min(S_max, g  + a_g  N_g)
ng_acc = min(S_max, ng + a_ng N_ng)

I=1: g_new = d_g g_acc,  ng_new = d_ng ng_acc
I=0: g_new = g_acc,      ng_new = ng_acc
```

`I`는 **플러그인 전체의 새 데이터 플래그**다. 두 토픽 중 어느 하나의 비어 있지 않은 클라우드를 정상 처리하면 1이 된다. 그 다음 `updateBounds()`에서 저장 중인 모든 셀을 한 번 감쇠한다. [코드: 누적·감쇠][layer-score]

| 입력 상황 | 감쇠 동작 |
|---|---|
| 다른 셀에만 새 점 도착 | 새 점이 없는 기존 셀도 감쇠 |
| 한 갱신 구간에 여러 메시지 도착 | 점은 모두 누적, 감쇠는 한 번 |
| 두 토픽 모두 중단 | 경과시간만으로는 감쇠하지 않음 |
| 빈 클라우드 수신 또는 TF 실패 | 새 데이터 플래그를 설정하지 않음 |
| 비어 있지 않은 비지면 클라우드의 모든 점이 높이 필터에서 제거됨 | 점수 증가 없이 감쇠 유발 |

감쇠 구간에서 두 점수가 모두 `0.001` 미만이면 셀을 삭제한다. 하나만 미만이면 그 점수만 0으로 정리한다.

새 점이 없는 셀을 `k`번 감쇠하면, 0.001 미만 값의 정리 전에는 `g_k=d_g^k g_0`, `ng_k=d_ng^k ng_0`이다. 감쇠 횟수 기준 반감기는 다음과 같다.

```text
k_half = ln(0.5) / ln(d)
```

`d_g=0.80`이면 약 3.11회, `d_ng=0.93`이면 약 9.55회다. 초 단위 수명으로 환산하려면 실제 감쇠 갱신 빈도를 사용해야 한다. 또한 양쪽 점수가 양수이고 정리 임계값 위에 있을 때, `ng/g`는 감쇠마다 `d_ng/d_g`배가 된다. 이 설정에서는 해당 셀에 새 관측이 없어도 비지면 비율이 상승할 수 있다.

### 4.3 비지면 비율과 판정 진입 조건

```text
p_occ = ng_new / (ng_new + g_new + 1e−5)

높이 검사 조건:
    ng_new ≥ nonground_occ_thresh
    AND p_occ > nonground_prob_thresh
```

`p_occ`는 증거 점수의 비율이다. 센서의 관측 확률 모델이나 베이지안 log-odds 갱신을 사용하지 않는다. 절대 점수 조건은 적은 수의 점을 걸러내고, 비율 조건은 지면 증거와의 경쟁을 반영한다. [코드: 비율·임계값][layer-height]

`a_g=1.0`, `a_ng=1.5`, `d_g=0.80`, `d_ng=0.93`일 때 한 셀의 예시는 다음과 같다.

| 갱신 | 새 점 `N_g / N_ng` | `g_new` | `ng_new` | `p_occ` |
|---|---:|---:|---:|---:|
| 1 | 4 / 3 | 3.200 | 4.185 | 0.567 |
| 2 | 0 / 3 | 2.560 | 8.077 | 0.759 |

점수 기준 6.0, 비율 기준 0.75이면 둘째 갱신부터 높이 검사를 수행한다. 초기 점수가 0인 셀에 비지면점만 4개 들어오면 `4×1.5×0.93=5.58`로 점수 기준에 미달하고, 5개면 6.975로 통과한다. 따라서 임계값은 스캔 횟수보다 **셀에 들어오는 점 수·증분·감쇠**의 조합으로 해석해야 한다.

### 4.4 지역 지면 높이 추정

점수·비율 조건을 통과한 셀은 다음 순서로 지면 높이 `z_ground`를 찾는다.

1. 같은 셀에 지면 높이 표본이 있으면 `z_ground=S_z/M_z`를 사용한다.
2. 없으면 `ground_neighbor_search_cells=q` 범위에서 지면 표본이 있는 이웃을 찾는다.
3. 각 이웃 셀의 평균 높이를 구하고, 그 평균들의 산술평균을 사용한다.
4. 끝내 지면 기준을 찾지 못하면 비용을 LETHAL로 정한다.

```text
이웃 범위: Δi_x, Δi_y ∈ [−q,q], 중심 셀 제외
이웃 평균: z_ground = (1/K) Σ (S_z,j / M_z,j)
```

탐색 범위는 `(2q+1)×(2q+1)` 정사각형이다. `q=1`이면 최대 8개 이웃을 사용한다. 지면점 1개를 가진 셀과 100개를 가진 셀의 가중치는 같다. 거리 가중·평면 피팅·경사 보간은 수행하지 않는다. 높이 표본이 남아 있으면 지면 점수가 0으로 감쇠했어도 사용한다. [코드: 지면 높이][layer-height]

### 4.5 상대 높이 구간과 충돌 판정

비지면점의 Z 범위를 지역 지면 높이에 대해 상대화한다.

```text
h_min = z_min − z_ground
h_max = z_max − z_ground

h_min > robot_height   → FREE
h_max < min_clearance  → FREE
그 외                 → LETHAL
```

즉 비지면 높이 구간 `[h_min,h_max]`와 로봇의 검사 구간 `[min_clearance,robot_height]` 사이의 겹침을 검사한다. 경계값이 같으면 LETHAL이다. 개별 점 대신 min/max 구간을 사용하므로, 중간 높이에 실제 점이 없어도 구간이 겹치면 LETHAL이 된다. [코드: 높이 판정][layer-height]

`z_ground=0`, `min_clearance=0.10 m`, `robot_height=0.92 m`이고 점수 조건을 통과한 예시는 다음과 같다. `0.92 m`는 Nav2 튜토리얼의 로봇 높이 예시값이다.

| 비지면 Z 범위(m) | 높이 판정 | 결과 |
|---|---|---|
| 0.04–0.08 | 모두 검사 구간 아래 | FREE |
| 0.12–0.50 | 검사 구간과 겹침 | LETHAL |
| 1.20–1.50 | 모두 로봇 위 | FREE |
| 지면 표본 없음 | 상대 높이 계산 불가 | LETHAL |

높이는 costmap global frame의 Z로 계산한다. 따라서 입력 TF가 로봇의 Roll·Pitch·높이 변화를 일관되게 표현해야 한다.

### 4.6 점수와 높이의 시간 이력

증거 점수·지면 높이·비지면 높이에는 서로 다른 누적 규칙을 사용한다.

| 데이터 | 과거 관측 처리 |
|---|---|
| `g`, `ng` | 이전 점수에 새 증거를 더하고 감쇠 |
| `S_z`, `M_z` | 셀 삭제 전까지 모든 지면 높이 표본을 누적. `z_ground`는 같은 셀의 평균 또는 이웃 셀 평균들의 산술평균으로 계산 |
| `z_min`, `z_max` | 가장 최근에 비지면점이 들어온 갱신 구간의 범위 |

예를 들어 4.3절의 첫 관측은 지면점 4개와 높이 0.20–0.50 m의 상자점 3개, 둘째 관측은 높이 1.20–1.50 m의 비지면점 3개라고 하자. 지면 평균은 0 m로 유지된다. 둘째 관측의 점수에는 상자 증거가 남지만, 비지면 높이는 1.20–1.50 m로 교체된다. `p_occ≈0.759`로 높이 검사에 진입한 뒤 `h_min>0.92`이므로 FREE가 된다.

이처럼 점수는 감쇠 누적 이력, 지면 높이는 전체 누적 이력, 비지면 높이는 최근 구간 이력을 사용한다. 움직이는 물체나 가림이 바뀌는 장면을 분석할 때 이 세 시간 범위를 구분해야 한다. [코드: 높이 범위 교체·점수 갱신][layer-score]

### 4.7 비용 계산과 master costmap 반영

높이 검사 전 임시 비용은 다음과 같다. `uint8_t` 변환 시 소수 부분을 버린다.

```text
cost = uint8_t(clamp(252 × p_occ, 0, 252))
```

| 조건 | 기록 비용 |
|---|---|
| 높이 검사에서 충돌 또는 지면 기준 없음 | `LETHAL_OBSTACLE=254` |
| 높이 검사에서 로봇 위 또는 검사 구간 아래 | `FREE_SPACE=0` |
| 높이 검사 진입 전, `discretize_costs=false` | 임시 비용 0–252 |
| 높이 검사 진입 전, `discretize_costs=true` | `FREE_SPACE=0` |

플러그인은 UNKNOWN(255)을 직접 생성하지 않는다. `discretize_costs=true`에서는 최종 결과가 FREE 또는 LETHAL로 이산화된다.

`updateBounds()`와 `updateCosts()`의 처리 순서는 다음과 같다. [코드: 셀 갱신][layer-update]·[master 기록][layer-write]

1. `footprint_clearing_enabled=true`이면 로봇 footprint를 갱신 영역에 포함하고 내부 셀의 증거를 삭제한다.
2. 로봇과의 거리 제한 및 현재 costmap 범위 밖의 셀을 삭제한다.
3. 남은 셀의 증거를 누적·감쇠하고, 높이 판정과 비용을 계산한다.
4. 남은 셀들의 좌표를 costmap 갱신 영역에 포함한다.
5. Nav2가 전체 레이어의 갱신 영역을 모아 master의 해당 영역을 기본값으로 초기화한다.
6. 각 레이어가 순서대로 비용을 기록한다. Ground Consistency는 대응 셀이 있으면 master 비용을 **직접 대입**한다.
7. 옵션에 따라 로봇 footprint를 FREE로 덮어쓴다. 뒤에 배치한 Inflation Layer가 장애물 주변 비용을 확장한다.

직접 대입은 `max(이전 비용, 새 비용)` 병합과 다르다. 앞선 레이어가 기록한 LETHAL도 이 레이어의 FREE로 바뀔 수 있으므로 레이어 순서가 결과에 영향을 준다.

삭제된 셀의 이전 좌표는 이 플러그인의 갱신 bounds에 포함하지 않는다. 그 좌표가 다른 갱신 영역에 포함되면 master 초기화 후 기본값 또는 다른 레이어 결과로 바뀌지만, 모든 bounds 밖이면 이전 비용이 남을 수 있다. 센서 광선을 따라 자유 공간을 지우는 raytracing은 없다. [Nav2: 갱신 영역 초기화·레이어 적용][master]

![지면·비지면 점군과 local costmap](https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/assets/rviz_window.png)

---

## 5. 파라미터와 조정 기준

### 5.1 GSeg3D

아래 예시값은 공개 ROS 2 래퍼의 `config/parameters.yaml` 기준이다. 라이브러리 `GridConfig`의 생성자 기본값과 구분한다. [설정 예시][gseg-config]·[라이브러리 기본값][core-defaults]

| 파라미터 | 단위 | 예시값 | 조정 대상 |
|---|---|---:|---|
| `cellSizeX`, `cellSizeY` | m | 1.0, 1.0 | 국소 분석 영역의 가로 크기. 작으면 희소 복셀이 늘고, 크면 여러 구조가 섞인다. |
| `cellSizeZ` | m | 10.0 | 1차의 수직 분석 범위 |
| `cellSizeZPhase2` | m | 1.0 | 2차의 수직 분해능 |
| `groundInlierThreshold` | m | 0.2 | 평면 inlier 거리 및 1차 점 복구 거리 |
| `slopeThresholdDegrees` | ° | 20.0 | LINE·PLANE의 지면 후보 경사 한계 |
| `centroidSearchRadius` | m | 5.0 | 후보 centroid를 연결하는 3D 반경 |
| `maxGroundHeightDeviation` | m | 0.1 | 2차 영역 확장의 높이 차이, 양 단계 모호성 검사의 높이 차이 |
| `lidar_to_ground` | m | −1.78 | 로봇 Z축 기준 LiDAR 원점→예상 지면 변위 |
| `use_imu_orientation` | bool | false | 중력 기준 경사 검사 사용 |
| `downsample`, `downsample_resolution` | bool, m | false, 0.1 | 입력 점 수와 표면 세부 형상 |
| `minX/maxX`, `minY/maxY`, `minZ/maxZ` | m | 각 −100/100 | 로봇 프레임의 처리 범위 |
| `transform_tolerance` | s | 0.1 | TF 조회 대기시간 |

`groundInlierThreshold`를 늘리면 거친 지면점이 inlier로 남기 쉬워지지만 낮은 돌출물도 포함될 수 있다. XY 복셀·다운샘플링 크기를 바꾸면 점 수가 달라져 PCA 적용 여부, 희소도 등급, 출력 누락 비율도 함께 바뀐다. 경사 한계와 연결 반경은 각각 **국소 방향**과 **공간적 연결**을 조정하는 값이다.

### 5.2 Ground Consistency

아래는 README 설명 대신 `onInitialize()`의 코드 기본값을 사용한다. [코드: 파라미터 선언][layer-params]

| 파라미터 | 단위 | 코드 기본값 | 동작 |
|---|---|---:|---|
| `ground_inc`, `nonground_inc` | score/point | 1.0, 1.5 | 점당 증거 증가량 |
| `ground_decay`, `nonground_decay` | 배율/update | 0.80, 0.93 | 전역 새 데이터 갱신당 감쇠 |
| `max_score` | score | 5000.0 | 감쇠 전 점수 상한 |
| `nonground_occ_thresh` | score | 6.0 | 높이 검사 진입의 절대 점수 기준 |
| `nonground_prob_thresh` | 비율 | 0.75 | 높이 검사 진입의 비지면 비율 기준 |
| `min_clearance` | m | 0.1 | 상대 높이 검사 구간의 하한 |
| `robot_height` | m | 1.2 | 상대 높이 검사 구간의 상한 |
| `maximum_height_filter` | m | `double` 최댓값 | 입력 프레임 Z값의 비지면 필터. 0이면 Z>0인 점이 제거된다. |
| `ground_neighbor_search_cells` | cell | 0 | 같은 셀에 지면 표본이 없을 때 이웃 탐색 범위 |
| `max_data_range` | m | 50.0 | 로봇 주변 XY 데이터 보관 반경. 0 이하이면 이 거리 제한을 끈다. |
| `discretize_costs` | bool | true | 중간 비용을 FREE로 이산화 |
| `footprint_clearing_enabled` | bool | true | footprint 증거 삭제 및 master FREE 처리 |
| `tf_timeout` | s | 0.1 | 입력 클라우드 TF 조회 대기시간 |

costmap 해상도가 커지면 여러 점이 한 셀에 모여 점수가 빨리 증가하고, 다른 높이의 구조도 함께 집계된다. 해상도나 입력 다운샘플링을 바꾼 뒤에는 절대 점수 기준을 다시 평가해야 한다. 이웃 높이 평균은 연석·경사 전환부·서로 다른 층이 가까운 곳에서 기준 높이를 섞을 수 있다.

---

## 6. 논문과 구현의 차이 및 구현 제약

### 6.1 GSeg3D 논문과의 비교

| 항목 | 논문 설명 | 분석한 코드 |
|---|---|---|
| 선형 구조 각도 | §III-B의 각도 부등식 | 주방향이 수평에 가까운 `α_line>90°−β`를 지면 후보로 처리 |
| 희소도 부피 | inlier·outlier 각각의 bounding box | 복셀 전체 점의 bounding box를 양쪽 계산에 공통 사용 |
| 시작 지면 | 로봇 아래 합성 지면점 | 시작 복셀과 KD-tree용 합성 좌표만 추가 |
| 2차 정밀화 | 작은 수직 격자로 지면 결과 정밀화 | 1차 지면점만 입력, 비지면 결과는 두 단계 결과를 이어 붙임 |

알고리즘 설명과 코드 재현에서는 위 차이를 구분한다. [논문 §III][paper]·[핵심 구현][core]

### 6.2 코드 수준의 예외

| 항목 | 확인된 제약 |
|---|---|
| GSeg3D 상태 초기화 | `GridCell` 생성자가 `terrain_type`, `primitive_type`, `centroid`, `normal`을 초기화하지 않는다. 3–5점 GROUND 이웃의 법선을 1차 복구에서, 빈 시작 복셀의 centroid를 주변 높이 검사에서 읽는 경로가 있다. |
| GSeg3D 수치 처리 | 고유값 합 0, 고유분해 실패, `acos` 내적의 수치 오차에 대한 명시적 방어가 없다. |
| IMU 입력 검증 | orientation 제공 여부(`orientation_covariance[0]`), 유한성·영 쿼터니언을 검사하지 않는다. |
| 지면 참조 이웃 | GSeg3D 모호성 검사와 1차 복구는 후보 상태를 참조하며, 해당 이웃의 최종 지면 출력 여부까지 검증하지 않는다. |
| Consistency 이웃 높이 | 셀 갱신과 삭제를 같은 해시맵 순회에서 수행한다. 아직 삭제되지 않은 이웃 높이가 참조될 수 있다. |
| 센서 중단 | Consistency 점수는 새 입력 없이는 감쇠하지 않는다. 입력 timeout으로 `current_`를 갱신하는 처리도 없다. |
| 출력·비용 삭제 | GSeg3D의 누락·중복은 3.8절, 내부 셀 삭제 후 master 잔상은 4.7절의 규칙을 따른다. |

미초기화 값이 있는 경로는 컴파일·실행 환경에 따라 결과가 달라질 수 있다. 이를 정상적인 지면 판정 규칙과 분리해 다룬다. [코드: 셀 생성자][core-types]·[래퍼][wrapper]·[costmap 갱신][layer-update]

---

## 7. 검증 지표

지면 분류, 2D 비용, 시간 응답을 분리해서 평가한다. 아래 Precision·Recall은 지면을 양성으로 정의한다.

```text
TP: 실제 지면을 지면으로 분류
FP: 실제 비지면을 지면으로 분류
FN: 실제 지면을 비지면으로 분류

Precision = TP / (TP+FP)
Recall    = TP / (TP+FN)
F1        = 2 × Precision × Recall / (Precision+Recall)
```

| 검증 대상 | 확인할 값 |
|---|---|
| 지면 분류 | Precision·Recall·F1, 거리별·경사별 결과 |
| 출력 보존 | 전처리 입력 대비 누락률, 중복률, 지면·비지면 교집합 |
| 낮은 장애물 | 지면점으로 흡수된 비지면점, 높이 기준 아래에서 FREE가 된 셀 |
| 경사·층 변화 | 지면 기준 높이 오차, 연결이 끊기거나 다른 표면으로 확장되는 위치 |
| 동적 물체 | 감지→LETHAL 지연, 물체 제거→비용 해제 지연 |
| 센서 이상 | IMU 누락·TF 지연·빈 클라우드·입력 중단 시 출력과 기존 비용 |
| 실행시간 | 두 분리 단계 시간과 입력 timestamp→costmap 반영 지연을 각각 측정 |

누락점은 별도 항목으로 기록하고 평가 대상에서 빠진 비율을 함께 제시한다. 래퍼의 `show_benchmark`는 두 단계의 입력 설정·분리와 비지면 결과 결합을 측정한다. TF 대기·전처리·메시지 발행·costmap 반영시간은 제외한다. [코드: 시간 측정][wrapper-passes]

---

## 참고 자료

- [GSeg3D 논문][paper]
- [Nav2 공식 튜토리얼: Ground Terrain Segmentation using 3D Lidar][tutorial]
- [GSeg3D 핵심 라이브러리][core]·[ROS 2 래퍼][wrapper]
- [PCL PROSAC][prosac]·[평면 모델][pcl-plane]
- [Ground Consistency Layer][layer]·[Nav2 LayeredCostmap][master]

[paper]: https://arxiv.org/pdf/2603.04208
[tutorial]: https://docs.nav2.org/rolling/tutorials/general_tutorials/navigation2_with_ground_consistency_layer/navigation2_with_ground_consistency_layer/
[core]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp
[core-types]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection_types.hpp#L94-L129
[core-defaults]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection_types.hpp#L182-L213
[core-voxel]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L353-L370
[core-classify]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L510-L630
[core-slope]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L372-L390
[core-fit]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L473-L492
[core-grow]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L635-L748
[core-output]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L775-L895
[core-sparsity]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L392-L416
[core-recover]: https://github.com/dfki-ric/ground_segmentation/blob/e5aa4c2c47a961eae491056cf616fd415ff3d602/include/ground_detection.hpp#L266-L335
[wrapper]: https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp
[wrapper-pre]: https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L164-L255
[wrapper-height]: https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L193-L201
[wrapper-imu]: https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L213-L244
[wrapper-sync]: https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L59-L75
[wrapper-passes]: https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/src/ground_segmentation_ros2_node.cpp#L257-L300
[gseg-config]: https://github.com/dfki-ric/ground_segmentation_ros2/blob/c0d60fd8ddcf561d006a907e04c84f9a8847372a/config/parameters.yaml
[prosac]: https://github.com/PointCloudLibrary/pcl/blob/pcl-1.14.1/sample_consensus/include/pcl/sample_consensus/impl/prosac.hpp
[pcl-plane]: https://github.com/PointCloudLibrary/pcl/blob/pcl-1.14.1/sample_consensus/include/pcl/sample_consensus/impl/sac_model_plane.hpp
[layer]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp
[layer-key]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/include/nav2_ground_consistency_costmap_plugin/ground_consistency_layer.hpp#L49-L67
[layer-input]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L468-L580
[layer-score]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L296-L337
[layer-height]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L343-L405
[layer-update]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L205-L435
[layer-write]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L583-L614
[layer-params]: https://github.com/dfki-ric/nav2_ground_consistency_costmap_plugin/blob/41cec620efba6c370dccfc59a6ec1134775ff48a/src/ground_consistency_layer.cpp#L61-L78
[master]: https://github.com/ros-navigation/navigation2/blob/645abd95f2be02a13ca539b29c2ddc065db34f89/nav2_costmap_2d/src/layered_costmap.cpp#L204-L239
