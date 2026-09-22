# Robot runtime

로봇 배포물은 소스 워크스페이스가 아닌, 특정 하드웨어와 운영체제 조합을 위한
실행 전용 Release artifact다.

## 1. 대상 고정

산출물 하나는 하나의 실행 환경만 지원한다.

```text
예: Jetson Orin / ARM64 / Ubuntu 24.04 / ROS 2 Jazzy
```

x86_64 개발 PC나 macOS에서 빌드한 executable·`.so`를 Jetson에 복사하지
않는다. 타깃 장비에서 native build하거나, 같은 architecture·OS·ROS의
격리된 build runner에서 빌드한다.

## 2. 런타임 내용

```text
shalom-robot-<version>-<target>.tar.zst
├── packages/              내부 runtime `.deb`와 필요한 의존성 `.deb`
├── manifest.json
├── checksums.txt
├── LICENSES/
└── install.sh
```

`.deb` 내부의 프로그램은 다음과 같이 배치한다.

```text
/opt/shalom/releases/<version>/
├── bin/                   node executable, bootstrap program
├── lib/                   내부 shared library (.so)
├── share/                 ROS resource index, launch, config, RViz
└── setup.bash             ROS package environment
```

ROS 2 package discovery를 위해 resource index, `package.xml`, 필요한
`share/<package>` 파일은 남긴다. 반면 `src/`, Git metadata, test, compiler,
개발용 header와 CMake export 파일은 고객 artifact에서 제외한다.

## 3. 코드 보호 경계

`mission_manager`, FSM/BT, safety, motion interlock, protocol 같은 핵심
도메인 로직은 C++ Release executable과 `.so`로 배포한다. 고객본에는 strip한
바이너리만 두고 debug symbol은 사내 artifact 저장소에 별도 보관한다.

Python launch와 YAML은 읽을 수 있다. 따라서 launch는 프로세스 조립과
설정 전달에만 사용하고, 미션 판단·안전 정책·BT 구현을 넣지 않는다. launch를
XML로 바꾸어도 코드 보호 효과는 없다.

`.so`는 소스 미제공 수단일 뿐 역공학 방지 장치는 아니다. 계약, 접근 통제,
서명된 업데이트, 라이선스 준수와 함께 적용한다.

## 4. 실행과 안전 경계

고객은 `ros2 launch`를 직접 실행하지 않는다. systemd 서비스가 환경과
launch argument를 고정해 시작한다.

```text
systemd
  └─ shalom-robot.service
       └─ runtime bootstrap
            └─ robot_bringup
```

로봇별 환경은 `/etc/shalom/robot.env`와 `/etc/shalom/*.yaml`에 둔다. 현재
control-plane은 driver 명령 경로와의 통합 검증 전 단계이므로, 실제 납품 전에는
아래 단일 경로를 hardware-in-the-loop로 검증해야 한다.

```text
mission / nav2 / dock / stair / teleop
                 ↓
            twist_mux
                 ↓
            safety_gate
                 ↓
             B2 / FR3 driver
```

## 5. 업데이트와 롤백

업데이트는 새 version directory를 만들고 검증한 뒤 `current` 링크만 바꾼다.
문제가 생기면 링크를 이전 버전으로 되돌리고 서비스를 재시작한다. 설정, 지도,
미션 데이터는 release directory 밖에 있으므로 롤백 때 보존된다.

## 6. 구현 순서

1. Jetson/Jazzy clean environment에서 전체 실기 워크스페이스 빌드를 성공시킨다.
2. runtime artifact에 포함할 ROS package·vendor library·OS 의존성을 확정한다.
3. `packaging` build가 Release 설치본, manifest, LICENSES, checksum을 만든다.
4. systemd와 health check를 추가한다.
5. Git·compiler·인터넷이 없는 깨끗한 장비에서 설치, 재부팅, HMI 연결을 시험한다.
6. 정상 동작, 업데이트, 실패 롤백, 장비 연결 손실, 안전 상태를 실기로 검증한다.

`third_party`와 ROS/vendor 라이브러리는 재배포 조건이 각각 다르다. artifact를
만들기 전에 component별 LICENSE/NOTICE 및 source-offer 의무를 검토한다.
