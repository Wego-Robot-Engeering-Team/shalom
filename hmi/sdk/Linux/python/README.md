# Python SDK

Python 3.9+, 표준 라이브러리 기반. Linux·macOS·Windows에서 같은 패키지를 사용한다.

```bash
# 해당 OS 디렉터리: Linux/, MacOS/, Windows/
python -m pip install ./python
python python/examples/monitor.py <robot-ip>
python python/examples/monitor.py <robot-ip> --channel state/nav --duration 30
python python/examples/catalogs.py <robot-ip>
```

```python
from robot_sdk import Client, RobotApi

with Client() as client:
    client.connect("192.168.210.88")
    robot = RobotApi(client)
    reply = robot.list_missions()
    if reply.ok:
        catalog = client.latest("state/missions")
        print(catalog.envelope["p"] if catalog else "수신 대기")
    else:
        print(reply.error_code, reply.error_message)
```

`Client(on_message=handler)`를 사용하면 동기 API 응답 대기 중에도 상태·이벤트를
처리할 수 있다. 계속 연결할 때는 `poll()` 또는 `run()`을 호출한다.

[API](../../docs/API.md) · [명령](../../docs/command.md) · [통신](../../docs/transport.md)
