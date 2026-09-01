#!/usr/bin/env python3
"""Send a timed velocity command to a simulated robot.

Examples:
  ros2 run robot_sim_tools drive forward --duration 2
  ros2 run robot_sim_tools drive turn-left --duration 1
  ros2 run robot_sim_tools drive custom --linear 0.3 --angular -0.4
"""

import argparse
import sys
import time

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node


COMMANDS = {
    "forward": (0.4, 0.0),
    "backward": (-0.25, 0.0),
    "turn-left": (0.0, 0.7),
    "turn-right": (0.0, -0.7),
    "stop": (0.0, 0.0),
}


class DriveController(Node):
    """Small `/cmd_vel` publisher for manual scripted control."""

    def __init__(self, topic: str) -> None:
        super().__init__("robot_drive")
        self.publisher = self.create_publisher(Twist, topic, 10)

    def publish(self, linear: float, angular: float) -> None:
        command = Twist()
        command.linear.x = linear
        command.angular.z = angular
        self.publisher.publish(command)

    def stop(self) -> None:
        # Publish a few times so the bridge receives the stop even if discovery
        # is still settling when this short-lived command starts.
        for _ in range(3):
            self.publish(0.0, 0.0)
            rclpy.spin_once(self, timeout_sec=0.05)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Control a simulated robot through a Twist topic.")
    parser.add_argument("command", choices=[*COMMANDS, "custom"])
    parser.add_argument("--linear", type=float, help="Forward speed in m/s (custom only).")
    parser.add_argument("--angular", type=float, help="Yaw rate in rad/s (custom only).")
    parser.add_argument("--duration", type=float, default=1.0, help="Command duration in seconds (default: 1).")
    parser.add_argument("--topic", default="/cmd_vel", help="Velocity topic (default: /cmd_vel).")
    arguments = parser.parse_args()

    if arguments.duration < 0:
        parser.error("--duration must be zero or greater")
    if arguments.command == "custom" and (arguments.linear is None or arguments.angular is None):
        parser.error("custom requires both --linear and --angular")
    if arguments.command != "custom" and (arguments.linear is not None or arguments.angular is not None):
        parser.error("--linear and --angular can only be used with custom")
    return arguments


def main() -> None:
    arguments = parse_arguments()
    linear, angular = (
        (arguments.linear, arguments.angular)
        if arguments.command == "custom"
        else COMMANDS[arguments.command]
    )

    rclpy.init(args=sys.argv)
    controller = DriveController(arguments.topic)
    try:
        end_time = time.monotonic() + arguments.duration
        while time.monotonic() < end_time:
            controller.publish(linear, angular)
            rclpy.spin_once(controller, timeout_sec=0.05)

        controller.stop()
        controller.get_logger().info(
            f"Sent {arguments.command}: linear={linear:.2f} m/s, angular={angular:.2f} rad/s"
        )
    except KeyboardInterrupt:
        controller.stop()
    finally:
        controller.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
