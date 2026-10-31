from __future__ import annotations

import platform
import datetime
import json
import os
import tempfile
import time
import subprocess
import unittest
from pathlib import Path

from native_verifier import verify_native

from dataclasses import dataclass

from test_build_budget import BuildBudgetContract as BuildBudgetContract

THIS_DIR = Path(__file__).resolve().parents[1]
TEST_BUILD_DIR_NAME = "tests-zmk-feature-default-layer"


_command_records = []
_command_output = None


def _flush_commands():
    if _command_output is None:
        return
    _command_output.mkdir(parents=True, exist_ok=True)
    for index, record in enumerate(_command_records):
        prefix = _command_output / f"{index:03d}"
        prefix.with_suffix(".json").write_text(
            json.dumps(
                {key: value for key, value in record.items() if key != "raw"}, indent=2
            )
            + "\n",
            encoding="utf-8",
        )
        prefix.with_suffix(".log").write_text(record["raw"], encoding="utf-8")


def run_west(args: list[str]) -> subprocess.CompletedProcess[str]:
    if args and args[0] == "zmk-build":
        parallelism = int(os.environ.get("OWNER_TEST_BUILD_PARALLELISM", "1"))
        if parallelism < 1:
            raise ValueError("OWNER_TEST_BUILD_PARALLELISM must be a positive integer")
        args = [args[0], "--parallelism", str(parallelism), *args[1:]]
    command = ["west", *args]
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    clock = time.monotonic_ns()
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        cwd=THIS_DIR,
    )
    _command_records.append(
        {
            "argv": command,
            "cwd": str(THIS_DIR),
            "exit": result.returncode,
            "start_utc": started,
            "end_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
            "elapsed_ns": time.monotonic_ns() - clock,
            "raw": result.stdout,
        }
    )
    _flush_commands()
    return subprocess.CompletedProcess(command, result.returncode, result.stdout, "")


@dataclass
class NotFound:
    text: str


@dataclass
class ConfigAndDeviceTree:
    # Expected rows in .config
    config: list[str | NotFound]
    # Expected rows in devicetree_generated.h
    device: list[str | NotFound]


