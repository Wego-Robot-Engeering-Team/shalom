"""SDK-only tests. All network tests use a loopback fake bridge."""
import copy
import json
import math
import socket
import struct
import threading
import time
import unittest
from robot_sdk import Client, ClientError, ConnectionClosed, ProtocolError, RequestTimeout, RobotApi, RobotMismatchError
from robot_sdk.api.inspection import InspectionApi
from robot_sdk.framing import FrameDecoder, FramingError, MAX_BODY_LENGTH, encode_frame
from robot_sdk.types import Message, Response


def envelope(kind="pub", channel="state/pose", payload=None, **fields):
    value = {"v": 1, "t": kind, "ts": time.time(), "p": {} if payload is None else payload, "robot": "SDK-test"}
    if kind != "hb":
        value["ch"] = channel
    value.update(fields)
    return value


class Peer:
    def __init__(self, handler):
        self.listener = socket.socket()
        self.listener.bind(("127.0.0.1", 0))
        self.listener.listen()
        self.port = self.listener.getsockname()[1]
        self.error = None
        self.requests = []
        self.beats = 0
        self.socket = None
        self.stopping = False
        def serve():
            try:
                self.socket, _ = self.listener.accept()
                self.socket.settimeout(0.1)
                decoder = FrameDecoder()
                while not self.stopping:
                    try:
                        chunk = self.socket.recv(16384)
                    except socket.timeout:
                        continue
                    if not chunk:
                        break
                    for frame in decoder.feed(chunk):
                        request = json.loads(frame.header)
                        if request["t"] == "hb":
                            self.beats += 1
                        else:
                            self.requests.append(request)
                            handler(self, request)
            except OSError as exc:
                if not self.stopping:
                    self.error = exc
            except Exception as exc:
                self.error = exc
        self.thread = threading.Thread(target=serve, daemon=True)
        self.thread.start()

    def send(self, value, fragment=False):
        frame = encode_frame(value)
        if fragment:
            for start in range(0, len(frame), 7):
                self.socket.sendall(frame[start:start + 7])
        else:
            self.socket.sendall(frame)

    def reply(self, request, ok=True):
        self.send(envelope("res", request["ch"], {"ok": ok}, id=request["id"]))

    def close(self):
        self.stopping = True
        if self.socket is not None:
            try:
                self.socket.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.socket.close()
        self.listener.close()
        self.thread.join(2)
        if self.thread.is_alive():
            raise AssertionError("test peer failed to stop")
        if self.error:
            raise self.error


class RecordingClient:
    def __init__(self):
        self.calls = []
    def request(self, channel, payload=None, timeout_s=3.0):
        self.calls.append((channel, copy.deepcopy(payload or {}), timeout_s))
        return Response("test", True, None, None, Message(envelope("res", channel, {"ok": True}, id="test"), b""))


class FramingTests(unittest.TestCase):
    def test_every_fragment_and_binary_payload(self):
        original = envelope()
        encoded = encode_frame(original, b"\x00SHLM\xff")
        decoder = FrameDecoder()
        frames = []
        for byte in encoded:
            frames += decoder.feed(bytes([byte]))
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0].payload, b"\x00SHLM\xff")
        self.assertEqual(json.loads(frames[0].header), original)

    def test_concatenated_frames(self):
        decoder = FrameDecoder()
        self.assertEqual(len(decoder.feed(encode_frame(envelope()) * 4)), 4)

    def test_bad_magic_size_and_header_length(self):
        for raw in (b"BAD!" + struct.pack("<I", 4), b"SHLM" + struct.pack("<I", 3),
                    b"SHLM" + struct.pack("<I", MAX_BODY_LENGTH + 1), b"SHLM" + struct.pack("<II", 4, 1)):
            with self.subTest(raw=raw), self.assertRaises(FramingError):
                FrameDecoder().feed(raw)

    def test_encoder_rejects_non_finite(self):
        for value in (math.nan, math.inf, -math.inf):
            with self.assertRaises(ValueError):
                encode_frame({"value": value})


