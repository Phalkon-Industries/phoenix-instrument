from __future__ import annotations

import json
from pathlib import Path
from typing import List

import pytest  # type: ignore

from phoenix_benchmark.report import (
    AdcSpeedSummaryRow,
    OsrSweepSummaryRow,
    PotSweepSummaryRow,
    ReportArtifacts,
    SummaryRow,
    create_report,
    parse_summary_table,
)


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
        "# running,scenario=channel_map,sweeps=10,dwell_us=100",
        "# summary_table",
        header,
        led1,
        led2,
        "",
        "# benchmark_complete",
        "# ready",
    ]


def test_parse_summary_table_extracts_rows() -> None:
    summaries = parse_summary_table(_sample_summary_lines())

    assert "channel_map" in summaries
    rows = summaries["channel_map"]

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
    assert "channel_map" in artifacts.transcript_path.name
    assert "channel_map" in artifacts.summary_json_path.name
    assert artifacts.scenarios == ["channel_map"]
    assert "channel_map" in artifacts.plot_paths
    channel_plots = artifacts.plot_paths["channel_map"]
    assert isinstance(channel_plots, list)
    assert len(channel_plots) == 1
    assert channel_plots[0].exists()
    assert channel_plots[0] == artifacts.plot_path

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    assert summary_data[0]["scenario"] == "channel_map"
    first_row = summary_data[0]["rows"][0]
    assert first_row["label"] == "LED1"
    assert first_row["channel_alignment"] == "A=OK"

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "![channel_map plot](channel_map.png)" in report_text
    assert "| State | Samples |" in report_text
    assert "| LED1 | 10 | 123.000 |" in report_text
    assert "LED1" in report_text


def _adc_speed_summary_lines() -> List[str]:
    return [
        "# phoenix benchmark ready",
        "# running,scenario=adc_speed,duration_ms=750,modes=blocking|irq",
        "# summary_table",
        "Mode     Samples_per_s     Loop_us    Errors Notes",
        "Blocking        1234.500      500.000         0 Stable",
        "IRQ             2345.000      250.000         1 adc_errors",
        "",
        "# benchmark_complete",
        "# ready",
    ]


def test_parse_summary_table_handles_adc_speed() -> None:
    summaries = parse_summary_table(_adc_speed_summary_lines())

    assert "adc_speed" in summaries
    rows = summaries["adc_speed"]
    assert len(rows) == 2

    first = rows[0]
    assert isinstance(first, AdcSpeedSummaryRow)
    assert first.mode == "Blocking"
    assert pytest.approx(first.samples_per_second, rel=1e-6) == 1234.5
    assert first.notes == "Stable"


def test_create_report_handles_adc_speed(tmp_path: Path) -> None:
    lines = _adc_speed_summary_lines()

    plan_path = tmp_path / "adc_plan.json"
    plan_path.write_text(json.dumps({"commands": []}), encoding="utf-8")

    output_dir = tmp_path / "adc-report"
    artifacts = create_report(lines, plan_path, output_dir)

    assert "adc_speed" in artifacts.transcript_path.name
    assert "adc_speed" in artifacts.summary_json_path.name
    assert artifacts.scenarios == ["adc_speed"]
    assert "adc_speed" in artifacts.plot_paths
    adc_plots = artifacts.plot_paths["adc_speed"]
    assert isinstance(adc_plots, list)
    assert len(adc_plots) == 1
    assert adc_plots[0].exists()
    assert adc_plots[0] == artifacts.plot_path

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    assert summary_data[0]["scenario"] == "adc_speed"
    rows = summary_data[0]["rows"]
    assert rows[0]["mode"] == "Blocking"
    assert rows[1]["mode"] == "IRQ"

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "![adc_speed plot](adc_speed.png)" in report_text
    assert "| Mode | Samples/s |" in report_text
    assert "| Blocking | 1234.500 |" in report_text


def _osr_sweep_summary_lines() -> List[str]:
    return [
        "# phoenix benchmark ready",
        "# running,scenario=osr_sweep,pot=85,dwell_us=250,sweeps=12",
        "# summary_table",
        "Value      Samples  Drain_Mean  Drain_Std  Drain_Min  Drain_Max  LED1_Mean  LED1_Std  LED1_Min  LED1_Max  LED2_Mean  LED2_Std  LED2_Min  LED2_Max  Sweep_us",
        "OSR32            12       1.234       0.456       1.111       1.888       2.345       0.567       2.123       2.789       3.456       0.678       3.210       3.890      12345",
        "OSR64            12       1.500       0.400       1.200       1.900       2.800       0.600       2.400       3.000       3.900       0.700       3.500       4.100      15000",
        "",
        "# benchmark_complete",
        "# ready",
    ]


