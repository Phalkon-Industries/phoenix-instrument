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
                "parameters": {"sweeps": 25},
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
    assert first.parameters == {"sweeps": 25}

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


def test_load_command_plan_rejects_channel_map_wiper_override(tmp_path: Path) -> None:
    plan_path = tmp_path / "bad_wiper.json"
    plan_path.write_text(
        json.dumps(
            [
                {
                    "command": "channel_map",
                    "parameters": {"sweeps": 10, "wiper_code": 150},
                }
            ]
        ),
        encoding="utf-8",
    )

    with pytest.raises(ValueError):
        load_command_plan(plan_path)


def test_load_command_plan_rejects_channel_map_dwell_override(tmp_path: Path) -> None:
    plan_path = tmp_path / "bad_dwell.json"
    plan_path.write_text(
        json.dumps(
            [
                {
                    "command": "channel_map",
                    "parameters": {"sweeps": 10, "dwell_us": 500},
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


def test_load_command_plan_normalizes_pot_sweep_defaults(tmp_path: Path) -> None:
    plan_path = tmp_path / "pot_sweep_defaults.json"
    plan_path.write_text(
        json.dumps({"commands": [{"command": "pot_sweep"}]}),
        encoding="utf-8",
    )

    commands = load_command_plan(plan_path)

    assert len(commands) == 1
    pot_command = commands[0]
    assert pot_command.name == "pot_sweep"
    assert pot_command.parameters == {"sweeps": 5}


def test_load_command_plan_accepts_custom_pot_sweep(tmp_path: Path) -> None:
    plan_path = tmp_path / "pot_sweep_custom.json"
    plan_path.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "pot_sweep",
                        "parameters": {"sweeps": 3, "dwell_us": 400},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    commands = load_command_plan(plan_path)

    assert len(commands) == 1
    pot_command = commands[0]
    assert pot_command.parameters == {"sweeps": 3, "dwell_us": 400}


@pytest.mark.parametrize(
    "payload",
    [
        {"command": "pot_sweep", "parameters": {"sweeps": 0}},
        {"command": "pot_sweep", "parameters": {"wipers": []}},
        {"command": "pot_sweep", "parameters": {"wipers": [0, 1]}},
        {"command": "pot_sweep", "parameters": {"wiper_start": 0}},
        {"command": "pot_sweep", "parameters": {"wiper_end": 32}},
        {"command": "pot_sweep", "parameters": {"wiper_step": 2}},
        {"command": "pot_sweep", "parameters": {"dwell_us": -1}},
    ],
)
def test_load_command_plan_rejects_invalid_pot_sweep(
    payload: dict[str, object], tmp_path: Path
) -> None:
    plan_path = tmp_path / "pot_sweep_invalid.json"
    plan_path.write_text(json.dumps([payload]), encoding="utf-8")

    with pytest.raises(ValueError):
        load_command_plan(plan_path)


def test_load_command_plan_normalizes_dwell_sweep(tmp_path: Path) -> None:
    plan_path = tmp_path / "dwell_sweep.json"
    plan_path.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "dwell_sweep",
                        "parameters": {
                            "sweeps_per_dwell": 4,
                            "start_dwell_us": 100,
                            "end_dwell_us": 500,
                            "dwell_step_us": 100,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    commands = load_command_plan(plan_path)

    assert len(commands) == 1
    dwell_command = commands[0]
    assert dwell_command.name == "dwell_sweep"
    assert dwell_command.parameters == {
        "sweeps_per_dwell": 4,
        "start_dwell_us": 100,
        "end_dwell_us": 500,
        "dwell_step_us": 100,
    }


@pytest.mark.parametrize(
    "payload",
    [
        {"command": "dwell_sweep", "parameters": {"sweeps_per_dwell": 0}},
        {
            "command": "dwell_sweep",
            "parameters": {
                "sweeps_per_dwell": 1,
                "start_dwell_us": -1,
                "end_dwell_us": 100,
                "dwell_step_us": 1,
            },
        },
        {
            "command": "dwell_sweep",
            "parameters": {
                "sweeps_per_dwell": 1,
                "start_dwell_us": 0,
                "end_dwell_us": 10,
                "dwell_step_us": 0,
            },
        },
        {
            "command": "dwell_sweep",
            "parameters": {
                "sweeps_per_dwell": 1,
                "start_dwell_us": 200,
                "end_dwell_us": 100,
                "dwell_step_us": 10,
            },
        },
        {
            "command": "dwell_sweep",
            "parameters": {
                "sweeps_per_dwell": 1,
                "start_dwell_us": 0,
                "end_dwell_us": 5_000_001,
                "dwell_step_us": 1,
            },
        },
        {
            "command": "dwell_sweep",
            "parameters": {
                "sweeps_per_dwell": 1_500,
                "start_dwell_us": 0,
                "end_dwell_us": 100,
                "dwell_step_us": 10,
            },
        },
        {
            "command": "dwell_sweep",
            "parameters": {
                "sweeps_per_dwell": 1,
                "start_dwell_us": 0,
                "end_dwell_us": 13_000,
                "dwell_step_us": 50,
            },
        },
        {
            "command": "dwell_sweep",
            "parameters": {
                "sweeps_per_dwell": 1,
                "start_dwell_us": 0,
                "end_dwell_us": 100,
                "dwell_step_us": 10,
                "unexpected": True,
            },
        },
    ],
)
def test_load_command_plan_rejects_invalid_dwell_sweep(
    payload: dict[str, object], tmp_path: Path
) -> None:
    plan_path = tmp_path / "dwell_sweep_invalid.json"
    plan_path.write_text(json.dumps([payload]), encoding="utf-8")

    with pytest.raises(ValueError):
        load_command_plan(plan_path)


def test_load_command_plan_accepts_drift_capture(tmp_path: Path) -> None:
    plan_path = tmp_path / "drift_capture.json"
    plan_path.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "drift_capture",
                        "parameters": {
                            "start_time_us": 100,
                            "end_time_us": 10_000,
                            "step_delay_us": 25,
                            "osr": 4096,
                            "wiper_code": 170,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    commands = load_command_plan(plan_path)

    assert len(commands) == 1
    drift_command = commands[0]
    assert drift_command.name == "drift_capture"
    assert drift_command.parameters == {
        "start_time_us": 100,
        "end_time_us": 10_000,
        "step_delay_us": 25,
        "osr": 4096,
        "wiper_code": 170,
    }


@pytest.mark.parametrize(
    "payload",
    [
        {
            "command": "drift_capture",
            "parameters": {"start_time_us": 5_000, "end_time_us": 1_000},
        },
        {
            "command": "drift_capture",
            "parameters": {
                "start_time_us": 0,
                "end_time_us": 100_000,
                "step_delay_us": -10,
            },
        },
        {
            "command": "drift_capture",
            "parameters": {
                "start_time_us": 0,
                "end_time_us": 100_000,
                "step_delay_us": 10,
            },
        },
        {
            "command": "drift_capture",
            "parameters": {"start_time_us": -1, "end_time_us": 10_000},
        },
        {
            "command": "drift_capture",
            "parameters": {
                "start_time_us": 0,
                "end_time_us": 10_000,
                "osr": 123,
            },
        },
        {
            "command": "drift_capture",
            "parameters": {"wiper_code": 300},
        },
    ],
)
def test_load_command_plan_rejects_invalid_drift_capture(
    payload: dict[str, object], tmp_path: Path
) -> None:
    plan_path = tmp_path / "drift_capture_invalid.json"
    plan_path.write_text(json.dumps([payload]), encoding="utf-8")

    with pytest.raises(ValueError):
        load_command_plan(plan_path)
