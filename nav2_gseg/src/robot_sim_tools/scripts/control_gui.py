#!/usr/bin/env python3
"""A small Tkinter GUI that publishes geometry_msgs/Twist commands.

The controls are deliberately generic: any simulated robot that accepts a
Twist command can use this GUI by selecting the appropriate topic.
"""

import argparse
import sys
import tkinter as tk
from tkinter import ttk

import rclpy
from geometry_msgs.msg import Twist
from rclpy.node import Node


PUBLISH_PERIOD_MS = 50


class CmdVelGui(Node):
    """Tkinter front-end for a continuously-published velocity command."""

    def __init__(self, topic: str) -> None:
        super().__init__("robot_control_gui")
        self.publisher = self.create_publisher(Twist, topic, 10)
        self.topic = topic
        self.pressed_keys: set[str] = set()
        self.button_command: tuple[float, float] | None = None

        self.root = tk.Tk()
        self.root.title("Robot Simulation Tools")
        self.root.resizable(False, False)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

        self.linear_speed = tk.DoubleVar(value=0.40)
        self.angular_speed = tk.DoubleVar(value=0.70)
        self.status = tk.StringVar(value="Stopped")
        self._build_ui()
        self.root.bind("<KeyPress>", self.key_press)
        self.root.bind("<KeyRelease>", self.key_release)
        self.root.after(PUBLISH_PERIOD_MS, self.publish_loop)

    def _build_ui(self) -> None:
        frame = ttk.Frame(self.root, padding=18)
        frame.grid()

        ttk.Label(frame, text="Robot Control", font=("Helvetica", 17, "bold")).grid(
            row=0, column=0, columnspan=3, pady=(0, 5)
        )
        ttk.Label(frame, text=f"Topic: {self.topic}").grid(row=1, column=0, columnspan=3, pady=(0, 12))

        controls = ttk.Frame(frame)
        controls.grid(row=2, column=0, columnspan=3)
        self._motion_button(controls, "▲  Forward\n(W)", 0.0, 1.0, row=0, column=1)
        self._motion_button(controls, "◀  Left\n(A)", -1.0, 0.0, row=1, column=0)
        ttk.Button(controls, text="■  STOP\n(Space)", command=self.stop, width=14).grid(
            row=1, column=1, padx=4, pady=4, ipady=8
        )
        self._motion_button(controls, "Right  ▶\n(D)", 1.0, 0.0, row=1, column=2)
        self._motion_button(controls, "▼  Reverse\n(S)", 0.0, -1.0, row=2, column=1)

        ttk.Separator(frame).grid(row=3, column=0, columnspan=3, sticky="ew", pady=14)
        ttk.Label(frame, text="Linear speed (m/s)").grid(row=4, column=0, sticky="w")
        ttk.Scale(frame, from_=0.05, to=1.0, variable=self.linear_speed, orient="horizontal", length=215).grid(
            row=4, column=1, columnspan=2, sticky="ew"
        )
        ttk.Label(frame, text="Angular speed (rad/s)").grid(row=5, column=0, sticky="w", pady=(8, 0))
        ttk.Scale(frame, from_=0.1, to=2.0, variable=self.angular_speed, orient="horizontal", length=215).grid(
            row=5, column=1, columnspan=2, sticky="ew", pady=(8, 0)
        )
        ttk.Label(frame, textvariable=self.status, foreground="#b00020").grid(
            row=6, column=0, columnspan=3, pady=(14, 0)
        )

    def _motion_button(
        self, parent: ttk.Frame, text: str, angular_sign: float, linear_sign: float, row: int, column: int
    ) -> None:
        button = ttk.Button(parent, text=text, width=14)
        button.grid(row=row, column=column, padx=4, pady=4, ipady=8)
        button.bind("<ButtonPress-1>", lambda _event: self.set_button_command(linear_sign, angular_sign))
        button.bind("<ButtonRelease-1>", lambda _event: self.clear_button_command())

    def set_button_command(self, linear_sign: float, angular_sign: float) -> None:
        self.button_command = (linear_sign, angular_sign)

    def clear_button_command(self) -> None:
        self.button_command = None

    def key_press(self, event: tk.Event) -> None:
        key = event.keysym.lower()
        if key == "space":
            self.stop()
        elif key in {"w", "a", "s", "d"}:
            self.pressed_keys.add(key)

    def key_release(self, event: tk.Event) -> None:
        self.pressed_keys.discard(event.keysym.lower())

    def current_command(self) -> tuple[float, float]:
        linear = 0.0
        angular = 0.0
        if self.button_command is not None:
            linear_sign, angular_sign = self.button_command
            linear = linear_sign * self.linear_speed.get()
            angular = angular_sign * self.angular_speed.get()
        else:
            linear = (float("w" in self.pressed_keys) - float("s" in self.pressed_keys)) * self.linear_speed.get()
            angular = (float("a" in self.pressed_keys) - float("d" in self.pressed_keys)) * self.angular_speed.get()
        return linear, angular

    def publish_loop(self) -> None:
        linear, angular = self.current_command()
        message = Twist()
        message.linear.x = linear
        message.angular.z = angular
        self.publisher.publish(message)

        if linear == 0.0 and angular == 0.0:
            self.status.set("Stopped")
        else:
            self.status.set(f"linear: {linear:.2f} m/s   angular: {angular:.2f} rad/s")

        rclpy.spin_once(self, timeout_sec=0.0)
        self.root.after(PUBLISH_PERIOD_MS, self.publish_loop)

    def stop(self) -> None:
        self.pressed_keys.clear()
        self.button_command = None
        self.publish_zero()

    def publish_zero(self) -> None:
        self.publisher.publish(Twist())

    def close(self) -> None:
        self.stop()
        self.root.destroy()

    def run(self) -> None:
        self.root.mainloop()


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Open a GUI that publishes Twist commands.")
    parser.add_argument("--topic", default="/cmd_vel", help="Velocity topic (default: /cmd_vel).")
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    rclpy.init(args=sys.argv)
    gui = CmdVelGui(arguments.topic)
    try:
        gui.run()
    except KeyboardInterrupt:
        gui.stop()
    finally:
        gui.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == "__main__":
    main()
