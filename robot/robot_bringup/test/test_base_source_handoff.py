# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Exercise the real autonomous source path without a robot driver."""

import os
import subprocess
import threading
import time
from pathlib import Path

from action_msgs.msg import GoalStatus
from ament_index_python.packages import get_package_prefix, get_package_share_directory
from geometry_msgs.msg import Twist
from nav2_msgs.action import NavigateToPose
import rclpy
from rclpy.action import ActionClient, ActionServer, CancelResponse
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import MultiThreadedExecutor
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy
from shalom_interfaces.msg import BaseSourceState, MotionAuthority, MotionStopped, SafetyState
from shalom_interfaces.srv import SelectBaseSource
from std_msgs.msg import Bool


def _node_binary(package, executable):
    return str(Path(get_package_prefix(package)) / 'lib' / package / executable)


class _Rig:
    """Publish upstream heartbeats and observe the driver's would-be input."""

    def __init__(self, output_topic):
        self.node = rclpy.create_node('test_base_source_handoff')
        self.executor = MultiThreadedExecutor(num_threads=6)
        self.executor.add_node(self.node)
        self.thread = threading.Thread(target=self.executor.spin, daemon=True)
        self.lock = threading.Lock()
        self.commands = {
            BaseSourceState.NAV: False,
            BaseSourceState.DOCK: False,
            BaseSourceState.STAIR: False,
            BaseSourceState.MISSION: False,
        }
        self.velocities = {
            BaseSourceState.NAV: 0.4,
            BaseSourceState.DOCK: 0.6,
            BaseSourceState.STAIR: 0.8,
            BaseSourceState.MISSION: 0.9,
        }
        self.manual_locked = False
        self.manual_ready = False
        self.safety_ok = True
        self.authority_ok = True
        self.teleop_enabled = False
        self.send_stopped = False
        self.stopped_sequence = 0
        self.heartbeat_sequence = 0
        self.outputs = []
        self.inhibits = []
        self.states = []
        self.request_number = 0

        state_qos = QoSProfile(
            depth=1,
            reliability=ReliabilityPolicy.RELIABLE,
            durability=DurabilityPolicy.TRANSIENT_LOCAL,
        )
        self.safety_pub = self.node.create_publisher(SafetyState, '/safety/state', state_qos)
        self.authority_pub = self.node.create_publisher(
            MotionAuthority, '/motion/authority', state_qos)
        self.base_inhibit_pub = self.node.create_publisher(
            Bool, '/motion/base/inhibit', 10)
        self.manual_lock_pub = self.node.create_publisher(
            Bool, '/motion/manual_autonomy_lock', 10)
        self.manual_ready_pub = self.node.create_publisher(
            Bool, '/motion/manual_ready', 10)
        self.teleop_pub = self.node.create_publisher(
            Twist, '/motion/teleop/cmd_vel', 10)
        self.stopped_pub = self.node.create_publisher(
            MotionStopped, '/motion/stopped', 10)
        topics = {
            BaseSourceState.NAV: '/motion/nav/cmd_vel',
            BaseSourceState.DOCK: '/motion/dock/cmd_vel',
            BaseSourceState.STAIR: '/motion/stair/cmd_vel',
            BaseSourceState.MISSION: '/motion/mission/cmd_vel',
        }
        self.command_pubs = {
            source: self.node.create_publisher(Twist, topic, 10)
            for source, topic in topics.items()
        }
        self.output_sub = self.node.create_subscription(
            Twist, output_topic, self._on_output, 20)
        self.inhibit_sub = self.node.create_subscription(
            Bool, '/motion/base_source/inhibit', self._on_inhibit, 20)
        self.state_sub = self.node.create_subscription(
            BaseSourceState, '/motion/base_source/state', self._on_state, state_qos)
        self.select_client = self.node.create_client(
            SelectBaseSource, '/motion/base_source/select')
        self.timer = self.node.create_timer(0.05, self._publish)
        self.thread.start()

    def _on_output(self, msg):
        with self.lock:
            self.outputs.append((time.monotonic(), msg.linear.x))

    def _on_inhibit(self, msg):
        with self.lock:
            self.inhibits.append((time.monotonic(), msg.data))

    def _on_state(self, msg):
        with self.lock:
            self.states.append(msg)

    def _publish(self):
        with self.lock:
            locked = self.manual_locked
            manual_ready = self.manual_ready
            safety_ok = self.safety_ok
            authority_ok = self.authority_ok
            teleop_enabled = self.teleop_enabled
            commands = dict(self.commands)
            stopped = self.send_stopped
            self.heartbeat_sequence += 1
            heartbeat_sequence = self.heartbeat_sequence
            if stopped:
                self.stopped_sequence += 1
                stopped_sequence = self.stopped_sequence

        safety = SafetyState()
        safety.stamp = self.node.get_clock().now().to_msg()
        safety.sequence = heartbeat_sequence
        safety.state = SafetyState.NORMAL if safety_ok else SafetyState.CONTROLLED_STOP
        safety.motion_permitted = safety_ok
        self.safety_pub.publish(safety)
        authority = MotionAuthority()
        authority.stamp = self.node.get_clock().now().to_msg()
        authority.sequence = heartbeat_sequence
        authority.state = MotionAuthority.BASE_ACTIVE if authority_ok else MotionAuthority.NONE
        self.authority_pub.publish(authority)
        self.base_inhibit_pub.publish(Bool(data=False))
        self.manual_lock_pub.publish(Bool(data=locked))
        self.manual_ready_pub.publish(Bool(data=manual_ready))
        if teleop_enabled:
            teleop = Twist()
            teleop.linear.x = 1.1
            self.teleop_pub.publish(teleop)
        for source, enabled in commands.items():
            if enabled:
                command = Twist()
                command.linear.x = self.velocities[source]
                self.command_pubs[source].publish(command)
        if stopped:
            feedback = MotionStopped()
            feedback.stamp = self.node.get_clock().now().to_msg()
            feedback.sequence = stopped_sequence
            feedback.resource = MotionStopped.BASE
            feedback.stopped = True
            feedback.source = 'test_base_source_handoff'
            self.stopped_pub.publish(feedback)

    def configure(self, *, commands=None, manual_locked=None, manual_ready=None,
                  teleop_enabled=None, send_stopped=None, safety_ok=None,
                  authority_ok=None):
        with self.lock:
            if commands is not None:
                self.commands.update(commands)
            if manual_locked is not None:
                self.manual_locked = manual_locked
            if manual_ready is not None:
                self.manual_ready = manual_ready
            if safety_ok is not None:
                self.safety_ok = safety_ok
            if authority_ok is not None:
                self.authority_ok = authority_ok
            if teleop_enabled is not None:
                self.teleop_enabled = teleop_enabled
            if send_stopped is not None:
                self.send_stopped = send_stopped

    def state(self):
        with self.lock:
            return self.states[-1] if self.states else None

    def output_values_since(self, since):
        with self.lock:
            return [value for stamp, value in self.outputs if stamp >= since]

    def inhibit_seen_since(self, since, value):
        with self.lock:
            return any(stamp >= since and inhibited == value
                       for stamp, inhibited in self.inhibits)

    def wait(self, condition, label, timeout=5.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if condition():
                return
            time.sleep(0.02)
        state = self.state()
        detail = (None if state is None else
                  (state.phase, state.active_source, state.requested_source,
                   state.reason_code))
        raise AssertionError(f'timed out waiting for {label}; last state={detail}')

    def wait_state(self, phase, source=None, reason=None, timeout=5.0):
        def matches():
            state = self.state()
            return (state is not None and state.phase == phase and
                    (source is None or state.active_source == source) and
                    (reason is None or state.reason_code == reason))
        self.wait(matches, f'phase={phase}, source={source}, reason={reason}', timeout)

    def select(self, source, requester='test_base_source_handoff'):
        self.request_number += 1
        request = SelectBaseSource.Request()
        request.requester = requester
        request.request_id = f'test-{os.getpid()}-{self.request_number}'
        request.source = source
        future = self.select_client.call_async(request)
        self.wait(future.done, f'select source {source} response')
        return future.result()

    def publish_stopped_at(self, stamp):
        with self.lock:
            self.stopped_sequence += 1
            sequence = self.stopped_sequence
        feedback = MotionStopped()
        feedback.stamp = stamp
        feedback.sequence = sequence
        feedback.resource = MotionStopped.BASE
        feedback.stopped = True
        feedback.source = 'test_pre_cancel_feedback'
        self.stopped_pub.publish(feedback)

    def close(self):
        self.executor.shutdown(timeout_sec=3.0)
        self.thread.join(timeout=3.0)
        self.node.destroy_node()


class _NavAction:
    """An active Nav2 goal whose cancellation is observable by the test."""

    def __init__(self, rig):
        self.cancel_seen = threading.Event()
        self.server = ActionServer(
            rig.node, NavigateToPose, 'navigate_to_pose', self._execute,
            cancel_callback=self._cancel,
            callback_group=ReentrantCallbackGroup(),
        )
        self.client = ActionClient(rig.node, NavigateToPose, 'navigate_to_pose')

    def _cancel(self, _goal_handle):
        self.cancel_seen.set()
        return CancelResponse.ACCEPT

    def _execute(self, goal_handle):
        while not goal_handle.is_cancel_requested:
            time.sleep(0.01)
        goal_handle.canceled()
        return NavigateToPose.Result()

    def start_goal(self, rig):
        assert self.client.wait_for_server(timeout_sec=3.0)
        future = self.client.send_goal_async(NavigateToPose.Goal())
        rig.wait(future.done, 'Nav2 goal acceptance')
        goal = future.result()
        assert goal.accepted
        return goal.get_result_async()

    def close(self):
        self.client.destroy()
        self.server.destroy()


def _assert_only(values, expected):
    assert values, 'safety gate did not publish output'
    assert all(abs(value - expected) < 0.01 for value in values), values


def _stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3.0)


