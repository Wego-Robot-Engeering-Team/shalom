# Inspection HMI

로봇 브릿지에 TCP로 연결하는 관제 프로그램이다. 통신 규약은
[bridge_protocol.md](../docs/bridge_protocol.md)를 참고한다.

## 실행

모든 명령은 이 디렉터리(`shalom/hmi`)에서 실행한다.

```bash
cmake --preset default
cmake --build --preset default --parallel 4
./build/inspection_hmi
```

납품할 때도 같은 실행 파일을 설치 경로로 복사한다. 테스트 실행 파일과
유지보수 도구는 설치 대상이 아니다.

```bash
cmake --install build --prefix dist/inspection-hmi
```

처음 실행하면 HMI만 열리며 어떤 로봇에도 자동 연결하지 않는다. 설정에서
로봇 이름과 IP를 등록한다. 이후 실행부터는 마지막으로 선택한 로봇의 상태
확인 포트가 응답할 때만 자동 연결하며, 상단 목록의 빨강·초록 점으로 등록된
각 로봇의 응답 상태를 확인할 수 있다.

실기와 시뮬레이터는 모두 브릿지가 있는 로봇으로 등록한다. HMI는 둘을
구별하지 않으며, 3D 팔은 받은 관절 상태와 입력 중인 목표를 표시하는 화면 기능이다.

## 테스트

```bash
/usr/bin/ctest --preset default --output-on-failure
```