class ClientTests(unittest.TestCase):
    def connect_peer(self, handler):
        peer = Peer(handler)
        self.addCleanup(peer.close)
        client = Client()
        client.connect("127.0.0.1", peer.port)
        self.addCleanup(client.close)
        return client, peer

    def test_fragmented_reply_and_state_callbacks(self):
        def handler(peer, request):
            peer.send(envelope(payload={"x": 2.0}), fragment=True)
            peer.send(envelope("res", request["ch"], {"ok": True, "file": "image.png"}, id=request["id"]), fragment=True)
        client, peer = self.connect_peer(handler)
        observed = []
        reply = client.request("cmd/maps/list", on_message=observed.append)
        self.assertTrue(reply.ok)
        self.assertEqual(reply.data["file"], "image.png")
        self.assertEqual(client.robot_id, "SDK-test")
        self.assertEqual(client.latest("state/pose").envelope["p"]["x"], 2.0)
        self.assertGreaterEqual(peer.beats, 1)
        self.assertEqual(len(observed), 1)

    def test_state_cache_is_a_copy_and_clears_on_close(self):
        client, _ = self.connect_peer(lambda peer, req: (peer.send(envelope(payload={"x": 1})), peer.reply(req)))
        client.request("cmd/maps/list")
        value = client.latest("state/pose")
        self.assertGreaterEqual(value.age_s, 0)
        value.envelope["p"]["x"] = 99
        self.assertEqual(client.latest("state/pose").envelope["p"]["x"], 1)
        client.close()
        self.assertIsNone(client.latest("state/pose"))
        self.assertIsNone(client.robot_id)
        self.assertFalse(client.connected)

    def test_default_handler_receives_states_during_facade_requests(self):
        client, _ = self.connect_peer(lambda peer, req: (peer.send(envelope()), peer.reply(req)))
        observed = []
        client._on_message = observed.append
        self.assertTrue(RobotApi(client).list_maps().ok)
        self.assertEqual(observed[0].channel, "state/pose")

    def test_wrong_channel_is_terminal(self):
        client, _ = self.connect_peer(lambda peer, req: peer.send(envelope("res", "cmd/mode", {"ok": True}, id=req["id"])))
        with self.assertRaises(ProtocolError):
            client.request("cmd/maps/list")
        self.assertFalse(client.connected)

    def test_robot_change_is_terminal(self):
        client, _ = self.connect_peer(lambda peer, req: (peer.send(envelope()), peer.send(envelope(robot="Other"))))
        with self.assertRaises(RobotMismatchError):
            client.request("cmd/maps/list")
        self.assertFalse(client.connected)

    def test_malformed_envelopes(self):
        for updates in ({"v": True}, {"v": 1.0}, {"t": "unknown"}, {"ch": 42}, {"p": []}, {"ts": True}, {"robot": ""}):
            client = Client()
            with self.subTest(updates=updates), self.assertRaises(ProtocolError):
                client._decode(encode_frame(envelope(**updates)))

    def test_response_requires_bool_ok(self):
        for payload in ({}, {"ok": 1}, {"ok": "true"}, {"ok": False, "err": {"code": 3, "msg": "x"}}):
            with self.subTest(payload=payload), self.assertRaises(ProtocolError):
                Client()._decode(encode_frame(envelope("res", payload=payload, id="c1")))

    def test_peer_close_clears_connection(self):
        def handler(peer, request):
            peer.socket.shutdown(socket.SHUT_WR)
        client, _ = self.connect_peer(handler)
        with self.assertRaises(ConnectionClosed):
            client.request("cmd/maps/list")
        self.assertFalse(client.connected)

    def test_timeout_keeps_heartbeat_and_does_not_retry(self):
        client, peer = self.connect_peer(lambda *_: None)
        with self.assertRaises(RequestTimeout):
            client.request("cmd/maps/list", timeout_s=0.65)
        self.assertTrue(client.connected)
        self.assertGreaterEqual(peer.beats, 3)
        self.assertEqual(len(peer.requests), 1)
        self.assertEqual(client._pending, {})
        self.assertEqual(client._responses, {})

    def test_nonblocking_poll_receives(self):
        client, peer = self.connect_peer(lambda peer, request: peer.reply(request))
        request_id = client.send_request("cmd/maps/list")
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            client.poll(0)
            reply = client.take_response(request_id)
            if reply:
                self.assertTrue(reply.ok)
                return
            time.sleep(0.001)
        self.fail("poll(0) never received the reply")

    def test_async_reply_is_consumed_once(self):
        client, _ = self.connect_peer(lambda peer, request: peer.reply(request))
        request_id = client.send_request("cmd/maps/list")
        while not client._responses:
            client.poll()
        self.assertTrue(client.take_response(request_id).ok)
        self.assertIsNone(client.take_response(request_id))

    def test_unsolicited_responses_do_not_accumulate(self):
        client = Client()
        for i in range(256):
            client._decode(encode_frame(envelope("res", payload={"ok": True}, id=str(i))))
        self.assertEqual(client._responses, {})

    def test_pending_requests_are_bounded(self):
        client, _ = self.connect_peer(lambda *_: None)
        for _ in range(128):
            client.send_request("cmd/maps/list")
        with self.assertRaises(ClientError):
            client.send_request("cmd/maps/list")
        client.forget_request("c1")
        client.send_request("cmd/maps/list")

    def test_invalid_arguments_do_not_send(self):
        client, peer = self.connect_peer(lambda *_: None)
        for value in (-1, math.nan, math.inf, True):
            with self.assertRaises(ValueError):
                client.request("cmd/maps/list", timeout_s=value)
        with self.assertRaises(ValueError):
            client.send_request("", {})
        with self.assertRaises(ValueError):
            client.send_request("cmd/maps/list", [])
        with self.assertRaises(ValueError):
            client.publish("cmd/cmd_vel", {"vx": 1})
        self.assertEqual(peer.requests, [])


