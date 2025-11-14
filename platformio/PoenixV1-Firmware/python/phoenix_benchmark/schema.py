"""Typed representations for Phoenix benchmark serial commands."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, Iterable, List


DWELL_MIN_US = 0
DWELL_MAX_US = 5_000_000
DWELL_MAX_STEP_COUNT = 128
DWELL_MAX_SWEEPS_PER_DWELL = 1_000

DRIFT_CAPTURE_MAX_SAMPLES = 2_048
DRIFT_CAPTURE_DEFAULT_START_US = 0
DRIFT_CAPTURE_DEFAULT_END_US = 100_000
DRIFT_CAPTURE_DEFAULT_STEP_US = 0
DRIFT_CAPTURE_ALLOWED_OSR_VALUES = {
    32,
    64,
    128,
    256,
    512,
    1_024,
    2_048,
    4_096,
    8_192,
    16_384,
    20_480,
    24_576,
    40_960,
    49_152,
    81_920,
    98_304,
}


@dataclass(frozen=True)
class ChannelMapCommand:
    """Requests a channel-mapping sweep using device-configured defaults."""

    sweeps: int

    def to_payload(self) -> Dict[str, Any]:
        return {"sweeps": self.sweeps}


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
class PotSweepCommand:
    """Requests a potentiometer sweep across the full wiper range."""

    sweeps_per_wiper: int
    dwell_us: int | None = None

    def to_payload(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {"sweeps": self.sweeps_per_wiper}
        if self.dwell_us is not None:
            payload["dwell_us"] = self.dwell_us
        return payload


@dataclass(frozen=True)
class DwellSweepCommand:
    """Requests a dwell sweep across a linear series of LED settle times."""

    sweeps_per_dwell: int
    start_dwell_us: int
    end_dwell_us: int
    dwell_step_us: int

    def to_payload(self) -> Dict[str, Any]:
        return {
            "sweeps_per_dwell": self.sweeps_per_dwell,
            "start_dwell_us": self.start_dwell_us,
            "end_dwell_us": self.end_dwell_us,
            "dwell_step_us": self.dwell_step_us,
        }


@dataclass(frozen=True)
class DriftCaptureCommand:
    """Requests a fast LED drift capture with optional overrides."""

    start_time_us: int | None = None
    end_time_us: int | None = None
    step_delay_us: int | None = None
    osr: int | None = None
    wiper_blue_code: int | None = None
    wiper_green_code: int | None = None

    def to_payload(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {}
        if self.start_time_us is not None:
            payload["start_time_us"] = self.start_time_us
        if self.end_time_us is not None:
            payload["end_time_us"] = self.end_time_us
        if self.step_delay_us is not None:
            payload["step_delay_us"] = self.step_delay_us
        if self.osr is not None:
            payload["osr"] = self.osr
        if self.wiper_blue_code is not None:
            payload["wiper_blue_code"] = self.wiper_blue_code
        if self.wiper_green_code is not None:
            payload["wiper_green_code"] = self.wiper_green_code
        return payload


@dataclass(frozen=True)
class ColdSweepCommand:
    """Requests a cold-start sweep using firmware defaults."""

    def to_payload(self) -> Dict[str, Any]:
        return {}


@dataclass(frozen=True)
class OsrLatencyCommand:
    """Requests an OSR latency capture with optional overrides."""

    warmup_count: int | None = None
    sample_count: int | None = None
    include_blocking: bool | None = None
    include_irq: bool | None = None

    def to_payload(self) -> Dict[str, Any]:
        payload: Dict[str, Any] = {}
        if self.warmup_count is not None:
            payload["warmup_count"] = self.warmup_count
        if self.sample_count is not None:
            payload["sample_count"] = self.sample_count
        if self.include_blocking is not None:
            payload["include_blocking"] = self.include_blocking
        if self.include_irq is not None:
            payload["include_irq"] = self.include_irq
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


def _compute_dwell_step_count(start_us: int, end_us: int, step_us: int) -> int:
    if step_us <= 0 or start_us > end_us:
        return 0
    if start_us == end_us:
        return 1
    span = end_us - start_us
    increments = span // step_us
    return increments + 1


def _build_command(entry: Dict[str, Any]) -> BenchmarkCommand:
    name = entry.get("command")
    if not isinstance(name, str) or not name:
        raise ValueError("Command entries must include a non-empty 'command' field")

    parameters = entry.get("parameters", {})
    if not isinstance(parameters, dict):
        raise ValueError("Command parameters must be a JSON object")

    if name == "channel_map":
        allowed_keys = {"sweeps"}
        extra_keys = set(parameters.keys()) - allowed_keys
        if extra_keys:
            unexpected = ", ".join(sorted(extra_keys))
            raise ValueError(
                f"channel_map received unsupported parameters: {unexpected}"
            )

        sweeps = parameters.get("sweeps", 0)
        if not isinstance(sweeps, int) or sweeps <= 0:
            raise ValueError("channel_map.sweeps must be a positive integer")

        command = ChannelMapCommand(sweeps=sweeps)
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

    if name == "pot_sweep":
        sweeps = parameters.get("sweeps", 5)
        if not isinstance(sweeps, int) or sweeps <= 0:
            raise ValueError("pot_sweep.sweeps must be a positive integer")

        dwell = parameters.get("dwell_us")
        if dwell is not None:
            if not isinstance(dwell, int) or dwell < 0:
                raise ValueError(
                    "pot_sweep.dwell_us must be a non-negative integer when provided"
                )

        extra_keys = set(parameters.keys()) - {"sweeps", "dwell_us"}
        if extra_keys:
            unexpected = ", ".join(sorted(extra_keys))
            raise ValueError(f"pot_sweep received unsupported parameters: {unexpected}")

        command = PotSweepCommand(sweeps_per_wiper=sweeps, dwell_us=dwell)
        return BenchmarkCommand(name=name, parameters=command.to_payload())

    if name == "dwell_sweep":
        required_keys = {
            "sweeps_per_dwell",
            "start_dwell_us",
            "end_dwell_us",
            "dwell_step_us",
        }
        extra_keys = set(parameters.keys()) - required_keys
        if extra_keys:
            unexpected = ", ".join(sorted(extra_keys))
            raise ValueError(
                f"dwell_sweep received unsupported parameters: {unexpected}"
            )

        missing = [key for key in required_keys if key not in parameters]
        if missing:
            raise ValueError(
                "dwell_sweep parameters must include sweeps_per_dwell, start_dwell_us, "
                "end_dwell_us, and dwell_step_us"
            )

        sweeps_per_dwell = parameters.get("sweeps_per_dwell")
        if (
            not isinstance(sweeps_per_dwell, int)
            or sweeps_per_dwell <= 0
            or sweeps_per_dwell > DWELL_MAX_SWEEPS_PER_DWELL
        ):
            raise ValueError(
                "dwell_sweep.sweeps_per_dwell must be an integer between 1 and 1000"
            )

        start_us = parameters.get("start_dwell_us")
        end_us = parameters.get("end_dwell_us")
        step_us = parameters.get("dwell_step_us")

        if (
            not isinstance(start_us, int)
            or not isinstance(end_us, int)
            or not isinstance(step_us, int)
        ):
            raise ValueError("dwell_sweep dwell parameters must be integers")

        if start_us < DWELL_MIN_US or end_us < DWELL_MIN_US:
            raise ValueError("dwell_sweep dwell values must be non-negative")
        if start_us > DWELL_MAX_US or end_us > DWELL_MAX_US:
            raise ValueError(
                f"dwell_sweep dwell values must not exceed {DWELL_MAX_US} microseconds"
            )
        if step_us <= 0:
            raise ValueError("dwell_sweep.dwell_step_us must be a positive integer")
        if start_us > end_us:
            raise ValueError(
                "dwell_sweep start_dwell_us must be less than or equal to end_dwell_us"
            )

        step_count = _compute_dwell_step_count(start_us, end_us, step_us)
        if step_count == 0:
            raise ValueError("dwell_sweep configuration produced zero dwell steps")
        if step_count > DWELL_MAX_STEP_COUNT:
            raise ValueError(
                f"dwell_sweep schedule exceeds maximum supported steps ({DWELL_MAX_STEP_COUNT})"
            )

        command = DwellSweepCommand(
            sweeps_per_dwell=sweeps_per_dwell,
            start_dwell_us=start_us,
            end_dwell_us=end_us,
            dwell_step_us=step_us,
        )
        return BenchmarkCommand(name=name, parameters=command.to_payload())

    if name == "drift_capture":
        allowed_keys = {
            "start_time_us",
            "end_time_us",
            "step_delay_us",
            "osr",
            "wiper_code",
            "wiper_blue_code",
            "wiper_green_code",
        }
        extra_keys = set(parameters.keys()) - allowed_keys
        if extra_keys:
            unexpected = ", ".join(sorted(extra_keys))
            raise ValueError(
                f"drift_capture received unsupported parameters: {unexpected}"
            )

        def _normalise_microseconds(value: Any, field_name: str) -> int | None:
            if value is None:
                return None
            if not isinstance(value, int):
                raise ValueError(f"{field_name} must be an integer")
            if value < 0:
                raise ValueError(f"{field_name} must be non-negative")
            return value

        start_us = _normalise_microseconds(
            parameters.get("start_time_us"), "drift_capture.start_time_us"
        )
        end_us = _normalise_microseconds(
            parameters.get("end_time_us"), "drift_capture.end_time_us"
        )
        step_us = parameters.get("step_delay_us")
        if step_us is not None:
            if not isinstance(step_us, int):
                raise ValueError("drift_capture.step_delay_us must be an integer")
            if step_us < 0:
                raise ValueError("drift_capture.step_delay_us must be non-negative")

        effective_start = (
            start_us if start_us is not None else DRIFT_CAPTURE_DEFAULT_START_US
        )
        effective_end = end_us if end_us is not None else DRIFT_CAPTURE_DEFAULT_END_US
        if end_us is not None and start_us is not None and end_us < start_us:
            raise ValueError("drift_capture.end_time_us must be >= start_time_us")
        if effective_end < effective_start:
            raise ValueError("drift_capture.end_time_us must be >= start_time_us")

        if step_us is not None and step_us > 0:
            span = effective_end - effective_start
            sample_count = (span // step_us) + 1
            if sample_count > DRIFT_CAPTURE_MAX_SAMPLES:
                raise ValueError(
                    "drift_capture capture window would exceed buffer capacity"
                )

        osr_value = parameters.get("osr")
        if osr_value is not None:
            if not isinstance(osr_value, int):
                raise ValueError("drift_capture.osr must be an integer")
            if osr_value not in DRIFT_CAPTURE_ALLOWED_OSR_VALUES:
                raise ValueError("drift_capture.osr is not supported")

        wiper_blue = parameters.get("wiper_blue_code")
        wiper_green = parameters.get("wiper_green_code")
        legacy_wiper = parameters.get("wiper_code")

        def _validate_wiper(value: Any, field_name: str) -> int:
            if not isinstance(value, int):
                raise ValueError(f"{field_name} must be an integer")
            if not (0 <= value <= 0xFF):
                raise ValueError(f"{field_name} must be an integer between 0 and 255")
            return value

        if legacy_wiper is not None:
            if wiper_blue is not None or wiper_green is not None:
                raise ValueError(
                    "drift_capture.wiper_code cannot be combined with per-colour wiper overrides"
                )
            override_value = _validate_wiper(legacy_wiper, "drift_capture.wiper_code")
            wiper_blue = override_value
            wiper_green = override_value
        if wiper_blue is not None:
            wiper_blue = _validate_wiper(wiper_blue, "drift_capture.wiper_blue_code")
        if wiper_green is not None:
            wiper_green = _validate_wiper(wiper_green, "drift_capture.wiper_green_code")

        command = DriftCaptureCommand(
            start_time_us=start_us,
            end_time_us=end_us,
            step_delay_us=step_us,
            osr=osr_value,
            wiper_blue_code=wiper_blue,
            wiper_green_code=wiper_green,
        )
        return BenchmarkCommand(name=name, parameters=command.to_payload())

    if name == "cold_sweep":
        if parameters:
            raise ValueError("cold_sweep does not accept parameters")
        command = ColdSweepCommand()
        return BenchmarkCommand(name=name, parameters=command.to_payload())

    if name == "osr_latency":
        allowed_keys = {
            "warmup_count",
            "sample_count",
            "include_blocking",
            "include_irq",
        }
        extra_keys = set(parameters.keys()) - allowed_keys
        if extra_keys:
            unexpected = ", ".join(sorted(extra_keys))
            raise ValueError(
                f"osr_latency received unsupported parameters: {unexpected}"
            )

        warmup_value = parameters.get("warmup_count")
        if warmup_value is not None:
            if not isinstance(warmup_value, int):
                raise ValueError("osr_latency.warmup_count must be an integer")
            if warmup_value < 0:
                raise ValueError("osr_latency.warmup_count must be non-negative")

        sample_value = parameters.get("sample_count")
        if sample_value is not None:
            if not isinstance(sample_value, int):
                raise ValueError("osr_latency.sample_count must be an integer")
            if sample_value <= 0:
                raise ValueError("osr_latency.sample_count must be a positive integer")

        include_blocking = parameters.get("include_blocking")
        if include_blocking is not None and not isinstance(include_blocking, bool):
            raise ValueError("osr_latency.include_blocking must be a boolean")

        include_irq = parameters.get("include_irq")
        if include_irq is not None and not isinstance(include_irq, bool):
            raise ValueError("osr_latency.include_irq must be a boolean")

        if (include_blocking is not None and include_irq is not None) and (
            not include_blocking and not include_irq
        ):
            raise ValueError(
                "osr_latency requires at least one sampling mode to be enabled"
            )

        command = OsrLatencyCommand(
            warmup_count=warmup_value,
            sample_count=sample_value,
            include_blocking=include_blocking,
            include_irq=include_irq,
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
    "ColdSweepCommand",
    "OsrLatencyCommand",
    "DRIFT_CAPTURE_ALLOWED_OSR_VALUES",
    "DRIFT_CAPTURE_DEFAULT_END_US",
    "DRIFT_CAPTURE_DEFAULT_START_US",
    "DRIFT_CAPTURE_DEFAULT_STEP_US",
    "DRIFT_CAPTURE_MAX_SAMPLES",
    "DwellSweepCommand",
    "DriftCaptureCommand",
    "PotSweepCommand",
    "OsrSweepCommand",
    "load_command_plan",
]
