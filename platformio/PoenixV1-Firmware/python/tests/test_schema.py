from __future__ import annotations

import json
from pathlib import Path

import pytest

from phoenix_benchmark.schema import BenchmarkCommand, load_command_plan


@pytest.fixture()
def tmp_plan(tmp_path: Path) -> Path:
    payload = {
        "commands": [
            {
                "command": "channel_map",
                "parameters": {"sweeps": 25, "dwell_us": 100, "wiper_code": 180},
            },
            {"command": "unimplemented", "parameters": {"note": "placeholder"}},
        ]
    }
    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps(payload), encoding="utf-8")
    return plan_path


def test_load_command_plan_normalizes_channel_map(tmp_plan: Path) -> None:
    commands = load_command_plan(tmp_plan)

    assert len(commands) == 2
    first = commands[0]
    assert isinstance(first, BenchmarkCommand)
    assert first.name == "channel_map"
    assert first.parameters == {"sweeps": 25, "dwell_us": 100, "wiper_code": 180}

    # Unknown commands should round-trip their payload for future expansion
    second = commands[1]
    assert second.name == "unimplemented"
    assert second.parameters == {"note": "placeholder"}


def test_load_command_plan_rejects_invalid_entries(tmp_path: Path) -> None:
    plan_path = tmp_path / "bad.json"
    plan_path.write_text(
        json.dumps([{"command": "channel_map", "parameters": {"sweeps": 0}}]),
        encoding="utf-8",
    )

    with pytest.raises(ValueError):
        load_command_plan(plan_path)


def test_load_command_plan_rejects_invalid_wiper(tmp_path: Path) -> None:
    plan_path = tmp_path / "bad_wiper.json"
    plan_path.write_text(
        json.dumps(
            [
                {
                    "command": "channel_map",
                    "parameters": {"sweeps": 10, "wiper_code": 300},
                }
            ]
        ),
        encoding="utf-8",
    )

    with pytest.raises(ValueError):
        load_command_plan(plan_path)


def test_load_command_plan_normalizes_adc_speed(tmp_path: Path) -> None:
    plan_path = tmp_path / "adc_speed_plan.json"
    plan_path.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "adc_speed",
                        "parameters": {"duration_ms": 750},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    commands = load_command_plan(plan_path)

    assert len(commands) == 1
    adc_speed_command = commands[0]
    assert isinstance(adc_speed_command, BenchmarkCommand)
    assert adc_speed_command.name == "adc_speed"
    assert adc_speed_command.parameters == {
        "duration_ms": 750,
        "enable_blocking": True,
        "enable_irq": True,
    }


def test_load_command_plan_normalizes_osr_sweep(tmp_path: Path) -> None:
    plan_path = tmp_path / "osr_plan.json"
    plan_path.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "osr_sweep",
                        "parameters": {
                            "sweeps": 120,
                            "dwell_us": 250,
                            "wiper_code": 85,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    commands = load_command_plan(plan_path)

    assert len(commands) == 1
    osr_command = commands[0]
    assert isinstance(osr_command, BenchmarkCommand)
    assert osr_command.name == "osr_sweep"
    assert osr_command.parameters == {
        "sweeps": 120,
        "dwell_us": 250,
        "wiper_code": 85,
    }


def test_load_command_plan_rejects_invalid_osr_payload(tmp_path: Path) -> None:
    plan_path = tmp_path / "bad_osr.json"
    plan_path.write_text(
        json.dumps(
            [
                {
                    "command": "osr_sweep",
                    "parameters": {"sweeps": -5, "dwell_us": -1},
                }
            ]
        ),
        encoding="utf-8",
    )

    with pytest.raises(ValueError):
        load_command_plan(plan_path)


def test_osr_sweep_defaults_to_ten_sweeps(tmp_path: Path) -> None:
    plan_path = tmp_path / "osr_defaults.json"
    plan_path.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "osr_sweep",
                        "parameters": {"dwell_us": 250, "wiper_code": 42},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    commands = load_command_plan(plan_path)

    assert len(commands) == 1
    osr_command = commands[0]
    assert osr_command.parameters["sweeps"] == 10
