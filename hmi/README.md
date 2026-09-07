# HMI

Qt 6 기반 관제 GUI. 로봇과는 raw TCP로 통신하며 규약은
[bridge_protocol.md](../docs/bridge_protocol.md)를 따른다.

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
