"""Typed representations for Phoenix benchmark serial commands."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List


@dataclass(frozen=True)
class ChannelMapCommand:
    """Requests a channel-mapping sweep with optional dwell override."""

    sweeps: int
    dwell_us: int | None = None

    def to_payload(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {"sweeps": self.sweeps}
        if self.dwell_us is not None:
            payload["dwell_us"] = self.dwell_us
        return payload


@dataclass(frozen=True)
class BenchmarkCommand:
    """Envelope describing a single host-to-firmware request."""

    name: str
    parameters: Dict[str, Any]

    def to_serial_line(self) -> str:
        """Render the command as a newline-delimited JSON message."""
        message = {"command": self.name, "parameters": self.parameters}
        return json.dumps(message, separators=(",", ":")) + "\n"


def _build_command(entry: Dict[str, Any]) -> BenchmarkCommand:
    name = entry.get("command")
    if not isinstance(name, str) or not name:
        raise ValueError("Command entries must include a non-empty 'command' field")

    parameters = entry.get("parameters", {})
    if not isinstance(parameters, dict):
        raise ValueError("Command parameters must be a JSON object")

    if name == "channel_map":
        sweeps = parameters.get("sweeps", 0)
        if not isinstance(sweeps, int) or sweeps <= 0:
            raise ValueError("channel_map.sweeps must be a positive integer")
        dwell = parameters.get("dwell_us")
        if dwell is not None and (not isinstance(dwell, int) or dwell < 0):
            raise ValueError(
                "channel_map.dwell_us must be a non-negative integer when provided"
            )
        command = ChannelMapCommand(sweeps=sweeps, dwell_us=dwell)
        return BenchmarkCommand(name=name, parameters=command.to_payload())

    # Unknown commands pass-through for future phases
    return BenchmarkCommand(name=name, parameters=parameters)


def load_command_plan(path: Path | str) -> List[BenchmarkCommand]:
    """Load a JSON benchmark plan into typed command envelopes."""
    plan_path = Path(path)
    if not plan_path.exists():
        raise FileNotFoundError(plan_path)

    raw = plan_path.read_text(encoding="utf-8")
    data = json.loads(raw)

    if isinstance(data, dict):
        entries = data.get("commands") or data.get("sequence")
    else:
        entries = data

    if not isinstance(entries, Iterable):
        raise ValueError(
            "Plan must be a list of command entries or contain a 'commands'/'sequence' array"
        )

    commands: List[BenchmarkCommand] = []
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("Command plan entries must be objects")
        commands.append(_build_command(entry))
    return commands


__all__ = ["BenchmarkCommand", "ChannelMapCommand", "load_command_plan"]
