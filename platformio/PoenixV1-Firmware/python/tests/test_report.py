from __future__ import annotations

import json
from pathlib import Path
from typing import List

import pytest  # type: ignore

from phoenix_benchmark.report import ReportArtifacts, create_report, parse_summary_table


def _format_row(
    label: str,
    samples: int,
    mean_a: float,
    std_a: float,
    min_a: float,
    max_a: float,
    mean_b: float,
    std_b: float,
    min_b: float,
    max_b: float,
    step_mean: float,
    step_std: float,
    step_range: float,
    alignment: str,
) -> str:
    return (
        f"{label:<8}"
        f"{samples:>9}"
        f"{mean_a:>12.3f}"
        f"{std_a:>12.3f}"
        f"{min_a:>12.3f}"
        f"{max_a:>12.3f}"
        f"{mean_b:>12.3f}"
        f"{std_b:>12.3f}"
        f"{min_b:>12.3f}"
        f"{max_b:>12.3f}"
        f"{step_mean:>15.1f}"
        f"{step_std:>15.1f}"
        f"{step_range:>15.1f}"
        f"{alignment:<12}"
    ).rstrip()


def _format_placeholder_row(
    label: str, samples: int, step_mean: float, step_std: float, step_range: float
) -> str:
    placeholder = "--".ljust(12)
    parts = [
        f"{label:<8}",
        f"{samples:>9}",
    ]
    parts.extend([placeholder] * 8)
    parts.extend(
        [
            f"{step_mean:>15.1f}",
            f"{step_std:>15.1f}",
            f"{step_range:>15.1f}",
            f"{'--':<12}",
        ]
    )
    return "".join(parts).rstrip()


def _sample_summary_lines() -> List[str]:
    header = (
        "State      Samples      Mean_A      Std_A      Min_A      Max_A      Mean_B      Std_B      Min_B      Max_B"
        "   Step_us_mean    Step_us_std  Step_us_range  Channel_Map"
    )
    led1 = _format_row(
        "LED1",
        10,
        123.0,
        2.0,
        120.0,
        130.0,
        321.0,
        3.0,
        310.0,
        330.0,
        5.0,
        0.5,
        1.0,
        "A=OK",
    )
    led2 = _format_row(
        "LED2",
        8,
        223.0,
        4.0,
        210.0,
        235.0,
        121.0,
        2.5,
        118.0,
        125.0,
        5.2,
        0.7,
        1.4,
        "B!=A",
    )
    cycle = _format_placeholder_row("Cycle", 18, 10.2, 0.3, 1.1)
    return [
        "# phoenix benchmark ready",
        "# summary_table",
        header,
        led1,
        led2,
        cycle,
        "",
        "# benchmark_complete",
        "# ready",
    ]


def test_parse_summary_table_extracts_rows() -> None:
    rows = parse_summary_table(_sample_summary_lines())

    assert len(rows) == 3

    first = rows[0]
    assert first.label == "LED1"
    assert first.sample_count == 10
    assert pytest.approx(first.mean_channel_a, rel=1e-6) == 123.0
    assert first.channel_alignment == "A=OK"
    assert first.has_channel_metrics is True

    third = rows[2]
    assert third.label == "Cycle"
    assert third.has_channel_metrics is False


def test_create_report_generates_artifacts(tmp_path: Path) -> None:
    lines = _sample_summary_lines()

    plan_path = tmp_path / "plan.json"
    plan_path.write_text(json.dumps({"commands": []}), encoding="utf-8")

    output_dir = tmp_path / "report"
    artifacts = create_report(lines, plan_path, output_dir)

    assert isinstance(artifacts, ReportArtifacts)
    assert artifacts.output_dir == output_dir
    assert artifacts.transcript_path.exists()
    assert artifacts.summary_json_path.exists()
    assert artifacts.plot_path.exists()
    assert artifacts.report_markdown_path.exists()

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    assert summary_data[0]["label"] == "LED1"
    assert summary_data[0]["channel_alignment"] == "A=OK"

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "channel_map.png" in report_text
    assert "LED1" in report_text
