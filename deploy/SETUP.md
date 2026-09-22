# 릴리스 자동화 사전 설정

이 문서는 `deploy/`의 패키징·릴리스 자동화를 처음 연결하기 전에 한 번 설정하는
항목을 정리한다. 고객 납품용 비밀값, 현장 센서 IP, 고객별 지도는 Git 저장소와
CI secret에 넣지 않는다.

관련 산출물 구조와 라이선스 기준은 [README.md](README.md)를 따른다.

## 1. 기준 버전과 Git 규칙

릴리스의 기준 소스는 `main`이며, 고객에게 전달할 실제 버전은 annotated tag로
고정한다.

```text
feat/*, fix/* → dev → release/<version> → main → v<version>
```

| 항목 | 설정 |
|---|---|
| `dev` | 기능 통합·일반 빌드 검사 브랜치 |
| `release/<version>` | 기능 추가 금지, 버전·문서·패키징만 조정 |
| `main` | 납품 가능한 소스 기준선, 직접 push 금지 |
| `v<version>` | 예: `v0.1.0`. 이 tag만 고객 릴리스 생성 |
| submodule | 모든 경로와 commit을 tag 시점에 고정 |

릴리스 tag 전 필수 확인:

```bash
git status --short
git submodule status --recursive
git log -1 --format='%H%n%cs%n%s'
```

서드파티 작업 트리가 dirty면 자동 릴리스를 시작하지 않는다. Aurora·FAIRINO·
RealSense처럼 별도 권리 검토가 필요한 submodule의 로컬 수정은 일반 Shalom
릴리스 커밋에 섞지 않는다.

## 2. 빌드 환경

각 산출물은 대상 OS·아키텍처에서 빌드한다. 개발자의 기존 `build/`, `install/`,
`log/` 디렉터리를 고객 패키지에 재사용하지 않는다.

| 산출물 | 빌드 runner | 필수 기준 |
|---|---|---|
| Robot runtime | Jetson Orin AGX self-hosted | JetPack 7.2.1, Ubuntu 24.04, arm64, ROS 2 Jazzy |
| Linux HMI·SDK | Ubuntu 24.04 x86_64 | GCC, CMake, Qt 6, Python 3 |
| Windows HMI·SDK | Windows x86_64 | MSVC, CMake, 동일 Qt 6 계열, Python 3 |
| Manifest·LICENSES 조립 | 신뢰된 Linux x86_64 | git, dpkg tools, Python 3, archive/signing tools |

로봇 runner에는 실제 로봇을 연결하지 않는다. 별도 빌드용 Jetson을 사용하고,
ROS Domain·Unitree DDS 인터페이스가 외부 실기에 닿지 않도록 격리한다.

### Jetson runner 기본 패키지

```bash
sudo apt update
sudo apt install -y \
  git git-lfs build-essential cmake ninja-build pkg-config \
  python3 python3-venv python3-pip python3-rosdep \
  dpkg-dev fakeroot devscripts zstd \
  jq python3-yaml
```

ROS 2 Jazzy, Nav2, SLAM Toolbox, CycloneDDS, `twist_mux` 등 실제 runtime
의존성은 대상 릴리스의 고정 목록을 설치한다. `scripts/install.sh`는 개발 환경
설치용이며, 고객 `.deb`를 만드는 자동화 job에서 고객 설정을 생성하거나 수정하면
안 된다.

### 공통 환경 기록

다음 값은 각 runner에서 기록하고 `manifest.json`에도 넣는다.

```bash
uname -m
. /etc/os-release && echo "$ID $VERSION_ID"
gcc --version | head -1
cmake --version | head -1
ros2 --version
```

ROS·Qt·컴파일러가 바뀌면 같은 Shalom source commit이라도 산출물이 달라질 수
있다. 릴리스 도중 자동 apt upgrade를 수행하지 않는다.

## 3. GitHub Actions runner 등록

현재 원격 저장소는 GitHub `origin`을 사용한다. `.github/workflows/`를 추가하기
전에 각 runner를 등록하고 라벨을 고정한다.

| runner | 권장 `runs-on` | 사용 범위 |
|---|---|---|
| Jetson | `[self-hosted, linux, ARM64, jetson-orin, jetpack-7]` | robot `.deb`, arm64 smoke test |
| Linux client | `[self-hosted, linux, x64, ubuntu-24.04]` 또는 승인된 hosted runner | Linux HMI·SDK |
| Windows client | `[self-hosted, windows, x64, msvc-qt6]` 또는 승인된 hosted runner | Windows HMI·SDK |
| Release assembler | `[self-hosted, linux, x64, release-signing]` | manifest·서명·GitHub Release |

Jetson runner는 장기 보존되는 호스트이므로 안전하지 않은 PR 코드를 실행하면 안 된다.
다음 기준을 workflow에 적용한다.

- `pull_request` job에는 self-hosted Jetson runner를 쓰지 않는다.
- Jetson build는 `release/*` push와 `v*` tag에서만 실행한다.
- release signing runner는 `v*` tag의 승인된 publish job에서만 실행한다.
- fork PR에는 write token, deploy secret, self-hosted runner를 제공하지 않는다.

## 4. GitHub 저장소 보호 설정

GitHub 저장소에서 다음을 설정한다.

