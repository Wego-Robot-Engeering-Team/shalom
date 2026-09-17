# Shalom Python SDK

Python 3.9 이상과 표준 라이브러리만 사용한다. ROS, Qt, 별도 TCP·JSON package는
필요 없다.

```bash
cd <SDK_ROOT>/Linux/python
python3 -m pip install .
python3 examples/monitor.py 192.168.210.88
```

`Client`는 연결·heartbeat·수신을 맡고, `RobotApi(client)`가 명령을 맡는다.
`Client.poll()` 또는 `Client.run()`을 계속 호출해야 5 Hz heartbeat가 유지된다.
상세 API와 권한 경계는 [`../../docs/API.md`](../../docs/API.md)를 따른다.