def test_selected_source_handoff_is_fail_closed(monkeypatch):
    # The driver topic is also redirected: this test cannot command a robot.
    domain = str(150 + os.getpid() % 70)
    monkeypatch.setenv('ROS_DOMAIN_ID', domain)
    monkeypatch.setenv('ROS_LOCALHOST_ONLY', '1')
    monkeypatch.delenv('CYCLONEDDS_URI', raising=False)
    output_topic = f'/test_base_source_handoff_{os.getpid()}/driver_input'
    env = os.environ.copy()
    config = Path(get_package_share_directory('robot_bringup')) / 'config/twist_mux.yaml'
    manager_cmd = [_node_binary('base_source_manager', 'base_source_manager_node')]
    mux_cmd = [
        _node_binary('twist_mux', 'twist_mux'), '--ros-args',
        '--params-file', str(config), '-r', 'cmd_vel_out:=/motion/base/cmd_vel',
    ]
    gate_cmd = [
        _node_binary('safety_gate', 'safety_gate_node'), '--ros-args',
        '-p', f'output_base_topic:={output_topic}', '-p', 'output_hz:=40.0',
    ]
    processes = []
    rig = None
    nav_action = None

    def launch(command):
        process = subprocess.Popen(
            command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        processes.append(process)
        return process

    try:
        rclpy.init()
        rig = _Rig(output_topic)
        manager = launch(manager_cmd)
        launch(mux_cmd)
        launch(gate_cmd)
        assert rig.select_client.wait_for_service(timeout_sec=5.0)
        rig.wait(lambda: rig.manual_lock_pub.get_subscription_count() > 0,
                 'manager lock subscription')
        rig.wait(lambda: rig.output_sub.get_publisher_count() > 0,
                 'safety gate output publisher')
        rig.wait(lambda: rig.stopped_pub.get_subscription_count() > 0,
                 'manager stopped subscription')
        rig.wait(lambda: rig.safety_pub.get_subscription_count() >= 2,
                 'manager and gate safety subscriptions')
        rig.wait(lambda: rig.authority_pub.get_subscription_count() >= 2,
                 'manager and gate authority subscriptions')
        rig.wait_state(BaseSourceState.INACTIVE)
        time.sleep(0.2)  # Allow a complete set of fresh upstream heartbeats.

        # Initial ownership requires a fresh post-selection stop report.
        rig.configure(commands={BaseSourceState.NAV: True})
        response = rig.select(BaseSourceState.NAV)
        assert response.accepted and response.state.phase == BaseSourceState.TRANSITIONING
        assert response.state.owner_requester == 'test_base_source_handoff'
        rig.configure(send_stopped=True)
        rig.wait_state(BaseSourceState.ACTIVE, BaseSourceState.NAV)
        rig.wait(lambda: any(abs(v - 0.4) < 0.01
                             for v in rig.output_values_since(0)), 'NAV output')

        nav_action = _NavAction(rig)
        nav_result = nav_action.start_goal(rig)

        # Keep every other producer busy while NAV relinquishes control.
        rig.configure(
            commands={BaseSourceState.STAIR: True, BaseSourceState.MISSION: True},
            send_stopped=False,
        )
        transition_at = time.monotonic()
        pre_cancel_stamp = rig.node.get_clock().now().to_msg()
        response = rig.select(BaseSourceState.DOCK)
        assert response.accepted and response.state.phase == BaseSourceState.TRANSITIONING
        assert response.result_code == SelectBaseSource.Response.ACCEPTED
        assert response.state.owner_requester == 'test_base_source_handoff'
        rig.wait(lambda: rig.inhibit_seen_since(transition_at, True),
                 'source inhibit during handoff')
        rig.wait(lambda: nav_action.cancel_seen.is_set(), 'Nav2 cancel request')
        rig.wait(nav_result.done, 'Nav2 goal cancelled')
        assert nav_result.result().status == GoalStatus.STATUS_CANCELED
        zero_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= zero_at, 'settled handoff output')
        sample_at = time.monotonic()
        rig.wait(lambda: len(rig.output_values_since(sample_at)) >= 4,
                 'handoff zero outputs')
        _assert_only(rig.output_values_since(sample_at), 0.0)
        assert rig.state().phase == BaseSourceState.TRANSITIONING

        # A delayed sample produced before cancellation must not count as the
        # post-cancel physical stop, even if its sequence was not seen before.
        rig.publish_stopped_at(pre_cancel_stamp)
        time.sleep(0.15)
        assert rig.state().phase == BaseSourceState.TRANSITIONING

        # Only feedback produced after the cancel response may grant DOCK.
        rig.configure(send_stopped=True, commands={BaseSourceState.DOCK: True})
        rig.wait_state(BaseSourceState.ACTIVE, BaseSourceState.DOCK)
        busy = rig.select(BaseSourceState.NAV, requester='another_controller')
        assert not busy.accepted
        assert busy.result_code == SelectBaseSource.Response.BUSY
        rig.wait(lambda: any(abs(v - 0.6) < 0.01
                             for v in rig.output_values_since(sample_at)),
                 'DOCK output')
        sample_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= sample_at + 0.35,
                 'selected DOCK while other sources publish')
        _assert_only(rig.output_values_since(sample_at), 0.6)

        # A DOCK input gap must fault and keep NAV/STAIR/MISSION blocked.
        rig.configure(commands={BaseSourceState.DOCK: False})
        rig.wait_state(BaseSourceState.FAULT, timeout=2.0)
        assert rig.state().reason_code == 'BASE_SOURCE_COMMAND_TIMEOUT'
        sample_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= sample_at + 0.4,
                 'fault output with live alternate sources')
        _assert_only(rig.output_values_since(sample_at), 0.0)
        assert rig.state().phase == BaseSourceState.FAULT

        # Manual mode revokes autonomy and admits teleop only after a fresh
        # manual-ready heartbeat from the HMI side.
        rig.configure(send_stopped=False)
        response = rig.select(BaseSourceState.DOCK)
        assert response.accepted
        rig.configure(send_stopped=True, commands={BaseSourceState.DOCK: True})
        rig.wait_state(BaseSourceState.ACTIVE, BaseSourceState.DOCK)
        rig.configure(manual_locked=True)
        rig.wait_state(BaseSourceState.INACTIVE)
        blocked = rig.select(BaseSourceState.NAV)
        assert not blocked.accepted
        assert blocked.result_code == SelectBaseSource.Response.NOT_READY
        sample_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= sample_at + 0.2,
                 'manual lock zero output')
        _assert_only(rig.output_values_since(sample_at), 0.0)

        rig.configure(manual_ready=True, teleop_enabled=True)
        rig.wait(lambda: any(abs(v - 1.1) < 0.01
                             for v in rig.output_values_since(sample_at)),
                 'manual teleop output')
        sample_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= sample_at + 0.25,
                 'manual teleop with competing autonomy')
        _assert_only(rig.output_values_since(sample_at), 1.1)

        rig.configure(manual_locked=False, manual_ready=False,
                      teleop_enabled=False, send_stopped=False)
        time.sleep(0.4)  # Let the finite teleop mux lease expire.
        response = rig.select(BaseSourceState.DOCK)
        assert response.accepted
        rig.configure(send_stopped=True)
        rig.wait_state(BaseSourceState.ACTIVE, BaseSourceState.DOCK)
        rig.wait(lambda: any(abs(v - 0.6) < 0.01
                             for v in rig.output_values_since(sample_at)),
                 'DOCK before manager loss')

        # Losing the owner closes the gate, and restart does not adopt input.
        _stop(manager)
        processes.remove(manager)
        sample_at = time.monotonic() + 0.45
        rig.wait(lambda: time.monotonic() >= sample_at + 0.3,
                 'manager loss output lease expiration')
        _assert_only(rig.output_values_since(sample_at), 0.0)
        launch(manager_cmd)
        rig.wait_state(BaseSourceState.INACTIVE, reason='BASE_SOURCE_STARTUP')
        sample_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= sample_at + 0.35,
                 'manager restart output')
        _assert_only(rig.output_values_since(sample_at), 0.0)

        # A temporary Safety loss revokes ownership. Restoring Safety must
        # not replay an old DOCK command or implicitly reselect the source.
        response = rig.select(BaseSourceState.DOCK)
        assert response.accepted
        rig.wait_state(BaseSourceState.ACTIVE, BaseSourceState.DOCK)
        rig.configure(safety_ok=False)
        rig.wait_state(BaseSourceState.INACTIVE,
                       reason='BASE_SOURCE_SAFETY_REVOKED')
        rig.configure(safety_ok=True)
        sample_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= sample_at + 0.35,
                 'safety recovery without a new source claim')
        _assert_only(rig.output_values_since(sample_at), 0.0)

        response = rig.select(BaseSourceState.DOCK)
        assert response.accepted
        rig.wait_state(BaseSourceState.ACTIVE, BaseSourceState.DOCK)
        rig.configure(authority_ok=False)
        rig.wait_state(BaseSourceState.INACTIVE,
                       reason='BASE_SOURCE_AUTHORITY_REVOKED')
        rig.configure(authority_ok=True)
        sample_at = time.monotonic() + 0.15
        rig.wait(lambda: time.monotonic() >= sample_at + 0.35,
                 'authority recovery without a new source claim')
        _assert_only(rig.output_values_since(sample_at), 0.0)
    finally:
        if nav_action is not None:
            nav_action.close()
        if rig is not None:
            rig.close()
        if rclpy.ok():
            rclpy.shutdown()
        for process in processes:
            _stop(process)
