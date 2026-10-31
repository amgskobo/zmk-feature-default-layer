"""Compatibility entry; executable owner assertions live in tests/."""

from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent / "tests"))
from test_runner_contract import RunnerContract as RunnerContract
from test_native_verifier import NativeVerifierContract as NativeVerifierContract
from test_build_budget import BuildBudgetContract as BuildBudgetContract
from test_owner import WestCommandsTests  # noqa: E402,F401

if __name__ == "__main__":
    unittest.main()
