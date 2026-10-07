# 오류 처리

```json
{"ok":false,"err":{"code":"E_BUSY","msg":"미션 revision이 바뀌었습니다"}}
```

`err.code`로 분기하고 `err.msg`는 화면에 표시한다.
성공 응답에는 `file` 등 명령별 결과 필드가 포함될 수 있다.

| 코드 | 처리 |
| --- | --- |
| `E_VERSION` | 프로토콜 호환성 확인 |
| `E_ROBOT_MISMATCH` | 연결 종료 후 대상 로봇 확인 |
| `E_BAD_PAYLOAD` | 필드·타입·참조 수정 |
| `E_BUSY` | 실행 상태 또는 편집 원본·revision 확인 |
| `E_MODE` | 모드·안전·선행 조건 확인 |
| `E_ESTOP_ENGAGED` | 비상정지 상태 확인 |
| `E_UNREACHABLE` | 내비게이션·기능 준비 상태 확인 |
| `E_LIMIT` | 관절·속도 제한 확인 |
| `E_HARDWARE` | 드라이버·저장소·파일 상태 확인 |
| `E_UNKNOWN_CHANNEL` | 로봇과 SDK의 기능 지원 범위 확인 |

Python 전송 오류는 `ClientError`, 연결 종료는 `ConnectionClosed`,
잘못된 규격은 `ProtocolError`, 로봇 ID 변경은 `RobotMismatchError`,
응답 시간 초과는 `RequestTimeout`이다.
C++는 실패 반환값과 `err`를 사용한다.

타임아웃은 로봇의 실행 결과가 불확실한 상태다.
이동·촬영·저장 명령을 재전송하기 전에 상태·목록·revision을 확인한다.
정지·취소는 해당 제어 명령으로 명시적으로 요청한다.

`evt/log`의 이벤트는 [error_codes.json](error_codes.json)을 code로 조회한다.
이 파일은 HMI 오류 카탈로그와 동기화하며 테스트에서 일치 여부를 검사한다.
알 수 없는 코드도 원문과 부가 정보를 보존해 표시한다.
