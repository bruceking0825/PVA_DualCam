from __future__ import annotations

import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "src"))

from measurement_core import MeasurementValues  # noqa: E402
from measurement_core.plc import OpcUaGateway, PlcSettings  # noqa: E402


class PlcGatewayTest(unittest.TestCase):
    def test_plc_defaults_are_complete_and_use_fixed_nodes(self) -> None:
        settings = PlcSettings()

        self.assertEqual(settings.missing_fields(), [])
        self.assertEqual(settings.endpoint_url, "opc.tcp://192.168.0.1:4840")
        self.assertEqual(settings.com_node, 'ns=3;s="Camera_Data_Global"."com"')
        self.assertFalse(hasattr(settings, "reflector_elevation_node"))

    def test_com_counter_cycles_from_zero_to_one_hundred(self) -> None:
        gateway = OpcUaGateway(PlcSettings())

        values = [gateway._next_com_value() for _ in range(103)]

        self.assertEqual(values[0], 0)
        self.assertEqual(values[100], 100)
        self.assertEqual(values[101], 0)
        self.assertEqual(values[102], 1)

    def test_write_values_writes_diameter_and_com(self) -> None:
        fake_opcua = types.ModuleType("opcua")
        fake_opcua.ua = object()
        gateway = OpcUaGateway(PlcSettings())
        gateway.nodes = {
            "diameter": object(),
            "com": object(),
        }
        values = MeasurementValues(diameter_mm=2.0)
        writes: list[tuple[str, float]] = []

        def fake_write(node, value, _ua_module) -> None:
            name = next(key for key, item in gateway.nodes.items() if item is node)
            writes.append((name, value))

        with patch.dict(sys.modules, {"opcua": fake_opcua}), patch(
            "measurement_core.plc._write_float_value",
            side_effect=fake_write,
        ):
            gateway.write_values(values)

        self.assertEqual(
            writes,
            [
                ("diameter", 2.0),
                ("com", 0.0),
            ],
        )


if __name__ == "__main__":
    unittest.main()
