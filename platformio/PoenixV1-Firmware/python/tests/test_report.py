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
        f"{alignment:<12}"
    ).rstrip()


def _sample_summary_lines() -> List[str]:
    header = "State      Samples      Mean_A      Std_A      Min_A      Max_A      Mean_B      Std_B      Min_B      Max_B   Channel_Map"
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
        "B!=A",
    )
    return [
        "# phoenix benchmark ready",
        "# summary_table",
        header,
        led1,
        led2,
        "",
        "# benchmark_complete",
        "# ready",
    ]


def test_parse_summary_table_extracts_rows() -> None:
    rows = parse_summary_table(_sample_summary_lines())

    assert len(rows) == 2

    first = rows[0]
    assert first.label == "LED1"
    assert first.sample_count == 10
    assert pytest.approx(first.mean_channel_a, rel=1e-6) == 123.0
    assert first.channel_alignment == "A=OK"
    assert first.has_channel_metrics is True


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
