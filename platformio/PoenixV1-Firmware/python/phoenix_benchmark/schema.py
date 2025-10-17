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
    wiper_code: int | None = None

    def to_payload(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {"sweeps": self.sweeps}
        if self.dwell_us is not None:
            payload["dwell_us"] = self.dwell_us
        if self.wiper_code is not None:
            payload["wiper_code"] = self.wiper_code
        return payload


@dataclass(frozen=True)
class AdcSpeedCommand:
    """Requests a throughput benchmark for blocking and/or IRQ sampling."""

    duration_ms: int
    enable_blocking: bool = True
    enable_irq: bool = True

    def to_payload(self) -> Dict[str, Any]:
        return {
            "duration_ms": self.duration_ms,
            "enable_blocking": self.enable_blocking,
            "enable_irq": self.enable_irq,
        }


@dataclass(frozen=True)
class OsrSweepCommand:
    """Requests an oversampling ratio sweep with shared dwell/pot settings."""

    sweeps: int
    dwell_us: int | None = None
    wiper_code: int | None = None

    def to_payload(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {"sweeps": self.sweeps}
        if self.dwell_us is not None:
            payload["dwell_us"] = self.dwell_us
        if self.wiper_code is not None:
            payload["wiper_code"] = self.wiper_code
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
        wiper = parameters.get("wiper_code")
        if wiper is not None:
            if not isinstance(wiper, int) or not (0 <= wiper <= 0xFF):
                raise ValueError(
                    "channel_map.wiper_code must be an integer between 0 and 255"
                )
        command = ChannelMapCommand(sweeps=sweeps, dwell_us=dwell, wiper_code=wiper)
        return BenchmarkCommand(name=name, parameters=command.to_payload())

    if name == "osr_sweep":
        sweeps = parameters.get("sweeps", 10)
        if not isinstance(sweeps, int) or sweeps <= 0:
            raise ValueError("osr_sweep.sweeps must be a positive integer")
        dwell = parameters.get("dwell_us")
        if dwell is not None and (not isinstance(dwell, int) or dwell < 0):
            raise ValueError(
                "osr_sweep.dwell_us must be a non-negative integer when provided"
            )
        wiper = parameters.get("wiper_code")
        if wiper is not None:
            if not isinstance(wiper, int) or not (0 <= wiper <= 0xFF):
                raise ValueError(
                    "osr_sweep.wiper_code must be an integer between 0 and 255"
                )
        command = OsrSweepCommand(sweeps=sweeps, dwell_us=dwell, wiper_code=wiper)
        return BenchmarkCommand(name=name, parameters=command.to_payload())

    if name == "adc_speed":
        duration = parameters.get("duration_ms")
        if not isinstance(duration, int) or duration <= 0:
            raise ValueError("adc_speed.duration_ms must be a positive integer")

        enable_blocking = parameters.get("enable_blocking", True)
        enable_irq = parameters.get("enable_irq", True)
        if not isinstance(enable_blocking, bool) or not isinstance(enable_irq, bool):
            raise ValueError("adc_speed enable flags must be boolean values")

        command = AdcSpeedCommand(
            duration_ms=duration,
            enable_blocking=enable_blocking,
            enable_irq=enable_irq,
        )
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


__all__ = [
    "AdcSpeedCommand",
    "BenchmarkCommand",
    "ChannelMapCommand",
    "OsrSweepCommand",
    "load_command_plan",
]
