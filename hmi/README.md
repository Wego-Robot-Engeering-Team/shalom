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
