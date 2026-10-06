# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Individual Nav2 goal controls, with no connection to a robot's ROS domain."""

import threading
import time
import unittest

# Reuse the isolated TCP bridge and controller-parameter fixtures.
import test_navigation_speed as fixture

import rclpy
from geometry_msgs.msg import Twist
from nav2_msgs.action import NavigateToPose
from rclpy.action import ActionServer, CancelResponse, GoalResponse
from rclpy.callback_groups import ReentrantCallbackGroup
from rclpy.executors import MultiThreadedExecutor
from interfaces.msg import SafetyState, MotionAuthority, SafetyEvent
from interfaces.srv import SafetyCommand, AuthorityRequest
from rclpy.qos import QoSProfile, DurabilityPolicy


class NavigationControlTest(fixture.NavigationSpeedTest):
    def setUp(self):
        super().setUp()
        self.finish_cancel = threading.Event()
        self.finish_cancel.set()
        self.accept_goal = threading.Event()
        self.accept_goal.set()
        self.accept_entered = threading.Event()
        self.finish_goal = threading.Event()
        self.closing = threading.Event()
        self.reject_cancel = False
        self.reject_goal = False
        self.publish_feedback = True
        self.goals = []
        self.holds = []
        self.safety_accepts = self.authority_accepts = True
        self.safety_reported = SafetyState.NORMAL
        self.authority_reported = MotionAuthority.BASE_ACTIVE
        self.report_dependencies = True
        self.safety_requests, self.authority_requests, self.safety_events = [], [], []
        self.hold_sub = self.node.create_subscription(Twist, "/motion/manual_hold/cmd_vel",
                                                       self.holds.append, 10)
        self.nav_node = rclpy.create_node("navigation_control_test_server")
        group = ReentrantCallbackGroup()

        def goal(request):
            self.accept_entered.set()
            self.accept_goal.wait(timeout=6)
            return GoalResponse.REJECT if self.reject_goal else GoalResponse.ACCEPT

        def cancel(_):
            return CancelResponse.REJECT if self.reject_cancel else CancelResponse.ACCEPT

        def execute(handle):
            self.goals.append(handle.request.pose)
            while not self.closing.is_set():
                if self.publish_feedback:
                    feedback = NavigateToPose.Feedback()
                    feedback.distance_remaining = 4.2
                    feedback.estimated_time_remaining.sec = 18
                    feedback.navigation_time.sec = 3
                    feedback.number_of_recoveries = 2
                    handle.publish_feedback(feedback)
                if handle.is_cancel_requested and self.finish_cancel.is_set():
                    handle.canceled()
                    return NavigateToPose.Result()
                if self.finish_goal.is_set():
                    handle.succeed()
                    return NavigateToPose.Result()
                time.sleep(0.01)
            handle.abort()
            return NavigateToPose.Result()

        self.action = ActionServer(self.nav_node, NavigateToPose, "navigate_to_pose",
            execute_callback=execute, goal_callback=goal, cancel_callback=cancel,
            callback_group=group)
        self.executor = MultiThreadedExecutor(num_threads=3)
        self.executor.add_node(self.nav_node)
        self.worker = threading.Thread(target=self.executor.spin, daemon=True)
        self.worker.start()
        self.fake_controller()
        transient = QoSProfile(depth=1, durability=DurabilityPolicy.TRANSIENT_LOCAL)
        self.safety_pub = self.node.create_publisher(SafetyState, "/safety/state", transient)
        self.authority_pub = self.node.create_publisher(MotionAuthority, "/motion/authority", transient)
        self.node.create_subscription(SafetyEvent, "/safety/event", self.safety_events.append, 20)
        def safety(request, response):
            self.safety_requests.append(request)
            response.accepted = self.safety_accepts
            response.reason_code = "" if self.safety_accepts else "SAFETY_RESUME_GUARD_FAILED"
            response.detail = "" if self.safety_accepts else "Safety rejected resume"
            return response
        def authority(request, response):
            self.authority_requests.append(request)
            response.accepted = self.authority_accepts
            response.reason_code = "" if self.authority_accepts else "AUTHORITY_BUSY"
            response.detail = "" if self.authority_accepts else "Authority rejected base"
            return response
        self.node.create_service(SafetyCommand, "/safety/command", safety)
        self.node.create_service(AuthorityRequest, "/motion/authority/request", authority)
        def report():
            if self.report_dependencies:
                self.safety_pub.publish(SafetyState(state=self.safety_reported,
                    motion_permitted=self.safety_reported == SafetyState.NORMAL))
                self.authority_pub.publish(MotionAuthority(state=self.authority_reported, owner="hmi_bridge"))
        self.dependency_timer = self.node.create_timer(0.05, report)
        self.expect_applied(0.3, 0.5)
        self.spin_for(0.3)

    def tearDown(self):
        self.closing.set()
        self.accept_goal.set()
        self.finish_cancel.set()
        self.stop_bridge()
        self.executor.shutdown(timeout_sec=3)
        self.worker.join(timeout=3)
        self.action.destroy()
        self.nav_node.destroy_node()
        super().tearDown()

    def nav_state(self, status):
        return self.receive(lambda e: e.get("ch") == "state/nav" and
                            e["p"].get("status") == status)["p"]

    def send_without_waiting(self, channel, payload):
        self.sequence += 1
        request_id = f"navigation-{self.sequence}"
        header = fixture.json.dumps({"v": 1, "t": "req", "robot": "speed-test",
            "id": request_id, "ch": channel, "p": payload}).encode()
        self.peer.sendall(fixture.struct.pack("<III", 0x4D4C4853, len(header) + 4, len(header)) + header)
        return request_id

    def test_pause_resume_cancel_retains_exact_goal(self):
        target = {"x": 1.5, "y": -2.0, "theta": 1.2}
        self.assertTrue(self.request(target, "cmd/goto")["ok"])
        self.nav_state("navigating")
        self.finish_cancel.clear()
        self.assertTrue(self.request({}, "cmd/nav_pause")["ok"])
        self.nav_state("pausing")
        self.holds.clear()
        self.spin_for(0.3)
        self.assertGreaterEqual(len(self.holds), 2)
        self.assertTrue(all(h == Twist() for h in self.holds))
        self.assertFalse(self.request({}, "cmd/nav_resume")["ok"])
        self.assertFalse(self.request(target, "cmd/goto")["ok"])
        self.finish_cancel.set()
        self.assertEqual(self.nav_state("paused")["goal"], target)
        self.assertFalse(self.request({"id": "test-map", "name": "changed"}, "cmd/maps/rename")["ok"])
        self.assertTrue(self.request({}, "cmd/nav_resume")["ok"])
        self.nav_state("navigating")
        self.spin_for(0.1)
        self.assertEqual(len(self.goals), 2)
        self.assertEqual(self.goals[0].pose, self.goals[1].pose)
        self.assertTrue(self.request({}, "cmd/nav_cancel")["ok"])
        self.assertIsNone(self.nav_state("canceled")["goal"])
        self.assertFalse(self.request({}, "cmd/nav_resume")["ok"])

    def test_late_acceptance_is_canceled_before_next_goal(self):
        self.accept_goal.clear()
        self.send_without_waiting("cmd/goto", {"x": 2.0, "y": 1.0})
        self.nav_state("accepting")
        self.assertTrue(self.accept_entered.wait(timeout=1))
        self.assertTrue(self.request({}, "cmd/nav_cancel")["ok"])
        self.nav_state("canceling")
        self.assertFalse(self.request({"x": 9.0, "y": 9.0}, "cmd/goto")["ok"])
        self.accept_goal.set()
        self.assertIsNone(self.nav_state("canceled")["goal"])
        self.assertTrue(self.request({"x": 3.0, "y": 4.0}, "cmd/goto")["ok"])
        self.nav_state("navigating")
        self.finish_goal.set()
        self.nav_state("succeeded")

    def test_cancel_rejection_does_not_report_paused_or_resume(self):
        self.assertTrue(self.request({"x": 1.0, "y": 0.0}, "cmd/goto")["ok"])
        self.reject_cancel = True
        self.assertTrue(self.request({}, "cmd/nav_pause")["ok"])
        self.receive(lambda e: e.get("ch") == "evt/log" and
                     e["p"].get("code") == "NAV_CANCEL_REJECTED")
        self.assertFalse(self.request({}, "cmd/nav_resume")["ok"])
        self.nav_state("pausing")
        self.reject_cancel = False
        self.assertTrue(self.request({}, "cmd/nav_cancel")["ok"])
        self.nav_state("canceled")

    def test_rejected_resume_keeps_paused_destination(self):
        target = {"x": 1.0, "y": 2.0, "theta": 0.2}
        self.assertTrue(self.request(target, "cmd/goto")["ok"])
        self.assertTrue(self.request({}, "cmd/nav_pause")["ok"])
        self.nav_state("paused")
        self.reject_goal = True
        self.assertFalse(self.request({}, "cmd/nav_resume")["ok"])
        self.assertEqual(self.nav_state("paused")["goal"], target)
        self.assertTrue(self.request({}, "cmd/nav_cancel")["ok"])
        self.assertIsNone(self.nav_state("canceled")["goal"])

    def test_goal_acceptance_timeout_cancels_late_goal(self):
        self.accept_goal.clear()
        self.assertFalse(self.request({"x": 1.0, "y": 2.0}, "cmd/goto")["ok"])
        self.nav_state("canceling")
        self.accept_goal.set()
        self.nav_state("canceled")
        self.assertFalse(self.request({}, "cmd/nav_resume")["ok"])

    def test_mode_switch_keeps_existing_navigation(self):
        self.assertTrue(self.request({"x": 1.0, "y": 2.0}, "cmd/goto")["ok"])
        self.assertTrue(self.request({"mode": "manual"}, "cmd/mode")["ok"])
        self.nav_state("navigating")
        self.assertFalse(self.request({"x": 3.0, "y": 4.0}, "cmd/goto")["ok"])
        self.assertTrue(self.request({"mode": "auto"}, "cmd/mode")["ok"])
        self.nav_state("navigating")
        self.assertEqual(len(self.goals), 1)
        self.assertTrue(self.request({}, "cmd/nav_cancel")["ok"])
        self.nav_state("canceled")

    def test_progress_and_lifecycle_status(self):
        from lifecycle_msgs.srv import GetState
        state = [3]

        def lifecycle(_, response):
            response.current_state.id = state[0]
            return response

        services = [self.node.create_service(GetState, name + "/get_state", lifecycle)
                    for name in ("/planner_server", "/bt_navigator", "/amcl")]
        self.receive(lambda e: e.get("ch") == "state/nav" and
                     e["p"].get("navigation_state") == "active" and
                     e["p"].get("localization_state") == "active")
        self.assertTrue(self.request({"x": 2.0, "y": 1.0}, "cmd/goto")["ok"])
        report = self.receive(lambda e: e.get("ch") == "state/nav" and
                              e["p"].get("elapsed_s") == 3.0)["p"]
        self.assertAlmostEqual(report["distance_remaining_m"], 4.2, places=5)
        self.assertEqual(report["eta_s"], 18)
        self.assertEqual(report["recoveries"], 2)
        self.publish_feedback = False
        self.receive(lambda e: e.get("ch") == "state/nav" and
                     e["p"].get("status") == "navigating" and
                     e["p"].get("distance_remaining_m") is None)
        state[0] = 2
        self.receive(lambda e: e.get("ch") == "state/nav" and
                     e["p"].get("navigation_state") == "inactive")
        for service in services:
            self.node.destroy_service(service)
        self.receive(lambda e: e.get("ch") == "state/nav" and
                     e["p"].get("navigation_state") == "unknown")

    def test_safety_and_authority_denials_do_not_send_nav2_goal(self):
        for dependency in ("safety", "authority"):
            with self.subTest(dependency=dependency):
                self.safety_accepts = dependency != "safety"
                self.authority_accepts = dependency != "authority"
                result = self.request({"x": 1.0, "y": 2.0}, "cmd/goto")
                self.assertFalse(result["ok"])
                self.assertEqual(result["err"]["code"],
                    "SAFETY_RESUME_GUARD_FAILED" if dependency == "safety" else "AUTHORITY_BUSY")
                self.assertEqual(self.goals, [])
                self.nav_state("rejected")

    def test_accepted_services_still_wait_for_typed_normal_and_base_active(self):
        self.safety_reported = SafetyState.CONTROLLED_STOP
        self.authority_reported = MotionAuthority.ARM_ACTIVE
        self.spin_for(0.2)
        result = self.request({"x": 1.0, "y": 2.0}, "cmd/goto")
        self.assertFalse(result["ok"])
        self.assertIn("시간 초과", result["err"]["msg"])
        self.assertEqual(self.goals, [])
        self.safety_reported = SafetyState.NORMAL
        self.authority_reported = MotionAuthority.BASE_ACTIVE
        self.spin_for(0.2)
        self.assertTrue(self.request({"x": 1.0, "y": 2.0}, "cmd/goto")["ok"])
        self.nav_state("navigating")

    def test_disconnect_requests_safety_stop_and_pauses_current_goal(self):
        target = {"x": 1.0, "y": 2.0, "theta": 0.2}
        self.assertTrue(self.request(target, "cmd/goto")["ok"])
        self.peer.close()
        self.peer = None
        self.spin_for(0.4)
        self.assertTrue(any(event.event == SafetyEvent.REQUEST_STOP for event in self.safety_events))
        self.assertTrue(self.holds)
        # The bridge remains alive and no second action goal is sent by a stale callback.
        self.assertIsNone(self.process.poll())
        self.assertEqual(len(self.goals), 1)

    def test_safety_hold_pauses_goal_and_resume_waits_for_permissions(self):
        target = {"x": 1.0, "y": 2.0, "theta": 0.2}
        self.assertTrue(self.request(target, "cmd/goto")["ok"])
        self.safety_reported = SafetyState.CONTROLLED_STOP
        self.assertEqual(self.nav_state("paused")["goal"], target)
        self.safety_accepts = False
        self.assertFalse(self.request({}, "cmd/nav_resume")["ok"])
        self.assertEqual(self.nav_state("paused")["goal"], target)
        self.safety_accepts = True
        self.safety_reported = SafetyState.NORMAL
        self.assertTrue(self.request({}, "cmd/nav_resume")["ok"])
        self.nav_state("navigating")
        self.spin_for(0.1)
        self.assertEqual(len(self.goals), 2)

    def test_stale_safety_and_authority_pause_existing_goal(self):
        target = {"x": 1.0, "y": 2.0, "theta": 0.2}
        self.assertTrue(self.request(target, "cmd/goto")["ok"])
        self.report_dependencies = False
        self.assertEqual(self.nav_state("paused")["goal"], target)
        self.receive(lambda e: e.get("ch") == "state/safety" and not e["p"].get("state_fresh"))
        self.assertEqual(len(self.goals), 1)

    def test_cancel_during_permission_wait_never_sends_action_goal(self):
        self.safety_reported = SafetyState.CONTROLLED_STOP
        self.spin_for(0.2)
        self.send_without_waiting("cmd/goto", {"x": 1.0, "y": 2.0})
        self.nav_state("accepting")
        self.assertTrue(self.request({}, "cmd/nav_cancel")["ok"])
        self.nav_state("canceled")
        self.safety_reported = SafetyState.NORMAL
        self.spin_for(0.3)
        self.assertEqual(self.goals, [])


if __name__ == "__main__":
    # The inherited fixture's own speed/CRUD tests have a separate CTest target.
    suite = unittest.TestSuite(NavigationControlTest(name) for name in
        NavigationControlTest.__dict__ if name.startswith("test_"))
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    raise SystemExit(not result.wasSuccessful())
