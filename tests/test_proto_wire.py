"""Compile both original and compact nanopb storage against fixed wire contracts."""

import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ProtoWireContract(unittest.TestCase):
    @classmethod
    def run_command(cls, command, expected=0):
        started = datetime.datetime.now(datetime.timezone.utc).isoformat()
        clock = time.monotonic_ns()
        result = subprocess.run(
            list(map(str, command)),
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=60,
        )
        prefix = cls.output / f"command-{cls.sequence:03d}"
        cls.sequence += 1
        prefix.with_suffix(".log").write_text(result.stdout, encoding="utf-8")
        prefix.with_suffix(".json").write_text(
            json.dumps(
                {
                    "argv": list(map(str, command)),
                    "cwd": str(ROOT),
                    "start_utc": started,
                    "end_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    "elapsed_ns": time.monotonic_ns() - clock,
                    "exit": result.returncode,
                    "expected_exit": expected,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
        if result.returncode != expected:
            raise AssertionError(
                f"{command}: expected exit {expected}, got {result.returncode}\n"
                + result.stdout
            )
        return result.stdout

    @classmethod
    def setUpClass(cls):
        requested = os.environ.get("PROTO_WIRE_CONTROL_OUTPUT")
        if requested:
            if not Path(requested).is_absolute():
                raise ValueError("PROTO_WIRE_CONTROL_OUTPUT must be absolute")
            if os.path.lexists(requested):
                raise FileExistsError(requested)
            cls.output = Path(requested).resolve()
            cls.output.mkdir(parents=True, exist_ok=False)
        else:
            cls.output = Path(tempfile.mkdtemp(prefix="default-layer-proto-wire-"))
        print(f"Proto wire control output: {cls.output}", flush=True)
        cls.sequence = 0
        nanopb = Path(
            cls.run_command(["west", "list", "nanopb", "-f", "{abspath}"]).strip()
        ).resolve(strict=True)
        cls.binaries = {}
        for variant, options in (
            (
                "original-full-width",
                ROOT / "tests/support/default_layer_full_width.options",
            ),
            (
                "product",
                ROOT / "proto/cormoran/default-layer/default_layer.options",
            ),
        ):
            generated = cls.output / variant
            generated.mkdir()
            cls.run_command(
                [
                    sys.executable,
                    nanopb / "generator/nanopb_generator.py",
                    "--error-on-unmatched",
                    "-I",
                    ROOT / "proto",
                    "-f",
                    options,
                    "-D",
                    generated,
                    ROOT / "proto/cormoran/default-layer/default_layer.proto",
                ]
            )
            binary = generated / "wire-contract"
            cls.run_command(
                [
                    "cc",
                    "-std=c11",
                    "-O2",
                    "-g",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-no-pie",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-omit-frame-pointer",
                    "-fstack-usage",
                    "-DPB_WITHOUT_64BIT=1",
                    "-I",
                    nanopb,
                    "-I",
                    generated,
                    ROOT / "tests/support/default_layer_proto_wire.c",
                    generated / "cormoran/default-layer/default_layer.pb.c",
                    nanopb / "pb_common.c",
                    nanopb / "pb_encode.c",
                    nanopb / "pb_decode.c",
                    "-o",
                    binary,
                ]
            )
            cls.binaries[variant] = binary
        (cls.output / "binaries.json").write_text(
            json.dumps(
                {
                    variant: {
                        "path": str(binary),
                        "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
                    }
                    for variant, binary in cls.binaries.items()
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )

    def test_full_capacity_matches_fixed_vector_and_original_both_directions(self):
        # Packed field1, length32, then the specified ordered indices0..31.
        specified = bytes([0x0A, 32, *range(32)])
        for variant, binary in self.binaries.items():
            available = self.output / f"{variant}-available.bin"
            response = self.output / f"{variant}-response.bin"
            self.assertIn(
                "PASS: available32 ordered0..31",
                self.run_command([binary, "produce-available", available]),
            )
            self.assertEqual(available.read_bytes(), specified)
            self.assertIn(
                "PASS: response endpoints10 os6 layers32",
                self.run_command([binary, "produce-response", response]),
            )
            for consumer in self.binaries.values():
                for _ in range(3):
                    self.assertIn(
                        "PASS: available32 ordered0..31",
                        self.run_command([consumer, "consume-available", available]),
                    )
                    self.assertIn(
                        "PASS: response endpoints10 os6 layers32",
                        self.run_command([consumer, "consume-response", response]),
                    )
        self.assertEqual(
            (self.output / "original-full-width-response.bin").read_bytes(),
            (self.output / "product-response.bin").read_bytes(),
        )

    def test_original_storage_control_fails_only_the_named_storage_assertion(self):
        output = self.run_command(
            [self.binaries["original-full-width"], "storage"], expected=2
        )
        self.assertIn("element=4 capacity=32", output)
        self.assertIn(
            "storage assertion failed: expected byte indices, actual=4", output
        )
        self.assertNotIn("wire assertion failed", output)

    def test_product_storage_retains_all32_entries(self):
        for _ in range(3):
            output = self.run_command([self.binaries["product"], "storage"])
            self.assertIn("element=1 capacity=32", output)
            self.assertIn("PASS: byte storage capacity32", output)

    def test_request_indices_and_mapping_values_remain_full_width(self):
        for binary in self.binaries.values():
            for _ in range(3):
                self.assertIn(
                    "PASS: incoming257 UINT32_MAX INT32_MIN INT32_MAX remain full-width",
                    self.run_command([binary, "request-widths"]),
                )

    def test_outgoing_decoder_rejects_overflow_without_truncation(self):
        self.assertIn(
            "PASS: original full-width decoder retains256",
            self.run_command([self.binaries["original-full-width"], "overflow"]),
        )
        for _ in range(3):
            self.assertIn(
                "PASS: outgoing-only byte decoder rejects256 without truncation",
                self.run_command([self.binaries["product"], "overflow"]),
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
