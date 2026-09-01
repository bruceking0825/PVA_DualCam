from __future__ import annotations

from dataclasses import dataclass
import re

from .models import MeasurementValues


@dataclass(frozen=True)
class PlcControls:
    stage_value: int
    shoulder_transition: bool


@dataclass
class PlcSettings:
    endpoint_url: str = "opc.tcp://192.168.0.1:4840"
    mode_node: str = 'ns=3;s="Camera_Data_Global"."V285_Melt_Level_Mode_From_Camera"'
    shoulder_transition_node: str = 'ns=3;s="Camera_Data_Global"."shoulderMode"'
    diameter_node: str = 'ns=3;s="Camera_Data_Global"."diameter"'
    com_node: str = 'ns=3;s="Camera_Data_Global"."com"'
    reconnect_ms: int = 2000

    def missing_fields(self) -> list[str]:
        required = (
            "endpoint_url",
            "mode_node",
            "shoulder_transition_node",
            "diameter_node",
            "com_node",
        )
        return [name for name in required if not getattr(self, name)]


class OpcUaGateway:
    def __init__(self, settings: PlcSettings):
        self.settings = settings
        self.client = None
        self.nodes: dict[str, object] = {}
        self._com_value = -1

    def connect(self) -> None:
        missing = self.settings.missing_fields()
        if missing:
            raise ValueError(f"missing PLC settings: {', '.join(missing)}")
        try:
            from opcua import Client
        except ImportError as exc:
            raise RuntimeError("opcua package is not installed") from exc
        timeout_s = max(float(self.settings.reconnect_ms) / 1000.0, 1.0)
        self.client = Client(self.settings.endpoint_url, timeout=timeout_s)
        self.client.connect()
        self.nodes = {
            "mode": self._get_node(self.settings.mode_node),
            "transition": self._get_node(self.settings.shoulder_transition_node),
            "diameter": self._get_node(self.settings.diameter_node),
            "com": self._get_node(self.settings.com_node),
        }

    def _get_node(self, node_id: str):
        if self.client is None:
            raise RuntimeError("PLC client is not connected")
        return self.client.get_node(_normalize_node_id(self.client, node_id))

    def disconnect(self) -> None:
        client, self.client = self.client, None
        self.nodes = {}
        if client is not None:
            try:
                client.disconnect()
            except Exception:
                pass

    def read_controls(self) -> PlcControls:
        if not self.nodes:
            raise RuntimeError("PLC is not connected")
        return PlcControls(
            stage_value=_to_int(self.nodes["mode"].get_value()),
            shoulder_transition=_to_bool(self.nodes["transition"].get_value()),
        )

    def write_values(self, values: MeasurementValues) -> None:
        if not values.complete():
            raise ValueError("measurement values are incomplete")
        if not self.nodes:
            raise RuntimeError("PLC is not connected")
        try:
            from opcua import ua
        except ImportError as exc:
            raise RuntimeError("opcua package is not installed") from exc
        output = (
            ("diameter", values.diameter_mm),
            ("com", self._next_com_value()),
        )
        for name, value in output:
            _write_float_value(self.nodes[name], float(value), ua)

    def _next_com_value(self) -> int:
        self._com_value = (self._com_value + 1) % 101
        return self._com_value


def _normalize_node_id(client, node_id: str) -> str:
    text = str(node_id).strip()
    if text.lower().startswith(("ns=", "i=", "s=", "g=", "b=")):
        return text
    if text.lower().startswith("nsu="):
        uri, identifier = _split_uri_node_id(text[4:])
        return f"ns={_namespace_index(client, uri)};{identifier}"
    if text.startswith("http://") or text.startswith("https://") or text.startswith("urn:"):
        uri, identifier = _split_uri_node_id(text)
        return f"ns={_namespace_index(client, uri)};{identifier}"
    return text


def _split_uri_node_id(text: str) -> tuple[str, str]:
    if ";" not in text:
        raise ValueError(f"OPC UA NodeId with namespace URI must contain ';': {text}")
    uri, identifier = text.rsplit(";", 1)
    if not re.match(r"^[isgb]=.+", identifier, re.IGNORECASE):
        raise ValueError(f"OPC UA NodeId identifier must start with i=, s=, g=, or b=: {text}")
    return uri, identifier


def _namespace_index(client, uri: str) -> int:
    namespaces = client.get_namespace_array()
    for index, namespace_uri in enumerate(namespaces):
        if namespace_uri == uri:
            return index
    raise ValueError(f"OPC UA namespace URI not found: {uri}; available={namespaces}")


def _to_int(value: object) -> int:
    return int(float(value))


def _to_bool(value: object) -> bool:
    if isinstance(value, str):
        return bool(float(value))
    return bool(value)


def _write_float_value(node, value: float, ua_module) -> None:
    # 博中 OPC UA Server 不接受带 SourceTimestamp 的写入；只写 Value 本身。
    data_value = ua_module.DataValue()
    data_value.Value = ua_module.Variant(value, ua_module.VariantType.Float)
    node.set_attribute(ua_module.AttributeIds.Value, data_value)
