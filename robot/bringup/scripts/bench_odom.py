#!/usr/bin/env python3
"""로봇 없이 스택을 세우기 위한 정지 오도메트리.

실기가 없는 자리에서도 센서·브릿지·관제 연동을 시험해야 한다. 그런데 로봇이
없으면 `odom -> base_link` 를 아무도 내지 않고, 그 한 변이 비면 TF 트리가
끊겨 `map` 도 `base_link` 도 조회되지 않는다. 그러면 브릿지가 자세를 못 읽어
`state/pose` 와 `state/safety` 가 멎고, 촬영은 "이동 중" 으로 거절된다 —
오도메트리가 끊기면 움직이는 것으로 보는 안전장치 때문이다.

그래서 "가만히 서 있는 로봇" 을 낸다. 항등 변환을 현재 시각으로 계속 발행할
뿐이며, 값을 지어내지 않는다. 위치가 늘 원점이라는 사실은 화면에 그대로
드러나므로 이것을 실주행으로 오해할 여지가 없다.

KISS-ICP 와 함께 쓰지 않는다. 그쪽이 같은 변을 소유하므로 둘이 동시에 내면
트리가 망가진다 — bringup 이 `robot:=none` 일 때 KISS-ICP 를 빼는 이유다.
"""

import rclpy
from geometry_msgs.msg import TransformStamped
from nav_msgs.msg import Odometry
from rclpy.node import Node
from tf2_ros import TransformBroadcaster


class BenchOdom(Node):
    def __init__(self):
        super().__init__("bench_odom")
        self._odom_frame = self.declare_parameter("odom_frame", "odom").value
        self._base_frame = self.declare_parameter("base_frame", "base_link").value
        rate = self.declare_parameter("rate_hz", 20.0).value

        self._tf = TransformBroadcaster(self)
        self._odom = self.create_publisher(Odometry, "odom", 10)
        self.create_timer(1.0 / max(1.0, rate), self._tick)

        self.get_logger().warn(
            "정지 오도메트리로 돕니다. 로봇이 없는 시험 구성이며, "
            "%s -> %s 는 항상 원점입니다." % (self._odom_frame, self._base_frame))

    def _tick(self):
        stamp = self.get_clock().now().to_msg()

        t = TransformStamped()
        t.header.stamp = stamp
        t.header.frame_id = self._odom_frame
        t.child_frame_id = self._base_frame
        t.transform.rotation.w = 1.0
        self._tf.sendTransform(t)

        # Nav2 와 브릿지는 TF 로 자세를 읽지만, /odom 을 보는 도구도 있어
        # 같은 내용을 토픽으로도 낸다.
        msg = Odometry()
        msg.header.stamp = stamp
        msg.header.frame_id = self._odom_frame
        msg.child_frame_id = self._base_frame
        msg.pose.pose.orientation.w = 1.0
        self._odom.publish(msg)


def main():
    rclpy.init()
    try:
        rclpy.spin(BenchOdom())
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
