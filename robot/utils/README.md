# Utils

로봇 운용 보조 기능과 개발·점검·유지보수 도구를 관리한다.

- 독립 실행 스크립트는 이 폴더에 둔다.
- 알림음 같은 운용 보조 기능은 기능별 ROS 패키지로 두고 `bringup`에서 실행한다.
  노드·Python 코드·음원 등 관련 파일은 해당 패키지 안에서 관리한다.

| 파일 | 용도 |
| --- | --- |
| `stop_stack.sh` | 등록된 스택 종료 또는 `--all`로 DDS 프로세스 정리 |

```bash
ros2 run robot_bringup stop_stack.sh
```

`stop_stack.sh`는 `robot_bringup`이 설치한다.
