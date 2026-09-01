from __future__ import annotations

import json
import os
from pathlib import Path

from .models import MeasurementState


STATE_SCHEMA_VERSION = 3


class StateStore:
    def __init__(self, path: str | Path):
        self.path = Path(path)

    def load(self) -> MeasurementState:
        if not self.path.exists():
            return MeasurementState()
        payload = json.loads(self.path.read_text(encoding="utf-8"))
        if payload.get("schema_version") != STATE_SCHEMA_VERSION:
            return MeasurementState()
        return MeasurementState.from_dict(payload.get("state", {}))

    def save(self, state: MeasurementState) -> None:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "schema_version": STATE_SCHEMA_VERSION,
            "state": state.to_dict(),
        }
        temp_path = self.path.with_suffix(self.path.suffix + ".tmp")
        temp_path.write_text(json.dumps(payload, indent=2, sort_keys=True), encoding="utf-8")
        os.replace(temp_path, self.path)
