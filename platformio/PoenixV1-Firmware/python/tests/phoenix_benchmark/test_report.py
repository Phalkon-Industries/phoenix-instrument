from __future__ import annotations

import json
from pathlib import Path
from typing import List

import pytest  # type: ignore

from phoenix_benchmark.report import (
    AdcSpeedSummaryRow,
    ColdSweepSummaryRow,
    DwellSweepSummaryRow,
    OsrSweepSummaryRow,
    PotSweepSummaryRow,
    ReportArtifacts,
    SummaryRow,
    create_report,
    parse_summary_table,
)


def _channel_map_header() -> str:
    return (
        f"{'State':<8}"
        f"{'Samples':>9}"
        f"{'Mean_A':>12}"
        f"{'Std_A':>12}"
        f"{'Slope_A':>12}"
        f"{'Min_A':>12}"
        f"{'Max_A':>12}"
        f"{'Mean_B':>12}"
        f"{'Std_B':>12}"
        f"{'Slope_B':>12}"
        f"{'Min_B':>12}"
        f"{'Max_B':>12}"
        f"{'Channel_Map':>12}"
        f"{'Warnings':>14}"
    )


def _channel_map_row(
    label: str,
    samples: int,
    mean_a: float,
    std_a: float,
    slope_a: float,
    min_a: float,
    max_a: float,
    mean_b: float,
    std_b: float,
    slope_b: float,
    min_b: float,
    max_b: float,
    alignment: str,
    warnings: str,
) -> str:
    return (
        f"{label:<8}"
        f"{samples:>9d}"
        f"{mean_a:>12.3f}"
        f"{std_a:>12.3f}"
        f"{slope_a:>12.6f}"
        f"{min_a:>12.3f}"
        f"{max_a:>12.3f}"
        f"{mean_b:>12.3f}"
        f"{std_b:>12.3f}"
        f"{slope_b:>12.6f}"
        f"{min_b:>12.3f}"
        f"{max_b:>12.3f}"
        f"{alignment:<12}"
        f"{warnings:<14}"
    ).rstrip()


def _format_row(
    label: str,
    samples: int,
    mean_a: float,
    std_a: float,
    slope_a: float,
    min_a: float,
    max_a: float,
    mean_b: float,
    std_b: float,
    slope_b: float,
    min_b: float,
    max_b: float,
    alignment: str,
    warnings: str,
) -> str:
    return (
        f"{label:<8}"
        f"{samples:>9}"
        f"{mean_a:>12.3f}"
        f"{std_a:>12.3f}"
        f"{slope_a:>12.6f}"
        f"{min_a:>12.3f}"
        f"{max_a:>12.3f}"
        f"{mean_b:>12.3f}"
        f"{std_b:>12.3f}"
        f"{slope_b:>12.6f}"
        f"{min_b:>12.3f}"
        f"{max_b:>12.3f}"
        f"{alignment:<12}"
        f"{warnings:<14}"
    ).rstrip()


def _cold_sweep_summary_header() -> str:
    return (
        "Channel        Samples  Mean      StdDev     Min        Max        Saturated"
    )


def _format_optional_float_field(value: float | None, width: int) -> str:
    if value is None:
        return f"{'--':>{width}}"
    return f"{value:>{width}.3f}"


def _format_optional_int_field(value: int | None, width: int) -> str:
    if value is None:
        return f"{'--':>{width}}"
    return f"{value:>{width}d}"


def _cold_sweep_summary_row(
    label: str,
    samples: int,
    mean: float | None,
    stddev: float | None,
    min_code: int | None,
    max_code: int | None,
    saturated: str,
) -> str:
    return (
        f"{label:<12}  "
        f"{samples:7d}  "
        f"{_format_optional_float_field(mean, 9)}  "
        f"{_format_optional_float_field(stddev, 9)}  "
        f"{_format_optional_int_field(min_code, 10)}  "
        f"{_format_optional_int_field(max_code, 10)}  "
        f"{saturated:>9}"
    ).rstrip()


