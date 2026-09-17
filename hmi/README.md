# Inspection HMI

로봇 브릿지에 TCP로 연결하는 관제 프로그램이다. 통신 규약은
[bridge_protocol.md](../docs/bridge_protocol.md)를 참고한다.

## 개발 빌드와 실행

모든 명령은 이 디렉터리(`shalom/hmi`)에서 실행한다.

```bash
cmake --preset dev
cmake --build --preset dev
./build/inspection_hmi
```

기준 실행 파일은 항상 `hmi/build/inspection_hmi`다. 작업공간 루트의
`build/` 또는 `install/` 아래 실행 파일은 사용하지 않는다.

### 연결 대상 지정

```bash
# 저장된 연결 설정으로 브릿지에 접속
./build/inspection_hmi

# 이번 실행에만 브릿지 주소와 포트를 지정
./build/inspection_hmi --host 192.168.210.88 --port 9090

# 로봇팔 화면부터 열기
./build/inspection_hmi --view arm
```

## 실행 방식

| 목적 | HMI 실행 | 데이터 원천 |
|---|---|---|
| 실기 운용 | `./build/inspection_hmi --host <로봇-IP>` | 로봇 브릿지 |
| 로봇 시뮬레이터 연동 | `./build/inspection_hmi --host <브릿지-IP>` | 시뮬레이터가 제공하는 브릿지 |
| 연결 전 화면 확인 | `./build/inspection_hmi` | 연결된 로봇 없음 |

실기와 로봇 시뮬레이터는 HMI 입장에서 모두 브릿지 연결이다. 차이는 로봇 쪽에
있으며 HMI 실행 인자가 다르지 않다. 인자 없이 실행하면 빈 HMI가 열리고,
설정에서 로봇을 등록한 뒤 상단 목록에서 명시적으로 선택해 연결한다. 3D
로봇팔 미리보기는 별도 실행 방식이 아니라 입력 중인 목표 자세를 보여 주는 기능이다.

## 테스트

```bash
ctest --preset dev --output-on-failure
```

## Release 빌드

```bash
cmake --preset release
cmake --build --preset release
cmake --install build-release --prefix dist/inspection-hmi
```


## 실행 인자

| 인자 | 설명 |
|---|---|
| `--host <주소>` | 이번 실행에만 브릿지 주소를 지정한다. |
| `--port <번호>` | 이번 실행에만 브릿지 포트를 지정한다. 기본값은 `9090`이다. |
| `--view <화면>` | 시작 화면을 지정한다. `drive`, `locations`, `arm`, `capture`, `diagnostics`, `data`, `events`를 사용할 수 있다. |
| `--dark` | 다크 테마로 시작한다. |

## 개발 전용 인자

| 인자 | 설명 |
|---|---|
| `--manual` | 수동 주행 조작 패널을 표시한다. |
| `--samples <경로>` | 이번 실행에만 촬영 표본 폴더를 지정한다. |
| `--size <너비>x<높이>` | 창 크기를 지정한다. 예: `--size 1280x760` |
| `--shot <파일>` | 로그인 후 화면을 캡처하고 종료한다. |
| `--shot-dialog <이름> <파일>` | 대화상자를 캡처하고 종료한다. 이름: `welcome`, `notifications`, `settings[:탭번호]` |
