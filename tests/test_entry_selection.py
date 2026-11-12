"""Keep canonical entries complete; focused diagnostic entries remain separate."""

import contextlib
import importlib.util
import io
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]


def load_entry():
    spec = importlib.util.spec_from_file_location(
        "complete_owner_entry_target", ROOT / "test.py"
    )
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    previous = sys.path[:]
    sys.path.insert(0, str(ROOT))
    try:
        spec.loader.exec_module(module)
    finally:
        sys.path[:] = previous
    return module


class EntrySelectionContract(unittest.TestCase):
    def test_owner_entry_rejects_focused_selectors_before_runtime(self):
        for selector in (
            ["WestCommandsTests.test_zmk_build"],
            ["-k", "test_zmk_build"],
            ["--failfast"],
        ):
            with self.subTest(selector=selector):
                result = subprocess.run(
                    [sys.executable, str(ROOT / "test.py"), *selector],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    timeout=10,
                )
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("unrecognized arguments", result.stderr)
                self.assertNotIn("Owner test output:", result.stdout)

    def test_web_entry_rejects_focused_selectors_before_generation(self):
        for selector in (
            ["lifecycle"],
            ["--passWithNoTests"],
            ["--testNamePattern", "absent"],
        ):
            with self.subTest(selector=selector):
                result = subprocess.run(
                    ["bash", str(ROOT / "tests/run-web.sh"), *selector],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    timeout=10,
                )
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
                self.assertIn("complete Web entry accepts no selectors", result.stderr)
                self.assertNotIn("buf generate", result.stdout)

    def test_complete_owner_entry_rejects_empty_collection(self):
        module = load_entry()
        with (
            patch.object(
                module.unittest.defaultTestLoader,
                "loadTestsFromModule",
                return_value=unittest.TestSuite(),
            ),
            patch.object(module.unittest, "TextTestRunner") as runner,
            contextlib.redirect_stderr(io.StringIO()) as output,
        ):
            self.assertEqual(module._run_complete(0), 1)
            runner.assert_not_called()
        self.assertIn("selection is empty", output.getvalue())

    def test_complete_owner_entry_rejects_missing_or_skipped_completions(self):
        module = load_entry()
        suite = unittest.TestSuite([unittest.FunctionTestCase(lambda: None)])
        for completed, skipped in ((0, []), (1, [("required", "unavailable")])):
            with (
                self.subTest(completed=completed, skipped=skipped),
                patch.object(
                    module.unittest.defaultTestLoader,
                    "loadTestsFromModule",
                    return_value=suite,
                ),
                patch.object(module.unittest, "TextTestRunner") as runner,
                contextlib.redirect_stderr(io.StringIO()) as output,
            ):
                result = runner.return_value.run.return_value
                result.testsRun = completed
                result.skipped = skipped
                result.wasSuccessful.return_value = True
                self.assertEqual(module._run_complete(0), 1)
            self.assertIn("did not finish every required case", output.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