| 대상 | 필수 설정 |
|---|---|
| `main` | 직접 push 금지, PR 필수, 상태 검사 필수 |
| `release/*` | `dev`에서의 PR만 허용, 패키징 검증 필수 |
| tag `v*` | Release workflow만 publish 가능 |
| Environment `release` | 승인자 지정, release signing secret 접근 제한 |
| Actions 권한 | 기본 `contents: read`; publish job만 `contents: write` |

`release` Environment 승인이 불가능한 GitHub 요금제라면, publish workflow를
`workflow_dispatch`로 분리하고 릴리스 담당자만 수동 실행하도록 한다.

## 5. CI secret과 서명 키

Secret에는 고객 현장값을 넣지 않는다. 필요한 것은 릴리스 생성·서명용 키뿐이다.

| 이름 | 사용처 | 보관 위치 |
|---|---|---|
| `RELEASE_SIGNING_KEY` | checksum/manifest 서명 | GitHub `release` Environment secret |
| `RELEASE_SIGNING_PASSPHRASE` | 서명 키 해제 | 같은 Environment secret |
| `RELEASE_SIGNING_KEY_ID` | 공개키 식별 | workflow variable 또는 manifest |
| `GH_TOKEN` | GitHub Release asset 업로드 | GitHub 제공 token 또는 제한된 PAT |
| `SUBMODULE_DEPLOY_KEY` | private submodule checkout 시에만 | repository/organization secret |

가능하면 개인 GPG key 대신 릴리스 전용 서명키를 만들고, 공개키와 검증 명령을
고객 릴리스의 `RELEASE.md`에 포함한다. private key와 passphrase는 artifact,
manifest, build log에 출력하지 않는다.

## 6. 릴리스 입력 파일

고객 릴리스에 공통으로 필요한 입력은 Git에서 버전 관리한다. 고객·현장별 값은
커미셔닝 때 별도 제공한다.

```text
deploy/
├── README.md
├── SETUP.md
├── templates/                 # 이후 추가
│   ├── RELEASE.md.in
│   ├── manifest.json.in
│   └── install.sh.in
├── packaging/                 # 이후 추가
│   ├── robot-runtime/debian/
│   └── site-config/debian/
└── scripts/                   # 이후 추가
```

Git에서 관리해도 되는 값:

- 기본 `site-config` 템플릿
- systemd unit template
- 공개 API protocol version
- 기본 설치·업데이트 절차
- 라이선스 전문과 NOTICE template

Git 또는 CI secret에 넣지 않는 값:

- 고객망 IP·계정·비밀번호·private key
- Aurora·FR3 등 현장 장비의 최종 IP·보정값
- 고객 지도·waypoint·촬영 결과
- 고객 인증서와 VPN 정보

## 7. 라이선스 입력 준비

릴리스 생성 전에 `LICENSES/` 자동 수집의 입력을 확정한다.

| 입력 | 준비 방식 |
|---|---|
| bundled source license | 각 허용 submodule의 `LICENSE`·`NOTICE` 경로 목록 |
| apt copyright | arm64 build runner의 `/usr/share/doc/<package>/copyright` |
| Aurora 서면 허락 | 법무·구매 증빙 식별자와 사본 보관 위치 |
| FAIRINO 권리 근거 | 벤더 답변 또는 해당 runtime 제외 결정 |
| LGPL source offer | 배포 기간, 대응 소스 위치, 요청 연락처 |

라이선스 입력이 하나라도 없으면 `assemble-release` job이 실패해야 한다. 경고만
남기고 빈 `LICENSES/` 디렉터리를 만드는 방식은 허용하지 않는다.

## 8. 릴리스 전 수동 승인 입력

자동화가 대체할 수 없는 승인 항목이다.

- 납품 대상 장비: Jetson Orin AGX, JetPack/Ubuntu/ROS 버전
- 포함 센서: B2, XT32, Aurora, FR3의 실제 포함 여부
- 제외 자산: D455, VLP-16, MuJoCo, 학습 자산, 개발 도구
- 고객 SDK 권한: 조회 전용 / 미션 제어 허용 범위 / HMI 동시 접속 정책
- 고객 기본 site-config와 지도 제공 여부
- Aurora·FAIRINO 재배포 권리 확인 상태
- RELEASE.md의 알려진 제한·업데이트·rollback 절차

이 승인 결과는 tag와 함께 `RELEASE.md` 및 `manifest.json`에 남긴다.

## 9. 사전 설정 완료 기준

다음이 모두 충족돼야 첫 자동 릴리스를 구현한다.

- [ ] `main`, `release/*`, `v*`의 Git 규칙과 보호 설정 적용
- [ ] Jetson arm64 self-hosted runner 등록 및 네트워크 격리
- [ ] Linux·Windows HMI/SDK 빌드 runner 준비
- [ ] release signing key와 GitHub Environment secret 설정
- [ ] private submodule checkout 권한 확인
- [ ] Jetson에서 ROS 2 Jazzy 및 `.deb` 빌드 도구 확인
- [ ] Windows에서 MSVC·Qt 버전 확인
- [ ] Aurora·FAIRINO 포함 여부와 권리 근거 확정
- [ ] 고객 설정·지도·인증서를 Git/CI에서 분리
- [ ] 첫 대상 버전과 `RELEASE.md` 입력 확정

이후에야 `deploy/scripts/`와 `.github/workflows/`를 추가한다.
