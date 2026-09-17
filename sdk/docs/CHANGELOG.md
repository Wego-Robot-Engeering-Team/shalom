# 규격 변경 이력

버전은 봉투의 `v` 필드다. 기존 필드의 **의미가 바뀌면** 올린다. 채널 추가와
필드 추가는 올리지 않는다.

## SDK 0.2.0 / protocol v1

- Linux, macOS, Windows 배포 단위를 `cpp/`와 `python/`으로 명확히 분리했다.
- C++ `shalom::RobotApi` 명령 facade를 추가했다.
- Python 표준 라이브러리 client와 read-only monitor sample을 추가했다.
- macOS socket의 broken-pipe 처리를 추가했다.

프로토콜 봉투와 채널 의미는 바뀌지 않았으므로 protocol version은 `v:1`을 유지한다.

## SDK 0.1.0 / protocol v1

최초 규격. `docs/bridge_protocol.md` 의 내용을 외부 연동용으로 정리했다.
