# CD — 고객 런타임 배포

이 디렉터리는 소스 저장소를 고객에게 전달하지 않고, 검증된 실행 산출물만
배포하기 위한 기준을 둔다. CI/CD 시스템의 설정 파일을 무조건 두는 장소가
아니다. 고객 설치물의 구조, 산출물 계약, 검증·업데이트 기준을 한 곳에서
관리한다.

## 결론

| 대상 | 고객 전달물 | 설치 방식 |
| --- | --- | --- |
| 로봇 컴퓨터 | 타깃별 Release 런타임 번들 | 오프라인 bundle → `.deb` |
| 관제 HMI | 별도 portable HMI 패키지 | 압축 해제 후 실행 |
| 개발자 | 이 Git 저장소와 submodule | `scripts/install.sh` |

고객 장비에는 Git 저장소, `src/`, `third_party/`, compiler, CMake, colcon을
설치하지 않는다. 이 저장소의 `scripts/install.sh`는 **개발/통합 환경용**이며
고객 런타임 설치 도구가 아니다.

```text
비공개 개발 저장소
  └─ 타깃과 동일한 OS·ROS·CPU 환경에서 Release 빌드
       └─ 검증된 산출물 + manifest + LICENSES
            └─ 고객 장비에 설치
```

## 배포 단위

로봇과 HMI는 CPU, 운영체제, 갱신 주기와 장애 범위가 달라 하나의 설치물로
묶지 않는다.

```text
robot runtime                         HMI runtime
Jetson / Ubuntu / ARM64               관제 PC / x86_64 또는 Windows
ROS 2 Jazzy와 vendor driver 포함       Qt application
systemd로 부팅·감시                    portable package 또는 OS installer
```

- 로봇 런타임 설계와 검증: [robot/README.md](robot/README.md)
- HMI 납품 패키지 기준: [hmi/README.md](hmi/README.md)
- 외부 연동용 상태 모니터 sample: [samples/bridge_monitor_cpp](samples/bridge_monitor_cpp)
- 출하 전 공통 확인: [release-checklist.md](release-checklist.md)

## 고객 로봇 파일 시스템 계약

```text
/opt/shalom/
  releases/<version>/      변경하지 않는 프로그램 런타임
  current -> releases/...  현재 활성 버전

/etc/shalom/               장비별 설정·보정값·권한 제한 인증정보
/var/lib/shalom/           지도·미션·운영 상태
/var/log/shalom/           서비스 로그
```

프로그램 패키지는 `/opt/shalom/releases/<version>`만 만든다. 로봇별 IP,
센서 시리얼, calibration, ROS domain, 지도와 미션은 `/etc`·`/var/lib`에
두며, 업데이트가 이를 덮어쓰지 않는다.

## 버전과 재현성

릴리스마다 다음을 `manifest.json`으로 기록한다.

- 제품 버전과 빌드 시각
- 최상위 Git commit 및 모든 submodule commit
- 타깃 OS, CPU architecture, ROS distribution
- OS/ROS/vendor 의존성 버전과 패키지 해시
- 각 배포 파일의 SHA-256

개발 빌드의 `--symlink-install`은 금지한다. Release 빌드는 실제 파일을
설치하고, runtime artifact만 조립한다.

## 업데이트 원칙

기존 설치를 덮어쓰지 않는다.

```text
새 release 설치 → checksum/의존성 검증 → 서비스 정지
→ current 링크 전환 → 서비스 시작·health check
→ 실패하면 이전 release 링크로 복구
```

초기에는 타깃별 오프라인 번들을 사용하고, 현장 검증이 끝난 뒤 서명된 `.deb`
및 사내 APT 저장소로 전환한다. 컨테이너는 빌드 재현성에는 유용하지만,
USB/udev/UDP/실시간 vendor driver가 있는 첫 실기 배포의 기본 수단으로는
사용하지 않는다.
