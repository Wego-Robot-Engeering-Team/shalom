"""Keep versions, language coverage and SDK documentation synchronized."""
import ast
import json
from pathlib import Path
import re
import unittest
import robot_sdk

PLATFORM_ROOT = Path(__file__).resolve().parents[2]
ROOT = PLATFORM_ROOT.parent


class ContractTests(unittest.TestCase):
    def test_versions_match(self):
        version = (ROOT / "VERSION").read_text().strip()
        self.assertEqual(robot_sdk.__version__, version)
        self.assertIn(f'version = "{version}"', (PLATFORM_ROOT / "python/pyproject.toml").read_text())
        self.assertIn(f"VERSION {version}", (PLATFORM_ROOT / "cpp/CMakeLists.txt").read_text())
        self.assertIn(version, (ROOT / "NOTICE").read_text())
        self.assertEqual((ROOT / "LICENSE").read_text(), (PLATFORM_ROOT / "python/LICENSE").read_text())

    def test_platform_sources_match(self):
        self.assertFalse((ROOT / "cpp").exists())
        self.assertFalse((ROOT / "python").exists())
        def sources(platform):
            files = {}
            for language in ("cpp", "python"):
                for file in (ROOT / platform / language).rglob("*"):
                    relative = file.relative_to(ROOT / platform)
                    if not file.is_file() or any(
                        part in ("build", "__pycache__") or part.endswith(".egg-info")
                        for part in relative.parts
                    ):
                        continue
                    files[str(relative)] = file.read_bytes()
            return files
        linux = sources("Linux")
        self.assertTrue(linux)
        for platform in ("MacOS", "Windows"):
            self.assertEqual(linux, sources(platform), platform)

    def test_cpp_python_and_api_document_have_same_channels(self):
        python = set()
        for file in (PLATFORM_ROOT / "python/robot_sdk/api").glob("*.py"):
            for node in ast.walk(ast.parse(file.read_text())):
                if isinstance(node, ast.Constant) and isinstance(node.value, str) and node.value.startswith("cmd/"):
                    python.add(node.value)
        cpp = set()
        for file in (PLATFORM_ROOT / "cpp/src/api").glob("*.*"):
            cpp.update(re.findall(r'"(cmd/[^"\s]+)"', file.read_text()))
        self.assertEqual(python, cpp)
        documented = set(re.findall(r"\| `(cmd/[^`]+)` \|", (ROOT / "docs/API.md").read_text()))
        self.assertEqual(python, documented)

    def test_relative_document_links_resolve(self):
        for file in ROOT.rglob("*.md"):
            for target in re.findall(r"\]\(([^)]+)\)", file.read_text()):
                if "://" in target or target.startswith("#"):
                    continue
                relative = target.split("#", 1)[0]
                self.assertTrue((file.parent / relative).exists(), f"broken link in {file}: {target}")

    def test_document_json_examples_parse(self):
        for file in (ROOT / "docs").glob("*.md"):
            for block in re.findall(r"```json\s*\n(.*?)```", file.read_text(), re.DOTALL):
                json.loads(block)

    def test_error_catalog_matches_hmi_source(self):
        source = ROOT.parent / "resources/error_codes.json"
        if not source.exists():
            self.skipTest("standalone SDK distribution")
        self.assertEqual(json.loads(source.read_text()), json.loads((ROOT / "docs/error_codes.json").read_text()))

    def test_channels_exist_in_bridge_source(self):
        source = ROOT.parents[1] / "robot/l4_communication/hmi_bridge/src/bridge_node.cpp"
        if not source.exists():
            self.skipTest("standalone SDK distribution")
        sdk = set()
        for file in (PLATFORM_ROOT / "python/robot_sdk/api").glob("*.py"):
            sdk.update(re.findall(r'"(cmd/[^"\s]+)"', file.read_text()))
        bridge = set(re.findall(r'"(cmd/[^"\s]+)"', source.read_text()))
        self.assertTrue(sdk.issubset(bridge), f"unsupported SDK channels: {sdk - bridge}")


if __name__ == "__main__":
    unittest.main()