class ApiTests(unittest.TestCase):
    def setUp(self):
        self.client = RecordingClient()
        self.api = RobotApi(self.client)

    def test_waypoints_send_original_snapshot_and_preserve_description(self):
        original = [{"id": "a", "x": 1.0, "y": 2.0, "status": "current", "description": "old"}]
        changed = [dict(original[0], x=3.0, description="new")]
        self.api.set_waypoints(changed, map_id="map", expected_points=original)
        channel, payload, _ = self.client.calls[-1]
        self.assertEqual(channel, "cmd/waypoints/set")
        self.assertEqual(payload["expected_points"][0]["description"], "old")
        self.assertEqual(payload["points"][0]["description"], "new")
        self.assertNotIn("status", payload["expected_points"][0])
        self.assertIn("status", original[0])

    def test_waypoints_require_explicit_snapshot_even_when_clearing(self):
        with self.assertRaises(TypeError):
            self.api.set_waypoints([])
        self.api.set_waypoints([], map_id="map", expected_points=[])
        self.assertEqual(self.client.calls[-1][1]["points"], [])

    def test_marker_z_yaw_and_snapshot(self):
        marker = {"id": 2, "x": 1, "y": 2, "z": 1.2, "yaw": 0.5, "description": "wall"}
        self.api.set_markers([marker], map_id="map", expected_markers=[])
        self.assertEqual(self.client.calls[-1][1]["markers"], [marker])
        for changed in (dict(marker, yaw=4), dict(marker, id=True), {"id": 2, "x": 1, "y": 2, "z": 1}):
            with self.assertRaises(ValueError):
                self.api.set_markers([changed], map_id="map", expected_markers=[])

    def test_mission_id_is_explicit(self):
        self.api.mission_start("inspection")
        self.assertEqual(self.client.calls[-1][1], {"mission_id": "inspection"})
        with self.assertRaises(TypeError):
            self.api.mission_start()

    def test_mission_validation_and_revision(self):
        mission = {"id": "m", "name": "검사", "description": "retain", "steps": [
            {"id": "n", "type": "navigate", "location_id": "wp"},
            {"id": "c", "type": "capture", "preset": "left"},
            {"id": "a", "type": "arm_move", "pose": "arm"}, {"id": "d", "type": "dock"}]}
        self.api.save_mission(mission, map_id="map", expected_revision=2)
        self.assertEqual(self.client.calls[-1][1]["mission"]["description"], "retain")
        self.assertNotIn("map_id", mission)
        with self.assertRaises(ValueError):
            self.api.save_mission(dict(mission, map_id="other"), map_id="map")
        with self.assertRaises(ValueError):
            self.api.archive_mission("m", map_id="map", expected_revision=True)

    def test_arm_pose_api_also_works_without_mission_mixin(self):
        pose = {"id": "arm", "name": "Arm", "positions": [0.0] * 6}
        InspectionApi(self.client).save_arm_pose(pose)
        self.api.update_arm_pose(pose, 1)
        self.api.archive_arm_pose("arm", 2)
        self.assertEqual(self.client.calls[-1][1]["expected_revision"], 2)
        with self.assertRaises(ValueError):
            self.api.arm_joint_goal([0.0] * 5)

    def test_capture_includes_train_number(self):
        self.api.trigger_capture("GTXA", "1234", "05", "P1", 7)
        self.assertEqual(self.client.calls[-1][1]["train_number"], "1234")

    def test_new_navigation_commands(self):
        self.api.pause_navigation()
        self.api.resume_navigation()
        self.api.set_initial_pose(1, 2, 0.5)
        self.api.set_speed_limits(0.3, 0.5)
        self.api.set_speed_ranges(0.1, 0.6, 0.05, 0.8)
        self.assertEqual(self.client.calls[-1][1]["max_angular_speed_rps"], 0.8)
        self.assertEqual(len(self.client.calls), 5)

    def test_invalid_speeds_bool_and_nan(self):
        for linear, angular in ((True, 0.5), (0.05, 0.5), (0.3, math.nan), (0.3, 1)):
            with self.assertRaises(ValueError):
                self.api.set_speed_limits(linear, angular)
        self.assertEqual(self.client.calls, [])

    def test_damp_requires_confirmation(self):
        with self.assertRaises(ValueError):
            self.api.set_base_posture("damp")
        self.api.set_base_posture("damp", confirm=True)
        self.assertTrue(self.client.calls[-1][1]["confirm"])

    def test_map_management_and_default_clear(self):
        self.api.rename_map("old", "new")
        self.api.delete_map("old")
        self.api.set_default_map("")
        self.assertEqual(self.client.calls[-1][1], {"id": ""})

    def test_locations_require_finite_unique_kinds(self):
        dock = {"kind": "dock", "x": 1, "y": 2, "theta": 0}
        self.api.set_locations([dock], map_id="map", expected_locations=[])
        with self.assertRaises(ValueError):
            self.api.set_locations([dock, dock], map_id="map", expected_locations=[])


if __name__ == "__main__":
    unittest.main()
