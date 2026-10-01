# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Test the real safety gate without publishing on production topic names."""

import os
import subprocess
import time
from pathlib import Path

from ament_index_python.packages import get_package_prefix

from geometry_msgs.msg import Twist

import rclpy
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy

from shalom_interfaces.msg import MotionAuthority, SafetyState

from std_msgs.msg import Bool


def test_base_inhibit_requires_fresh_command_after_release(monkeypatch):
    """Both independent inhibit heartbeats must permit each base command."""
    # The unique domain and remapped topics avoid any production command path.
    prefix = f'/test_safety_gate_{os.getpid()}'
    monkeypatch.setenv('ROS_DOMAIN_ID', str(160 + os.getpid() % 60))
    monkeypatch.setenv('ROS_LOCALHOST_ONLY', '1')
    monkeypatch.delenv('CYCLONEDDS_URI', raising=False)
    gate_path = (
        Path(get_package_prefix('safety_gate')) /
        'lib/safety_gate/safety_gate_node'
    )
    gate = subprocess.Popen(
        [
            str(gate_path), '--ros-args',
            '-p', f'input_base_topic:={prefix}/input',
            '-p', f'output_base_topic:={prefix}/output',
            '-p', 'output_hz:=40.0',
            '-p', 'base_inhibit_timeout_ms:=250',
            '-r', f'/safety/state:={prefix}/safety',
            '-r', f'/motion/authority:={prefix}/authority',
            '-r', f'/motion/base/inhibit:={prefix}/inhibit',
            '-r', f'/motion/base_source/inhibit:={prefix}/source_inhibit',
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        env=os.environ.copy(),
    )
    rclpy.init()
    node = rclpy.create_node('test_base_inhibit')
    state_qos = QoSProfile(
        depth=1,
        reliability=ReliabilityPolicy.RELIABLE,
        durability=DurabilityPolicy.TRANSIENT_LOCAL,
    )
    safety_pub = node.create_publisher(
        SafetyState, f'{prefix}/safety', state_qos)
    authority_pub = node.create_publisher(
        MotionAuthority, f'{prefix}/authority', state_qos)
    command_pub = node.create_publisher(Twist, f'{prefix}/input', 10)
    inhibit_pub = node.create_publisher(Bool, f'{prefix}/inhibit', 10)
    source_inhibit_pub = node.create_publisher(
        Bool, f'{prefix}/source_inhibit', 10)
    outputs = []
    node.create_subscription(
        Twist, f'{prefix}/output',
        lambda msg: outputs.append(msg.linear.x), 10)

    def drive(seconds, *, inhibit=None, source_inhibit=None, command=None,
              safety=SafetyState.NORMAL):
        outputs.clear()
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            state = SafetyState()
            state.state = safety
            state.motion_permitted = safety == SafetyState.NORMAL
            safety_pub.publish(state)
            authority = MotionAuthority()
            authority.state = MotionAuthority.BASE_ACTIVE
            authority_pub.publish(authority)
            if inhibit is not None:
                inhibit_pub.publish(Bool(data=inhibit))
            if source_inhibit is not None:
                source_inhibit_pub.publish(Bool(data=source_inhibit))
            if command is not None:
                twist = Twist()
                twist.linear.x = command
                command_pub.publish(twist)
            rclpy.spin_once(node, timeout_sec=0.01)
        return list(outputs)

    try:
        assert drive(0.8, command=0.4), 'safety gate did not publish'
        assert all(value == 0.0 for value in outputs), (
            'missing inhibit heartbeats must fail closed')

        # HMI permission alone cannot bypass a missing source heartbeat.
        assert drive(0.12, inhibit=False, command=0.4)
        assert all(value == 0.0 for value in outputs)

        # The old Twist is within its 300 ms lease, but must not be replayed
        # when the source manager first releases its inhibit.
        assert drive(0.12, inhibit=False, source_inhibit=False)
        assert all(value == 0.0 for value in outputs)
        assert any(
            value > 0.3 for value in drive(
                0.2, inhibit=False, source_inhibit=False, command=0.4))

        # Source takeover blocks motion while HMI remains permissive.
        assert drive(0.25, inhibit=False, source_inhibit=True, command=0.4)
        assert all(value == 0.0 for value in outputs[-4:]), (
            'source transition must keep output zero')
        assert drive(0.12, inhibit=False, source_inhibit=False)
        assert all(value == 0.0 for value in outputs), (
            'pre-source-transition command was replayed')
        assert any(
            value > 0.3 for value in drive(
                0.2, inhibit=False, source_inhibit=False, command=0.4))

        assert drive(0.25, inhibit=True, source_inhibit=False, command=0.4)
        assert all(value == 0.0 for value in outputs[-4:]), (
            'HMI transition must keep output zero')
        assert drive(0.12, inhibit=False, source_inhibit=False)
        assert all(value == 0.0 for value in outputs), (
            'pre-HMI-transition command was replayed')
        assert any(
            value > 0.3 for value in drive(
                0.2, inhibit=False, source_inhibit=False, command=0.4))

        # A lost source heartbeat inhibits even fresh velocity commands.
        assert drive(0.4, inhibit=False, command=0.4)
        assert all(value == 0.0 for value in outputs[-4:])
        assert drive(0.12, inhibit=False, source_inhibit=False)
        assert all(value == 0.0 for value in outputs), (
            'source heartbeat recovery replayed old command')
        assert any(
            value > 0.3 for value in drive(
                0.2, inhibit=False, source_inhibit=False, command=0.4))

        # A lost HMI heartbeat independently inhibits fresh commands.
        assert drive(0.4, source_inhibit=False, command=0.4)
        assert all(value == 0.0 for value in outputs[-4:])
        assert drive(0.12, inhibit=False, source_inhibit=False)
        assert all(value == 0.0 for value in outputs), (
            'HMI heartbeat recovery replayed old command')
        assert any(
            value > 0.3 for value in drive(
                0.2, inhibit=False, source_inhibit=False, command=0.4))

        # E-Stop remains a BLOCK, not a stream of zero commands.
        drive(0.15, inhibit=False, source_inhibit=False,
              safety=SafetyState.E_STOP_LATCHED)
        assert not drive(
            0.12, inhibit=False, source_inhibit=False,
            safety=SafetyState.E_STOP_LATCHED)
    finally:
        node.destroy_node()
        rclpy.shutdown()
        gate.terminate()
        try:
            gate.wait(timeout=3)
        except subprocess.TimeoutExpired:
            gate.kill()
            gate.wait(timeout=3)