def _cold_sweep_sample_header() -> str:
    return "Index  Drain_Blue  Drain_Green  Blue  Green  Saturation"


def _cold_sweep_sample_row(
    index: int,
    drain_blue: int,
    drain_green: int,
    blue: int,
    green: int,
    saturation: str,
) -> str:
    return (
        f"{index:5d}  "
        f"{drain_blue:11d}  "
        f"{drain_green:12d}  "
        f"{blue:5d}  "
        f"{green:6d}  "
        f"{saturation}"
    )


def _cold_sweep_summary_lines() -> List[str]:
    header = _cold_sweep_summary_header()
    drain_row = _cold_sweep_summary_row(
        "drain_blue",
        4,
        120.0,
        5.5,
        100,
        140,
        "no",
    )
    blue_row = _cold_sweep_summary_row(
        "blue",
        0,
        None,
        None,
        None,
        None,
        "yes",
    )

    sample_header = _cold_sweep_sample_header()
    sample_rows = [
        _cold_sweep_sample_row(0, 120, 125, 118, 122, "none"),
        _cold_sweep_sample_row(1, 122, 126, 119, 123, "db"),
        _cold_sweep_sample_row(2, 121, 124, 118, 121, "none"),
        _cold_sweep_sample_row(3, 123, 127, 120, 124, "blue|green"),
    ]

    return [
        "# phoenix benchmark ready",
        "# running,scenario=cold_sweep,sweeps=4,dwell_blue_us=100,dwell_green_us=110",
        "# summary_table",
        header,
        drain_row,
        blue_row,
        "",
        "# cold_sweep_samples",
        sample_header,
        *sample_rows,
        "",
        "# cold_sweep_metadata,captured_sweeps=4,timestamp_us=123456",
        "# cold_sweep_warnings,reason=saturation",
        "# benchmark_complete",
        "# ready",
    ]


