from __future__ import annotations

import sys
import unittest
from pathlib import Path

from blender_stubs import install


install()
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from mxt_track_editor.curve_matrix import _cubic
from mxt_track_editor.foundation import (
    _cubic_bezier_derivative,
    _cubic_bezier_value,
    _remap,
)


class GeometryHelperTests(unittest.TestCase):
    def test_cubic_endpoints_and_midpoint(self) -> None:
        self.assertEqual(_cubic(2.0, 4.0, 8.0, 10.0, 0.0), 2.0)
        self.assertEqual(_cubic(2.0, 4.0, 8.0, 10.0, 1.0), 10.0)
        self.assertEqual(_cubic(0.0, 1.0, 2.0, 3.0, 0.5), 1.5)

    def test_bezier_value_and_derivative(self) -> None:
        self.assertEqual(_cubic_bezier_value(0.0, 1.0, 2.0, 3.0, 0.5), 1.5)
        self.assertEqual(_cubic_bezier_derivative(0.0, 1.0, 2.0, 3.0, 0.0), 3.0)

    def test_remap(self) -> None:
        self.assertAlmostEqual(_remap(0.25, 0.0, 1.0, -1.0, 1.0), -0.5)


if __name__ == "__main__":
    unittest.main()
