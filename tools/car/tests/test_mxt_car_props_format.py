"""Regression tests for the authoritative MXT car-properties codec."""

from __future__ import annotations

from pathlib import Path
import re
import struct
import sys
import unittest
import zlib

CAR_TOOL_DIR = Path(__file__).resolve().parents[1]
if str(CAR_TOOL_DIR) not in sys.path:
    sys.path.insert(0, str(CAR_TOOL_DIR))

from mxt_car_props_format import (
    Curve,
    CurveKey,
    SCHEMA_FINGERPRINT,
    default_properties,
    properties_from_json,
    properties_to_json,
    read_binary,
    write_binary,
)


REPO_ROOT = Path(__file__).resolve().parents[3]
TRACKED_CARS = (
    "mxto/vehicle/asset/accelerator/golden_fox.mxt_car_props",
    "mxto/vehicle/asset/allrounder/blue_falcon.mxt_car_props",
    "mxto/vehicle/asset/bruiser/wild_goose.mxt_car_props",
    "mxto/vehicle/asset/topspeeder/fire_stingray.mxt_car_props",
)


def _replace_payload(encoded: bytes, payload: bytes) -> bytes:
    out = bytearray(encoded)
    out[16:20] = struct.pack("<I", len(payload))
    out[20:24] = struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF)
    out[24:] = payload
    return bytes(out)


class CurveTests(unittest.TestCase):
    def test_cubic_bezier_sampling_and_clamping(self) -> None:
        curve = Curve([
            CurveKey(0.0, 0.0, 0.0, 3.0),
            CurveKey(1.0, 1.0, 0.0, 0.0),
        ])
        self.assertEqual(curve.sample(-1.0), 0.0)
        self.assertAlmostEqual(curve.sample(0.5), 0.875, places=7)
        self.assertEqual(curve.sample(2.0), 1.0)

    def test_constant_curve(self) -> None:
        curve = Curve.constant(19.25)
        for setting in (0.0, 0.25, 0.5, 0.75, 1.0):
            self.assertEqual(curve.sample(setting), 19.25)
            self.assertEqual(curve.derivative(setting), 0.0)

    def test_cubic_derivative_matches_end_tangents_and_finite_difference(self) -> None:
        curve = Curve([
            CurveKey(0.0, 2.0, 0.0, -3.8),
            CurveKey(0.5, 0.1, -3.8, 0.0),
            CurveKey(1.0, 0.1, 0.0, 0.0),
        ])
        self.assertAlmostEqual(curve.derivative(0.0), -3.8, places=7)
        self.assertAlmostEqual(curve.derivative(0.5), -3.8, places=7)
        self.assertAlmostEqual(curve.derivative(0.75), 0.0, places=7)
        epsilon = 1.0e-5
        finite_difference = (curve.sample(0.25 + epsilon) - curve.sample(0.25 - epsilon)) / (2.0 * epsilon)
        self.assertAlmostEqual(curve.derivative(0.25), finite_difference, places=6)


class BinaryCodecTests(unittest.TestCase):
    def test_binary_and_json_round_trips_are_canonical(self) -> None:
        properties = default_properties()
        encoded = write_binary(properties)
        self.assertEqual(write_binary(read_binary(encoded)), encoded)
        as_json = properties_to_json(properties)
        self.assertEqual(write_binary(properties_from_json(as_json)), encoded)

    def test_all_tracked_cars_round_trip_bit_exactly(self) -> None:
        for relative_path in TRACKED_CARS:
            with self.subTest(car=relative_path):
                encoded = (REPO_ROOT / relative_path).read_bytes()
                decoded = read_binary(encoded)
                self.assertEqual(write_binary(decoded), encoded)
                self.assertEqual(
                    write_binary(properties_from_json(properties_to_json(decoded))),
                    encoded,
                )

    def test_header_corruption_is_rejected(self) -> None:
        encoded = write_binary(default_properties())
        corruptions: list[bytes] = []
        mutated = bytearray(encoded)
        mutated[0] ^= 0xFF
        corruptions.append(bytes(mutated))
        mutated = bytearray(encoded)
        mutated[8] ^= 0x01
        corruptions.append(bytes(mutated))
        mutated = bytearray(encoded)
        mutated[16:20] = struct.pack("<I", len(encoded))
        corruptions.append(bytes(mutated))
        mutated = bytearray(encoded)
        mutated[-1] ^= 0x01
        corruptions.append(bytes(mutated))
        corruptions.append(encoded[:-1])
        corruptions.append(encoded + b"trailing")
        for index, corrupted in enumerate(corruptions):
            with self.subTest(corruption=index):
                with self.assertRaises(ValueError):
                    read_binary(corrupted)

    def test_duplicate_override_and_curve_are_rejected(self) -> None:
        encoded = write_binary(default_properties())
        payload = bytearray(encoded[24:])

        # Fixed fields occupy 100 bytes. The override count is followed by
        # fixed-size <stat_id, value> records.
        first_override = 102
        second_override = first_override + 6
        payload[second_override:second_override + 2] = payload[first_override:first_override + 2]
        with self.assertRaisesRegex(ValueError, "duplicated"):
            read_binary(_replace_payload(encoded, payload))

        payload = bytearray(encoded[24:])
        override_count = struct.unpack_from("<H", payload, 100)[0]
        first_curve = 102 + override_count * 6 + 2
        second_curve = first_curve + 10  # Default curves are one-key constants.
        payload[second_curve:second_curve + 3] = payload[first_curve:first_curve + 3]
        with self.assertRaisesRegex(ValueError, "duplicated"):
            read_binary(_replace_payload(encoded, payload))

    def test_nonincreasing_curve_keys_are_rejected(self) -> None:
        properties = default_properties()
        properties.curves["base"]["weight_kg"] = Curve([
            CurveKey(0.0, 1000.0),
            CurveKey(1.0, 1200.0),
        ])
        encoded = write_binary(properties)
        payload = bytearray(encoded[24:])
        override_count = struct.unpack_from("<H", payload, 100)[0]
        first_curve = 102 + override_count * 6 + 2
        first_key = first_curve + 6
        second_key = first_key + 16
        payload[second_key:second_key + 4] = struct.pack("<f", 0.0)
        with self.assertRaisesRegex(ValueError, "strictly increasing"):
            read_binary(_replace_payload(encoded, payload))

    def test_native_and_python_schema_fingerprints_match(self) -> None:
        header = (REPO_ROOT / "src/car/car_properties.h").read_text(encoding="utf-8")
        match = re.search(
            r"MXT_CAR_PROPS_SCHEMA_FINGERPRINT\s*=\s*UINT64_C\(0x([0-9a-fA-F]+)\)",
            header,
        )
        self.assertIsNotNone(match)
        self.assertEqual(int(match.group(1), 16), SCHEMA_FINGERPRINT)


if __name__ == "__main__":
    unittest.main()
