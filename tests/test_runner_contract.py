"""Exercise output admission through the actual owner runner without building firmware."""

import importlib.util
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def load_runner():
    source = ROOT / "tests" / "test_owner.py"
    if not source.is_file():
        source = ROOT / "test.py"
    spec = importlib.util.spec_from_file_location(
        "owner_runner_contract_target", source
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    sys.path.insert(0, str(ROOT / "tests"))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path.pop(0)
    return module


class RunnerContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.workspace = self.root / "dependency workspace"
        self.workspace.mkdir()
        self.module = load_runner()

    def setup_owner(self, output=None, failed_workspace=False):
        def run(command, **kwargs):
            self.assertEqual(command, ["west", "topdir"])
            return subprocess.CompletedProcess(
                command,
                1 if failed_workspace else 0,
                "" if failed_workspace else str(self.workspace) + "\n",
                "workspace unavailable" if failed_workspace else "",
            )

        environment = dict(os.environ)
        environment.pop("OWNER_TEST_OUTPUT", None)
        if output is not None:
            environment["OWNER_TEST_OUTPUT"] = str(output)
        with (
            patch.dict(os.environ, environment, clear=True),
            patch.object(self.module.subprocess, "run", side_effect=run),
        ):
            self.module.WestCommandsTests.setUpClass()
        return self.module.WestCommandsTests.BUILD_DIR

    def test_requested_fresh_output_is_created(self):
        requested = self.root / "retained results" / "new run"
        actual = self.setup_owner(requested)
        self.assertEqual(actual, requested.resolve())
        self.assertTrue(actual.is_dir())
        self.assertFalse((self.workspace / "build").exists())

    def test_existing_output_preserves_prior_evidence(self):
        requested = self.root / "previous run"
        requested.mkdir()
        sentinel = requested / "ordered.log"
        sentinel.write_bytes(b"prior ordered evidence\n")
        with self.assertRaises(FileExistsError):
            self.setup_owner(requested)
        self.assertEqual(sentinel.read_bytes(), b"prior ordered evidence\n")

    def test_failed_workspace_does_not_create_output(self):
        requested = self.root / "must not exist"
        with self.assertRaisesRegex(RuntimeError, "workspace"):
            self.setup_owner(requested, failed_workspace=True)
        self.assertFalse(requested.exists())

    def test_default_output_is_fresh_and_outside_dependencies(self):
        first = self.setup_owner()
        second = self.setup_owner()
        self.addCleanup(shutil.rmtree, first, ignore_errors=True)
        self.addCleanup(shutil.rmtree, second, ignore_errors=True)
        self.assertNotEqual(first, second)
        self.assertTrue(first.is_dir())
        self.assertTrue(second.is_dir())
        self.assertNotIn(self.workspace, first.parents)
        self.assertNotIn(self.workspace, second.parents)


if __name__ == "__main__":
    unittest.main(verbosity=2)
