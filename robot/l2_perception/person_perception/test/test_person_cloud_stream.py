# Copyright (c) 2026 WeGo Robotics. All rights reserved.
# SPDX-License-Identifier: LicenseRef-Wego-Proprietary

"""Exercise the real L2 executable with synthetic frames, never a device SDK."""

import copy
import os
from pathlib import Path
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest


BINARY = sys.argv.pop(1)
os.environ["ROS_DOMAIN_ID"] = str(90 + os.getpid() % 30)
os.environ["ROS_LOCALHOST_ONLY"] = "1"

import rclpy
from rclpy.qos import qos_profile_sensor_data
from interfaces.msg import SemanticDepthFrame
from sensor_msgs.msg import Image, PointCloud2


def image(encoding, width, height, data, step, bigendian=False):
    result = Image()
    result.encoding, result.width, result.height = encoding, width, height
    result.step, result.is_bigendian = step, int(bigendian)
    result.data = data
    return result


class PersonCloudStreamTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rclpy.init()
        cls.node = rclpy.create_node("person_cloud_test")
        cls.messages = {}
        cls.subscriptions = [cls.node.create_subscription(
            msg_type, f"/aurora/person/{suffix}",
            lambda message, suffix=suffix: cls.messages.__setitem__(suffix, message),
            qos_profile_sensor_data)
            for suffix, msg_type in (("points3d", PointCloud2), ("points2d", PointCloud2),
                                     ("camera_image", Image), ("overlay_image", Image))]
        cls.publisher = cls.node.create_publisher(SemanticDepthFrame, "/aurora/imaging/frame",
                                                   qos_profile_sensor_data)
        cls.log = tempfile.TemporaryFile(mode="w+")
        cls.process = subprocess.Popen([BINARY, "--ros-args", "-p", "mask_erosion_pixels:=0",
            "-p", "max_local_depth_jump_m:=0.0", "-p", "input_timeout_ms:=500"],
            stdout=cls.log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 8
        while cls.publisher.get_subscription_count() < 1 and time.monotonic() < deadline:
            rclpy.spin_once(cls.node, timeout_sec=0.05)
        if cls.publisher.get_subscription_count() < 1:
            cls.log.seek(0)
            details = cls.log.read()
            cls.tearDownClass()
            raise RuntimeError("L2 subscriber not ready: " + details)

    @classmethod
    def tearDownClass(cls):
        if cls.process.poll() is None:
            cls.process.send_signal(signal.SIGINT)
            try:
                cls.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                cls.process.kill()
                cls.process.wait(timeout=3)
        cls.log.close()
        cls.node.destroy_node()
        rclpy.shutdown()

    def frame(self, bigendian=False, padding=0):
        frame = SemanticDepthFrame()
        frame.header.stamp = self.node.get_clock().now().to_msg()
        frame.header.frame_id = "aurora_depth_optical"
        frame.semantic_timestamp_ns = 1_000_000_000
        frame.depth_timestamp_ns = 1_000_000_000
        prefix = ">" if bigendian else "<"
        frame.depth_points = image("32FC3", 2, 1,
            struct.pack(prefix + "6f", 0.25, -0.5, 2.0, 0.0, 0.0, 1.0) + b"\0" * padding,
            24 + padding, bigendian)
        frame.depth_labels = image("mono8", 2, 1, bytes([1, 0]), 2)
        frame.semantic = copy.deepcopy(frame.depth_labels)
        frame.camera = image("mono8", 2, 1, bytes([40, 80]), 2)
        frame.camera.header.stamp = frame.header.stamp
        frame.camera.header.frame_id = "aurora_camera_left"
        frame.texture = image("rgb8", 2, 1, bytes([210, 120, 30, 1, 2, 3]), 6)
        return frame

    def receive(self, frame, keys=("points3d", "points2d")):
        self.messages.clear()
        deadline, next_publish = time.monotonic() + 3, 0
        while time.monotonic() < deadline:
            tick = time.monotonic()
            if tick >= next_publish:
                self.publisher.publish(frame)
                next_publish = tick + 0.08
            rclpy.spin_once(self.node, timeout_sec=0.02)
            if all(key in self.messages and self.messages[key].header.stamp == frame.header.stamp
                   for key in keys):
                return {key: self.messages[key] for key in keys}
        self.fail("Expected output for frame stamp not received: " + str(keys))

    def xyz_rgb(self, cloud):
        offsets = {field.name: field.offset for field in cloud.fields}
        xyz = tuple(struct.unpack_from("<f", cloud.data, offsets[axis])[0] for axis in ("x", "y", "z"))
        rgb = struct.unpack_from("<I", cloud.data, offsets["rgb"])[0] & 0xFFFFFF
        return xyz, rgb

    def test_01_person_xyz_color_projection_and_images(self):
        output = self.receive(self.frame(), ("points3d", "points2d", "camera_image", "overlay_image"))
        self.assertEqual(output["points3d"].width, 1)
        self.assertEqual(output["points3d"].header.frame_id, "aurora_depth_local")
        self.assertEqual(self.xyz_rgb(output["points3d"]), ((2.0, -0.25, 0.5), 0xD2781E))
        self.assertEqual(self.xyz_rgb(output["points2d"]), ((2.0, -0.25, 0.0), 0xFF0000))
        self.assertEqual(output["camera_image"].header.frame_id, "aurora_camera_left")
        self.assertEqual(bytes(output["camera_image"].data), bytes([40, 80]))
        overlay = output["overlay_image"]
        self.assertEqual(overlay.encoding, "bgr8")
        self.assertEqual(bytes(overlay.data), bytes([20, 148, 20, 80, 80, 80]))

    def test_02_padded_bigendian_points(self):
        output = self.receive(self.frame(bigendian=True, padding=8))
        self.assertEqual(self.xyz_rgb(output["points3d"])[0], (2.0, -0.25, 0.5))

    def test_03_device_timestamp_mismatch_clears_clouds(self):
        frame = self.frame()
        frame.depth_timestamp_ns += 101_000_000
        output = self.receive(frame)
        self.assertEqual(output["points3d"].width, 0)
        self.assertEqual(output["points2d"].width, 0)

    def test_04_malformed_frames_clear_clouds_and_node_recovers(self):
        good = self.frame()
        variants = []
        bad = copy.deepcopy(good); bad.depth_points.data = bytes(1); variants.append(bad)
        bad = copy.deepcopy(good); bad.depth_labels.width = 1; variants.append(bad)
        bad = copy.deepcopy(good); bad.depth_points.encoding = "32FC1"; variants.append(bad)
        bad = copy.deepcopy(good); bad.depth_points.step = 1; variants.append(bad)
        bad = copy.deepcopy(good); bad.depth_points.width = 0xFFFFFFFF; variants.append(bad)
        for frame in variants:
            with self.subTest(encoding=frame.depth_points.encoding, step=frame.depth_points.step):
                frame.header.stamp = self.node.get_clock().now().to_msg()
                self.assertEqual(self.receive(frame)["points3d"].width, 0)
        self.assertEqual(self.receive(self.frame())["points3d"].width, 1)

    def test_05_invalid_texture_falls_back_to_gray(self):
        frame = self.frame()
        frame.texture.encoding = "unsupported"
        output = self.receive(frame)
        self.assertEqual(self.xyz_rgb(output["points3d"])[1], 0x808080)

    def test_06_empty_input_and_no_person_clear_clouds(self):
        frame = SemanticDepthFrame()
        frame.header.stamp = self.node.get_clock().now().to_msg()
        self.assertEqual(self.receive(frame)["points3d"].width, 0)
        frame = self.frame()
        frame.depth_labels.data = bytes([0, 0])
        self.assertEqual(self.receive(frame)["points3d"].width, 0)

    def test_07_nonfinite_depth_rejected(self):
        frame = self.frame()
        frame.depth_points.data = struct.pack("<6f", 0.25, float("nan"), 2, 0, 0, 1)
        self.assertEqual(self.receive(frame)["points3d"].width, 0)

    def test_08_input_timeout_clears_previous_person(self):
        self.assertEqual(self.receive(self.frame())["points3d"].width, 1)
        self.messages.clear()
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            rclpy.spin_once(self.node, timeout_sec=0.02)
            if all(key in self.messages and self.messages[key].width == 0
                   for key in ("points3d", "points2d")):
                return
        self.fail("Last person cloud survived the input watchdog")


if __name__ == "__main__":
    unittest.main(verbosity=2)