def test_parse_summary_table_handles_osr_sweep() -> None:
    summaries = parse_summary_table(_osr_sweep_summary_lines())

    assert "osr_sweep" in summaries
    rows = summaries["osr_sweep"]
    assert len(rows) == 2

    first = rows[0]
    assert isinstance(first, OsrSweepSummaryRow)
    assert first.label == "OSR32"
    assert first.osr_value == 32
    assert first.sample_count == 12
    assert pytest.approx(first.drain_std, rel=1e-6) == 0.456
    assert pytest.approx(first.led2_mean, rel=1e-6) == 3.456
    assert first.has_metrics is True


def test_create_report_handles_osr_sweep(tmp_path: Path) -> None:
    lines = _osr_sweep_summary_lines()

    plan_path = tmp_path / "osr_plan.json"
    plan_path.write_text(json.dumps({"commands": []}), encoding="utf-8")

    output_dir = tmp_path / "osr-report"
    artifacts = create_report(lines, plan_path, output_dir)

    assert "osr_sweep_pot85_dwell_us250_sweeps12" in artifacts.transcript_path.name
    assert "osr_sweep_pot85_dwell_us250_sweeps12" in artifacts.summary_json_path.name
    assert artifacts.scenarios == ["osr_sweep"]
    assert "osr_sweep" in artifacts.plot_paths
    osr_plots = artifacts.plot_paths["osr_sweep"]
    assert isinstance(osr_plots, list)
    assert len(osr_plots) == 2
    assert any(path.name.endswith("_stddev.png") for path in osr_plots)
    assert any(path.name.endswith("_duration.png") for path in osr_plots)
    for path in osr_plots:
        assert path.exists()

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    assert summary_data[0]["scenario"] == "osr_sweep"
    first_row = summary_data[0]["rows"][0]
    assert first_row["osr_value"] == 32
    assert pytest.approx(first_row["drain_std"], rel=1e-6) == 0.456
    assert first_row["sweep_duration_us"] == 12345

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "_stddev.png)" in report_text
    assert "_duration.png)" in report_text
    assert "| OSR | Samples |" in report_text
    assert "| 32 | 12 | 1.234 |" in report_text


def _pot_sweep_summary_lines() -> List[str]:
    return [
        "# phoenix benchmark ready",
        "# running,scenario=pot_sweep,sweeps_per_wiper=6,dwell_us=180,wiper_count=256",
        "# summary_table",
        "Wiper LED1_Max LED2_Max LED1_Sat LED2_Sat",
        "0x10  7000000  6800000    no    no",
        "0x20  7600000  6900000   yes    no",
        "0x30  7800000  7900000   yes   yes",
        "",
        "# pot_sweep_recommendation,led=led1,wiper=0x10",
        "# pot_sweep_recommendation,led=led2,wiper=0x20",
        "# pot_sweep_warnings,reason=saturation",
        "# benchmark_complete",
        "# ready",
    ]


def test_parse_summary_table_handles_pot_sweep() -> None:
    summaries = parse_summary_table(_pot_sweep_summary_lines())

    assert "pot_sweep" in summaries
    rows = summaries["pot_sweep"]
    assert len(rows) == 3

    first = rows[0]
    assert isinstance(first, PotSweepSummaryRow)
    assert first.wiper_code == 0x10
    assert first.led1_max_code == 7_000_000
    assert first.led1_saturated is False
    assert first.led2_max_code == 6_800_000
    assert first.led2_saturated is False


def test_create_report_handles_pot_sweep(tmp_path: Path) -> None:
    lines = _pot_sweep_summary_lines()

    plan_path = tmp_path / "pot_plan.json"
    plan_path.write_text(json.dumps({"commands": []}), encoding="utf-8")

    output_dir = tmp_path / "pot-report"
    artifacts = create_report(lines, plan_path, output_dir)

    assert "pot_sweep" in artifacts.scenarios
    assert "pot_sweep" in artifacts.plot_paths
    pot_plots = artifacts.plot_paths["pot_sweep"]
    assert len(pot_plots) == 1
    assert pot_plots[0].exists()

    assert "pot_sweep" in artifacts.csv_paths
    csv_files = artifacts.csv_paths["pot_sweep"]
    assert len(csv_files) == 1
    csv_text = csv_files[0].read_text(encoding="utf-8")
    assert "wiper_code,led1_max_code" in csv_text
    assert ",7800000," in csv_text

    assert artifacts.pot_sweep_recommendations == {"led1": "0x10", "led2": "0x20"}

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    pot_entry = next(item for item in summary_data if item["scenario"] == "pot_sweep")
    assert pot_entry["extras"]["recommendations"]["led1"] == "0x10"
    assert len(pot_entry["rows"]) == 3

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "| LED | Recommended Wiper |" in report_text
    assert "| LED1 | 0x10 |" in report_text
    assert "<details>" in report_text
    assert "</details>" in report_text
    assert "Wiper | LED1 max" in report_text