def _sample_summary_lines() -> List[str]:
    header = _channel_map_header()
    blue = _format_row(
        "Blue",
        10,
        123.0,
        2.0,
        0.001234,
        120.0,
        130.0,
        321.0,
        3.0,
        -0.000321,
        310.0,
        330.0,
        "A=OK",
        "--",
    )
    green = _format_row(
        "Green",
        8,
        223.0,
        4.0,
        -0.002468,
        210.0,
        235.0,
        121.0,
        2.5,
        0.000789,
        118.0,
        125.0,
        "B!=A",
        "saturation",
    )
    return [
        "# phoenix benchmark ready",
        "# running,scenario=channel_map,sweeps=10",
        "# summary_table",
        header,
        blue,
        green,
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
    assert first.label == "Blue"
    assert first.sample_count == 10
    assert pytest.approx(first.mean_channel_a, rel=1e-6) == 123.0
    assert pytest.approx(first.slope_channel_a, rel=1e-6) == 0.001234
    assert pytest.approx(first.slope_channel_b, rel=1e-6) == -0.000321
    assert first.channel_alignment == "A=OK"
    assert first.warning_label == "--"
    assert first.has_channel_metrics is True


def test_parse_summary_table_handles_cold_sweep() -> None:
    summaries = parse_summary_table(_cold_sweep_summary_lines())

    assert "cold_sweep" in summaries
    rows = summaries["cold_sweep"]

    assert len(rows) == 2

    first = rows[0]
    assert isinstance(first, ColdSweepSummaryRow)
    assert first.channel == "drain_blue"
    assert first.sample_count == 4
    assert pytest.approx(first.mean, rel=1e-6) == 120.0
    assert pytest.approx(first.stddev, rel=1e-6) == 5.5
    assert first.min_code == 100
    assert first.max_code == 140
    assert first.saturated is False
    assert first.has_metrics is True

    second = rows[1]
    assert isinstance(second, ColdSweepSummaryRow)
    assert second.channel == "blue"
    assert second.has_metrics is False
    assert second.mean is None
    assert second.saturated is True


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
    assert first_row["label"] == "Blue"
    assert first_row["channel_alignment"] == "A=OK"
    assert first_row["warning_label"] == "--"
    assert pytest.approx(first_row["slope_channel_a"], rel=1e-6) == 0.001234
    assert pytest.approx(first_row["slope_channel_b"], rel=1e-6) == -0.000321

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "![channel_map plot](channel_map.png)" in report_text
    assert (
        "| State | Samples | Mean A | Std A | Slope A | Min A | Max A | Mean B | Std B | Slope B | Min B | Max B | Channel map | Warnings |"
        in report_text
    )
    assert (
        "| Blue | 10 | 123.000 | 2.000 | 0.001234 | 120.000 | 130.000 | 321.000 | 3.000 | -0.000321 | 310.000 | 330.000 | A=OK | -- |"
        in report_text
    )
    assert "Blue" in report_text


def test_create_report_handles_cold_sweep(tmp_path: Path) -> None:
    lines = _cold_sweep_summary_lines()

    plan_path = tmp_path / "cold_plan.json"
    plan_path.write_text(json.dumps({"commands": []}), encoding="utf-8")

    output_dir = tmp_path / "cold-report"
    artifacts = create_report(lines, plan_path, output_dir)

    assert "cold_sweep" in artifacts.scenarios
    assert "cold_sweep" in artifacts.plot_paths
    cold_plots = artifacts.plot_paths["cold_sweep"]
    assert cold_plots
    for path in cold_plots:
        assert path.exists()

    assert "cold_sweep" in artifacts.csv_paths
    cold_csvs = artifacts.csv_paths["cold_sweep"]
    assert cold_csvs
    for csv_path in cold_csvs:
        assert csv_path.exists()

    csv_text = cold_csvs[0].read_text(encoding="utf-8")
    assert (
        "index,drain_blue_code,drain_green_code,blue_code,green_code,saturation"
        in csv_text
    )
    assert "1,122,126,119,123,db" in csv_text

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    cold_entry = next(item for item in summary_data if item["scenario"] == "cold_sweep")
    rows = cold_entry["rows"]
    assert rows[0]["channel"] == "drain_blue"
    assert pytest.approx(rows[0]["mean"], rel=1e-6) == 120.0
    extras = cold_entry["extras"]
    assert extras["captured_sweeps"] == 4
    assert extras["warning"] == "saturation"
    assert extras["sample_count"] == 4
    drift_codes = extras.get("drift_codes")
    assert isinstance(drift_codes, dict)
    assert drift_codes["drain_blue"] == 3
    assert drift_codes["blue"] == 2

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "Warm-up guidance" in report_text
    assert (
        "| Channel | Samples | Mean | StdDev | Min | Max | Drift (Δ) | Saturated |"
        in report_text
    )
    assert "| drain_blue | 4 | 120.000 | 5.500 | 100 | 140 | +3 | no |" in report_text
    assert "![cold_sweep plot]" in report_text
    assert "[Download CSV](cold_sweep_samples.csv)" in report_text


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
        "Value      Samples  Drain_Mean  Drain_Std  Drain_Min  Drain_Max  Blue_Mean  Blue_Std  Blue_Min  Blue_Max  Green_Mean  Green_Std  Green_Min  Green_Max  Sweep_us",
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
    assert pytest.approx(first.green_mean, rel=1e-6) == 3.456
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
        "Wiper Blue_Max Green_Max Blue_Sat Green_Sat",
        "0x10  7000000  6800000    no    no",
        "0x20  7600000  6900000   yes    no",
        "0x30  7800000  7900000   yes   yes",
        "",
        "# pot_sweep_recommendation,led=blue,wiper=0x10",
        "# pot_sweep_recommendation,led=green,wiper=0x20",
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
    assert first.blue_max_code == 7_000_000
    assert first.blue_saturated is False
    assert first.green_max_code == 6_800_000
    assert first.green_saturated is False


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
    assert "wiper_code,blue_max_code" in csv_text
    assert ",7800000," in csv_text

    assert artifacts.pot_sweep_recommendations == {"blue": "0x10", "green": "0x20"}

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    pot_entry = next(item for item in summary_data if item["scenario"] == "pot_sweep")
    assert pot_entry["extras"]["recommendations"]["blue"] == "0x10"
    assert len(pot_entry["rows"]) == 3

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "| LED | Recommended Wiper |" in report_text
    assert "| Blue | 0x10 |" in report_text
    assert "<details>" in report_text
    assert "</details>" in report_text
    assert "Wiper | Blue max" in report_text


def _dwell_sweep_summary_lines() -> List[str]:
    return [
        "# phoenix benchmark ready",
        "# running,scenario=dwell_sweep,sweeps_per_dwell=4,start_us=100,end_us=300,step_us=100,steps=3",
        "# summary_table",
        "Dwell_us  Sweeps  Drain_Mean  Drain_Std  Drain_Slope  Blue_Mean  Blue_Std  Blue_Slope  Green_Mean  Green_Std  Green_Slope  Duration_us  Warning_Mask",
        "     100       4        1.234      0.111      -0.123        2.345      0.222       1.050        3.456      0.333      -0.900        50000  0x00",
        "     200       4        1.500      0.450       0.250        2.650      0.520      -0.640        3.700      0.610       0.315        60000  0x01",
        "     300       2           --        --         --           --        --         --           --        --         --        30000  0x00",
        "",
        "# dwell_sweep_warnings,reason=saturation",
        "# benchmark_complete",
        "# ready",
    ]


def test_parse_summary_table_handles_dwell_sweep() -> None:
    summaries = parse_summary_table(_dwell_sweep_summary_lines())

    assert "dwell_sweep" in summaries
    rows = summaries["dwell_sweep"]
    assert len(rows) == 3

    first = rows[0]
    assert isinstance(first, DwellSweepSummaryRow)
    assert first.dwell_us == 100
    assert first.sweeps_completed == 4
    assert pytest.approx(first.drain_std, rel=1e-6) == 0.111
    assert pytest.approx(first.drain_slope, rel=1e-6) == -0.123
    assert pytest.approx(first.blue_slope, rel=1e-6) == 1.05
    assert pytest.approx(first.green_slope, rel=1e-6) == -0.9
    assert first.warning_mask == 0
    assert first.has_metrics is True

    third = rows[2]
    assert isinstance(third, DwellSweepSummaryRow)
    assert third.has_metrics is False
    assert third.duration_us == 30_000


def test_create_report_handles_dwell_sweep(tmp_path: Path) -> None:
    lines = _dwell_sweep_summary_lines()

    plan_path = tmp_path / "dwell_plan.json"
    plan_path.write_text(json.dumps({"commands": []}), encoding="utf-8")

    output_dir = tmp_path / "dwell-report"
    artifacts = create_report(lines, plan_path, output_dir)

    assert "dwell_sweep" in artifacts.scenarios
    assert "dwell_sweep" in artifacts.plot_paths
    dwell_plots = artifacts.plot_paths["dwell_sweep"]
    assert len(dwell_plots) == 3
    assert any(path.name.endswith("_variance.png") for path in dwell_plots)
    assert any(path.name.endswith("_slope.png") for path in dwell_plots)
    assert any(path.name.endswith("_duration.png") for path in dwell_plots)
    for path in dwell_plots:
        assert path.exists()

    assert "dwell_sweep" in artifacts.csv_paths
    csv_files = artifacts.csv_paths["dwell_sweep"]
    assert len(csv_files) == 1
    csv_text = csv_files[0].read_text(encoding="utf-8")
    assert "dwell_us,sweeps_completed" in csv_text
    assert "drain_slope" in csv_text
    assert "200,4" in csv_text

    recommendations = artifacts.dwell_sweep_recommendations
    assert isinstance(recommendations, dict)
    assert recommendations.get("recommended") == 100
    assert artifacts.dwell_sweep_warning == "saturation"

    summary_data = json.loads(artifacts.summary_json_path.read_text(encoding="utf-8"))
    dwell_entry = next(
        item for item in summary_data if item["scenario"] == "dwell_sweep"
    )
    assert len(dwell_entry["rows"]) == 3
    assert dwell_entry["rows"][0]["dwell_us"] == 100
    assert dwell_entry["rows"][1]["warning_mask"] == 1
    assert pytest.approx(dwell_entry["rows"][0]["drain_slope"], rel=1e-6) == -0.123
    assert pytest.approx(dwell_entry["rows"][0]["blue_slope"], rel=1e-6) == 1.05
    assert pytest.approx(dwell_entry["rows"][0]["green_slope"], rel=1e-6) == -0.9

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "## Summary Table (dwell_sweep)" in report_text
    assert "Recommended dwell" in report_text
    assert "saturation" in report_text
    assert (
        "| Dwell (µs) | Sweeps | Drain mean | Drain std | Drain slope | Blue mean | Blue std | Blue slope | Green mean | Green std | Green slope | Duration (µs) | Warnings |"
        in report_text
    )
    assert (
        "| 100 | 4 | 1.234 | 0.111 | -0.123 | 2.345 | 0.222 | 1.050 | 3.456 | 0.333 | -0.900 | 50000 | none |"
        in report_text
    )
    assert "![dwell_sweep plot #2]" in report_text


def _drift_capture_lines() -> List[str]:
    return [
        "# phoenix benchmark ready",
        "# running,scenario=drift_capture,start_us=0,end_us=50,step_us=10",
        "# drift_capture,metadata,start_us=0,end_us=50,step_delay_us=10,osr=4096,wiper_code=0x2A",
        "# drift_capture,results,blue_samples=3,green_samples=2,warning_mask=0x03",
        "Elapsed_Blue_us\tCode_Blue\tElapsed_Green_us\tCode_Green",
        "0\t1024\tnan\tnan",
        "10\t1100\t0\t950",
        "20\t1200\t10\t960",
        "",
        "# benchmark_complete",
        "# ready",
    ]


def test_create_report_handles_drift_capture(tmp_path: Path) -> None:
    lines = _drift_capture_lines()

    plan_path = tmp_path / "drift_plan.json"
    plan_path.write_text(json.dumps({"commands": []}), encoding="utf-8")

    output_dir = tmp_path / "drift-report"
    artifacts = create_report(lines, plan_path, output_dir)

    slug = "drift_capture_start_us0_end_us50_step_us10_osr4096_wiper_0x2A"

    assert "drift_capture" in artifacts.scenarios
    assert "drift_capture" in artifacts.plot_paths
    assert "drift_capture" in artifacts.csv_paths

    json_map = getattr(artifacts, "drift_capture_json_paths", {})
    assert slug in json_map
    json_path = json_map[slug]
    assert json_path.exists()

    data = json.loads(json_path.read_text(encoding="utf-8"))
    assert data["metadata"]["end_us"] == 50
    assert data["warnings"] == ["buffer_overflow", "saturation"]
    assert len(data["samples"]) == 3

    report_text = artifacts.report_markdown_path.read_text(encoding="utf-8")
    assert "## Drift Capture" in report_text
    assert (
        "| Burst | Start (us) | End (us) | Step (us) | OSR | Wiper | Blue Samples | Green Samples | Warnings |"
        in report_text
    )
    assert (
        "| 1 | 0 | 50 | 10 | 4096 | 0x2A | 3 | 2 | buffer_overflow, saturation |"
        in report_text
    )
    assert "### Burst 1" in report_text
    artifact_line = f"Artifacts: [CSV]({slug}.csv) · [JSON]({slug}.json)"
    assert artifact_line in report_text
    assert "<details>" in report_text
    assert "<summary>Burst 1 samples</summary>" in report_text
    assert "</details>" in report_text
    assert "Elapsed Blue" in report_text
    assert "buffer_overflow" in report_text
    plot_token = f"![drift_capture plot]({slug}.png)"
    assert plot_token in report_text
    assert report_text.index(plot_token) < report_text.index("<details>")
