"""Compatibility entry; executable owner assertions live in tests/."""

from pathlib import Path
import argparse
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent / "tests"))
from test_runner_contract import RunnerContract as RunnerContract
from test_native_verifier import NativeVerifierContract as NativeVerifierContract
from test_build_budget import BuildBudgetContract as BuildBudgetContract
from test_review_dependencies import (
    ReviewDependencyContract as ReviewDependencyContract,
)
from test_owner import WestCommandsTests  # noqa: E402,F401
from test_proto_wire import ProtoWireContract as ProtoWireContract
from test_entry_selection import EntrySelectionContract as EntrySelectionContract


def _run_complete(verbosity):
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    expected = suite.countTestCases()
    if expected == 0:
        print("Complete owner selection is empty", file=sys.stderr)
        return 1
    result = unittest.TextTestRunner(verbosity=verbosity).run(suite)
    if result.testsRun != expected or result.skipped:
        print(
            "Complete owner selection did not finish every required case",
            file=sys.stderr,
        )
        return 1
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run the complete owner selection")
    verbosity = parser.add_mutually_exclusive_group()
    verbosity.add_argument("-v", "--verbose", action="store_const", const=2, default=1)
    verbosity.add_argument(
        "-q", "--quiet", action="store_const", dest="verbose", const=0
    )
    args = parser.parse_args()
    sys.exit(_run_complete(args.verbose))