class WestCommandsTests(unittest.TestCase):
    WEST_TOPDIR: Path
    BUILD_DIR: Path

    @classmethod
    def setUpClass(cls):
        global _command_output
        result = run_west(["topdir"])
        selected = result.stdout.strip()
        if result.returncode or not selected or not Path(selected).is_absolute():
            raise RuntimeError(
                "Cannot resolve the selected west workspace: "
                + result.stdout
                + result.stderr
            )
        cls.WEST_TOPDIR = Path(selected).resolve(strict=True)
        requested = os.environ.get("OWNER_TEST_OUTPUT")
        if requested:
            cls.BUILD_DIR = Path(requested).resolve()
            cls.BUILD_DIR.mkdir(parents=True, exist_ok=False)
        else:
            cls.BUILD_DIR = Path(tempfile.mkdtemp(prefix=THIS_DIR.name + "-tests-"))
        _command_output = cls.BUILD_DIR / "commands"
        _flush_commands()
        print(f"Owner test output: {cls.BUILD_DIR}", flush=True)

    @unittest.skipUnless(
        platform.system() == "Linux", "zmk-test is only supported on Linux"
    )
    def test_zmk_test(self):
        test_build_dir = self.BUILD_DIR / TEST_BUILD_DIR_NAME

        result = run_west(["zmk-test", "tests", "-m", ".", "-d", str(test_build_dir)])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        verify_native(test_build_dir, THIS_DIR / "tests")
        self.assertIn("PASS: test", result.stdout, result.stdout + result.stderr)
        self.assertIn("PASS: studio", result.stdout, result.stdout + result.stderr)
        self.assertNotIn("FAILED: ", result.stdout, result.stdout + result.stderr)

    def test_zmk_build(self):
        self._test_zmk_build(
            {
                "default_layer_board_disabled": ConfigAndDeviceTree(
                    config=[
                        'CONFIG_ZMK_KEYBOARD_NAME="Module Test"',
                        "CONFIG_ZMK_USB=y",
                        "CONFIG_ZMK_BLE=y",
                        "# CONFIG_ZMK_DEFAULT_LAYER is not set",
                    ],
                    device=[
                        "DT_COMPAT_HAS_OKAY_zmk_keymap",
                    ],
                ),
                "default_layer_board_without_rpc": ConfigAndDeviceTree(
                    config=[
                        "CONFIG_ZMK_DEFAULT_LAYER=y",
                        "CONFIG_ZMK_DEFAULT_LAYER_MAX_INDEX=3",
                        "# CONFIG_ZMK_STUDIO is not set",
                        NotFound("CONFIG_ZMK_DEFAULT_LAYER_STUDIO_RPC"),
                    ],
                    device=[],
                ),
                "default_layer_board_with_rpc": ConfigAndDeviceTree(
                    config=[
                        # Verify that zmk-feature-custom-settings is present and enabled
                        "zmk-feature-custom-settings",
                        "CONFIG_ZMK_STUDIO=y",
                        "CONFIG_ZMK_DEFAULT_LAYER=y",
                        "CONFIG_ZMK_DEFAULT_LAYER_STUDIO_RPC=y",
                        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
                        "CONFIG_ZMK_CUSTOM_SETTINGS_STUDIO_RPC=y",
                        "CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=128",
                        "CONFIG_ZMK_LOW_PRIORITY_THREAD_STACK_SIZE=2048",
                    ],
                    device=[],
                ),
                "default_layer_board_full": ConfigAndDeviceTree(
                    config=[
                        # Verify that zmk-feature-os-detection is present and enabled
                        "zmk-feature-os-detection",
                        "CONFIG_ZMK_STUDIO=y",
                        "CONFIG_ZMK_DEFAULT_LAYER=y",
                        "CONFIG_ZMK_DEFAULT_LAYER_STUDIO_RPC=y",
                        "CONFIG_ZMK_CUSTOM_SETTINGS=y",
                        "CONFIG_ZMK_DEFAULT_LAYER_OS_DETECTION=y",
                        "CONFIG_ZMK_OS_DETECTION=y",
                        "CONFIG_ZMK_OS_DETECTION_USB=y",
                        "CONFIG_ZMK_OS_DETECTION_BLE=y",
                    ],
                    device=[],
                ),
            }
        )

    def _test_zmk_build(
        self, artifacts_and_expected_build_params: dict[str, ConfigAndDeviceTree]
    ):

        result = run_west(
            ["zmk-build", "tests/zmk-config", "-d", str(self.BUILD_DIR), "-q"]
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        for artifact, entries in artifacts_and_expected_build_params.items():
            artifact_dir = self.BUILD_DIR / artifact / "zephyr"
            config_path = artifact_dir / ".config"
            device_tree_path = (
                artifact_dir
                / "include"
                / "generated"
                / "zephyr"
                / "devicetree_generated.h"
            )
            self._test_strings_in_file(
                config_path, entries.config, f"{artifact} config"
            )
            if entries.device:
                self._test_strings_in_file(
                    device_tree_path, entries.device, f"{artifact} device tree"
                )
            self.assertTrue(
                (artifact_dir / "zmk.uf2").exists(),
                f"{artifact} zmk.uf2 is missing in {artifact_dir}",
            )

    def _test_strings_in_file(
        self, file_path: Path, expected_strings: list[str | NotFound], hint: str
    ):
        self.assertTrue(file_path.exists(), f"{hint}: {file_path} is missing")
        file_text = file_path.read_text()

        for expected in expected_strings:
            if isinstance(expected, NotFound):
                if expected.text in file_text:
                    self.fail(
                        f"{hint}: {expected.text} found in {file_path}, but it should not be present"
                    )
            else:
                if expected not in file_text:
                    self.fail(f"{hint}: {expected} not found in {file_path}")


if __name__ == "__main__":
    unittest.main()
