from __future__ import annotations

import json
import re
from pathlib import Path

from typing import Dict, List

import pytest  # type: ignore

from phoenix_benchmark.cli import main


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
        first_row = (
            _channel_map_row(
                "Blue",
                8,
                120.0,
                1.5,
                0.000321,
                118.0,
                124.0,
                322.0,
                2.5,
                -0.000210,
                315.0,
                329.0,
                "A=OK",
                "--",
            )
            + "\n"
        ).encode("utf-8")
        second_row = (
            _channel_map_row(
                "Green",
                9,
                121.0,
                1.6,
                0.000111,
                119.0,
                125.0,
                323.0,
                2.6,
                -0.000199,
                316.0,
                330.0,
                "A=OK",
                "--",
            )
            + "\n"
        ).encode("utf-8")
        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# summary_table\n",
            header,
            first_row,
            b"\n",
            b"# benchmark_complete\n",
            b"# ready\n",
            b"# summary_table\n",
            header,
            second_row,
            b"\n",
            b"# benchmark_complete\n",
            b"# ready\n",
        ]


def test_cli_dry_run_emits_preview(tmp_path: Path, capsys) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {"command": "channel_map", "parameters": {"sweeps": 10}},
                ]
            }
        ),
        encoding="utf-8",
    )

    exit_code = main([str(plan), "--dry-run"])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# phoenix benchmark command plan" in stdout
    assert "channel_map" in stdout
    assert "# progress" not in stdout


def test_cli_requires_port_when_not_dry_run(tmp_path: Path) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {"commands": [{"command": "channel_map", "parameters": {"sweeps": 1}}]}
        ),
        encoding="utf-8",
    )

    with pytest.raises(SystemExit):
        main([str(plan)])


def test_cli_streams_plan_to_serial_port(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "channel_map",
                        "parameters": {
                            "sweeps": 5,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    dummy_serial = DummySerial

    captured: dict[str, object] = {}

    class DummyArtifacts:
        def __init__(self, directory: Path) -> None:
            self.output_dir = directory
            self.transcript_path = directory / "transcript.txt"
            self.summary_json_path = directory / "summary.json"
            self.plot_path = directory / "channel_map.png"
            self.report_markdown_path = directory / "report.md"
            self.scenarios = ["channel_map"]
            self.plot_paths = {"channel_map": [self.plot_path]}
            self.csv_paths = {}
            self.pot_sweep_recommendations = {}
            self.pot_sweep_warning = None
            self.dwell_sweep_recommendations = {}
            self.dwell_sweep_warning = None

    def fake_create_report(lines, plan_path, output_dir, drift_captures=None):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = output_dir
        captured["drift_captures"] = drift_captures
        return DummyArtifacts(output_dir)

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", dummy_serial)
    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    output_dir = tmp_path / "reports"

    exit_code = main([str(plan), "--port", "COM9", "--output", str(output_dir)])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# phoenix benchmark ready" in stdout
    assert "# benchmark_complete" in stdout
    progress_lines = [
        line for line in stdout.splitlines() if line.startswith("# progress")
    ]
    assert len(progress_lines) == 1
    progress_line = progress_lines[0]
    assert "step=1" in progress_line
    assert "total=1" in progress_line
    assert "command=channel_map" in progress_line
    assert re.search(r"started_at=\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", progress_line)

    instance = DummySerial.last_instance
    assert instance is not None
    assert instance.args[0] == "COM9"
    assert pytest.approx(instance.kwargs["timeout"]) == 1.0
    assert len(instance.written) >= 2
    assert instance.written[0] == b"\n"
    assert b'"sweeps":5' in instance.written[1]
    assert b'"wiper_blue_code"' not in instance.written[1]
    assert b'"wiper_green_code"' not in instance.written[1]
    assert b'"dwell_us"' not in instance.written[1]

    assert captured["plan"] == plan
    assert captured["output"] == output_dir
    recorded_lines = captured["lines"]
    assert isinstance(recorded_lines, list)
    assert "# summary_table" in recorded_lines
    assert not any(line.startswith("# progress") for line in recorded_lines)


def test_cli_emits_progress_for_multiple_commands(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {"command": "channel_map", "parameters": {"sweeps": 5}},
                    {"command": "channel_map", "parameters": {"sweeps": 3}},
                ]
            }
        ),
        encoding="utf-8",
    )

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", MultiCommandSerial)

    captured: dict[str, object] = {}

    class DummyArtifacts:
        def __init__(self, directory: Path) -> None:
            self.output_dir = directory
            self.transcript_path = directory / "transcript.txt"
            self.summary_json_path = directory / "summary.json"
            self.plot_path = directory / "channel_map.png"
            self.report_markdown_path = directory / "report.md"
            self.scenarios = ["channel_map"]
            self.plot_paths = {"channel_map": [self.plot_path]}
            self.csv_paths = {}
            self.pot_sweep_recommendations = {}
            self.pot_sweep_warning = None
            self.dwell_sweep_recommendations = {}
            self.dwell_sweep_warning = None

    def fake_create_report(lines, plan_path, output_dir, drift_captures=None):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = output_dir
        captured["drift_captures"] = drift_captures
        return DummyArtifacts(output_dir)

    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    exit_code = main([str(plan), "--port", "COM5"])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    progress_lines = [
        line for line in stdout.splitlines() if line.startswith("# progress")
    ]
    assert len(progress_lines) == 2
    assert progress_lines[0].startswith("# progress,step=1,total=2,command=channel_map")
    assert progress_lines[1].startswith("# progress,step=2,total=2,command=channel_map")
    for line in progress_lines:
        assert re.search(r"started_at=\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z", line)

    recorded_lines = captured["lines"]
    assert isinstance(recorded_lines, list)
    assert not any(line.startswith("# progress") for line in recorded_lines)

    instance = MultiCommandSerial.last_instance
    assert instance is not None
    assert len(instance.written) >= 3
    assert instance.written[0] == b"\n"
    assert b'"sweeps":5' in instance.written[1]
    assert b'"sweeps":3' in instance.written[2]


def test_cli_streams_cold_sweep_command(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "cold_sweep",
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", ColdSweepSerial)

    captured: dict[str, object] = {}

    class DummyArtifacts:
        def __init__(self, directory: Path) -> None:
            self.output_dir = directory
            self.transcript_path = directory / "transcript.txt"
            self.summary_json_path = directory / "summary.json"
            self.plot_path = directory / "cold_sweep.png"
            self.report_markdown_path = directory / "report.md"
            self.scenarios = ["cold_sweep"]
            self.plot_paths = {"cold_sweep": [self.plot_path]}
            self.csv_paths = {}
            self.pot_sweep_recommendations = {}
            self.pot_sweep_warning = None
            self.dwell_sweep_recommendations = {}
            self.dwell_sweep_warning = None

    def fake_create_report(lines, plan_path, output_dir, drift_captures=None):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = output_dir
        captured["drift_captures"] = drift_captures
        return DummyArtifacts(output_dir)

    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    output_dir = tmp_path / "reports"

    exit_code = main([str(plan), "--port", "COM7", "--output", str(output_dir)])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# running,scenario=cold_sweep" in stdout
    assert "# cold_sweep_summary" in stdout

    instance = DummySerial.last_instance
    assert instance is not None
    assert instance.args[0] == "COM7"
    payload = b"".join(instance.written)
    assert b'"command":"cold_sweep"' in payload
    assert b'"sweeps"' not in payload
    assert b'"dwell_us"' not in payload

    recorded_lines = captured.get("lines", [])
    assert isinstance(recorded_lines, list)
    assert any(line.startswith("# cold_sweep_summary") for line in recorded_lines)
    assert captured["plan"] == plan
    assert captured["output"] == output_dir


def test_cli_streams_adc_speed_command(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
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

    captured: dict[str, object] = {}

    class DummyArtifacts:
        def __init__(
            self, directory: Path, transcript_name: str, summary_name: str
        ) -> None:
            self.output_dir = directory
            self.transcript_path = directory / transcript_name
            self.summary_json_path = directory / summary_name
            self.plot_path = directory / "adc_speed.png"
            self.report_markdown_path = directory / "report.md"
            self.scenarios = ["adc_speed"]
            self.plot_paths = {"adc_speed": [self.plot_path]}
            self.csv_paths = {}
            self.pot_sweep_recommendations = {}
            self.pot_sweep_warning = None
            self.dwell_sweep_recommendations = {}
            self.dwell_sweep_warning = None

    def fake_create_report(lines, plan_path, output_dir, drift_captures=None):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = output_dir
        captured["drift_captures"] = drift_captures
        transcript_name = "transcript_adc_speed.txt"
        summary_name = "summary_adc_speed.json"
        return DummyArtifacts(output_dir, transcript_name, summary_name)

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", AdcSpeedSerial)
    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    exit_code = main([str(plan), "--port", "COM7"])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# running,scenario=adc_speed" in stdout

    instance = DummySerial.last_instance
    assert instance is not None
    assert any(b'"command":"adc_speed"' in payload for payload in instance.written)
    assert any(b'"duration_ms":750' in payload for payload in instance.written)

    assert captured["plan"] == plan
    recorded_lines = captured["lines"]
    assert isinstance(recorded_lines, list)
    assert any("Mode     Samples_per_s" in entry for entry in recorded_lines)


def test_cli_streams_osr_sweep_command(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "osr_sweep",
                        "parameters": {
                            "sweeps": 12,
                            "dwell_us": 250,
                            "wiper_code": 85,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    output_dir = tmp_path / "osr-report"

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", OsrSweepSerial)

    exit_code = main([str(plan), "--port", "COM5", "--output", str(output_dir)])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# running,scenario=osr_sweep" in stdout

    instance = DummySerial.last_instance
    assert instance is not None
    assert any(b'"command":"osr_sweep"' in payload for payload in instance.written)

    assert output_dir.exists()
    transcript_files = list(output_dir.glob("transcript_*.txt"))
    summary_files = list(output_dir.glob("summary_*.json"))
    assert len(transcript_files) == 1
    assert len(summary_files) == 1
    transcript_name = transcript_files[0].name
    summary_name = summary_files[0].name
    assert "osr_sweep_pot85_dwell_us250_sweeps12" in transcript_name
    assert "osr_sweep_pot85_dwell_us250_sweeps12" in summary_name

    plots = list(output_dir.glob("*.png"))
    assert any(
        "osr_sweep_pot85_dwell_us250_sweeps12_stddev" in plot.name for plot in plots
    )
    assert any(
        "osr_sweep_pot85_dwell_us250_sweeps12_duration" in plot.name for plot in plots
    )


def test_cli_streams_pot_sweep_command(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "pot_sweep",
                        "parameters": {"sweeps": 6, "dwell_us": 180},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    output_dir = tmp_path / "pot-report"

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", PotSweepSerial)

    captured: Dict[str, object] = {}

    class DummyArtifacts:
        def __init__(self, directory: Path) -> None:
            self.output_dir = directory
            self.transcript_path = directory / "transcript_pot_sweep.txt"
            self.summary_json_path = directory / "summary_pot_sweep.json"
            self.plot_path = directory / "pot_sweep.png"
            self.report_markdown_path = directory / "report.md"
            self.scenarios = ["pot_sweep"]
            self.plot_paths = {"pot_sweep": [self.plot_path]}
            self.csv_paths = {"pot_sweep": [directory / "pot_sweep.csv"]}
            self.pot_sweep_recommendations = {"blue": "0x10", "green": "0x20"}
            self.pot_sweep_warning = "saturation"
            self.dwell_sweep_recommendations = {}
            self.dwell_sweep_warning = None
            self.transcript_path.parent.mkdir(parents=True, exist_ok=True)
            self.transcript_path.write_text("", encoding="utf-8")
            self.summary_json_path.write_text("[]", encoding="utf-8")
            self.plot_path.write_text("plot", encoding="utf-8")
            self.report_markdown_path.write_text("report", encoding="utf-8")
            for path in self.csv_paths["pot_sweep"]:
                path.write_text(
                    "wiper,blue_max,green_max,blue_saturated,green_saturated\n",
                    encoding="utf-8",
                )

    def fake_create_report(lines, plan_path, out_dir, drift_captures=None):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = out_dir
        captured["drift_captures"] = drift_captures
        return DummyArtifacts(out_dir)

    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    exit_code = main([str(plan), "--port", "COM10", "--output", str(output_dir)])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# running,scenario=pot_sweep" in stdout
    assert "# pot_sweep_recommendation" in stdout
    assert "# pot_sweep_summary,blue=0x10,green=0x20,warnings=saturation" in stdout

    instance = DummySerial.last_instance
    assert instance is not None
    assert any(b'"command":"pot_sweep"' in payload for payload in instance.written)
    assert any(b'"sweeps":6' in payload for payload in instance.written)
    assert any(b'"dwell_us":180' in payload for payload in instance.written)
    assert all(b'"wipers"' not in payload for payload in instance.written)

    assert captured["plan"] == plan
    assert captured["output"] == output_dir
    recorded_lines = captured["lines"]
    assert isinstance(recorded_lines, list)
    assert any("Wiper Blue_Max" in entry for entry in recorded_lines)


def test_cli_streams_dwell_sweep_command(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "dwell_sweep",
                        "parameters": {
                            "sweeps_per_dwell": 4,
                            "start_dwell_us": 100,
                            "end_dwell_us": 300,
                            "dwell_step_us": 100,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    output_dir = tmp_path / "dwell-report"

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", DwellSweepSerial)

    captured: Dict[str, object] = {}

    class DummyArtifacts:
        def __init__(self, directory: Path) -> None:
            self.output_dir = directory
            slug = "dwell_sweep_start_us100_end_us300_step_us100_steps3"
            self.transcript_path = directory / f"transcript_{slug}.txt"
            self.summary_json_path = directory / f"summary_{slug}.json"
            self.plot_paths = {
                "dwell_sweep": [
                    directory / f"{slug}_variance.png",
                    directory / f"{slug}_duration.png",
                ]
            }
            self.plot_path = self.plot_paths["dwell_sweep"][0]
            self.csv_paths = {"dwell_sweep": [directory / f"{slug}.csv"]}
            self.report_markdown_path = directory / "report.md"
            self.scenarios = ["dwell_sweep"]
            self.pot_sweep_recommendations = {}
            self.pot_sweep_warning = None
            self.dwell_sweep_recommendations = {
                "recommended": 100,
                "stable_dwells": [100, 300],
                "threshold": 0.75,
            }
            self.dwell_sweep_warning = "saturation"

    def fake_create_report(lines, plan_path, output_dir, drift_captures=None):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = output_dir
        captured["drift_captures"] = drift_captures
        return DummyArtifacts(output_dir)

    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    exit_code = main([str(plan), "--port", "COM12", "--output", str(output_dir)])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# running,scenario=dwell_sweep" in stdout
    assert "# dwell_sweep_schedule,index=1,steps=3,dwells=100|200|300" in stdout
    assert (
        "# dwell_sweep_summary,recommended=100,stable=100|300,threshold=0.75,warnings=saturation"
        in stdout
    )

    instance = DummySerial.last_instance
    assert instance is not None
    assert any(b'"command":"dwell_sweep"' in payload for payload in instance.written)

    assert captured["plan"] == plan
    assert captured["output"] == output_dir
    recorded_lines = captured["lines"]
    assert isinstance(recorded_lines, list)
    assert any("Dwell_us  Sweeps" in entry for entry in recorded_lines)


def test_cli_streams_drift_capture_command(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "drift_capture",
                        "parameters": {
                            "start_time_us": 0,
                            "end_time_us": 50,
                            "step_delay_us": 10,
                            "osr": 4096,
                            "wiper_blue_code": 42,
                            "wiper_green_code": 84,
                        },
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    output_dir = tmp_path / "drift-report"
    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", DriftCaptureSerial)

    captured: Dict[str, object] = {}

    def fake_create_report(lines, plan_path, out_dir, drift_captures=None):  # type: ignore[no-untyped-def]
        burst_list = list(drift_captures or [])
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = out_dir
        captured["drift_captures"] = burst_list

        class DummyArtifacts:
            def __init__(self, directory: Path) -> None:
                self.output_dir = directory
                self.transcript_path = directory / "transcript_drift_capture.txt"
                self.summary_json_path = directory / "summary_drift_capture.json"
                primary_slug = burst_list[0].slug() if burst_list else "drift_capture"
                self.plot_path = directory / f"{primary_slug}.png"
                self.plot_paths = {"drift_capture": [self.plot_path]}
                self.csv_paths = {"drift_capture": [directory / f"{primary_slug}.csv"]}
                self.report_markdown_path = directory / "report.md"
                self.report_markdown_path.write_text("", encoding="utf-8")
                self.scenarios = ["drift_capture"]
                self.pot_sweep_recommendations = {}
                self.pot_sweep_warning = None
                self.dwell_sweep_recommendations = {}
                self.dwell_sweep_warning = None
                self.drift_capture_json_paths = {
                    burst.slug(): directory / f"{burst.slug()}.json"
                    for burst in burst_list
                }
                self.drift_capture_csv_paths = {
                    burst.slug(): directory / f"{burst.slug()}.csv"
                    for burst in burst_list
                }
                self.drift_capture_plot_paths = {
                    burst.slug(): directory / f"{burst.slug()}.png"
                    for burst in burst_list
                }

        return DummyArtifacts(out_dir)

    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    exit_code = main([str(plan), "--port", "COM15", "--output", str(output_dir)])
    assert exit_code == 0

    stdout = capsys.readouterr().out
    assert "# running,scenario=drift_capture" in stdout
    assert "# drift_capture_summary" in stdout

    instance = DummySerial.last_instance
    assert instance is not None
    assert any(b'"command":"drift_capture"' in payload for payload in instance.written)
    assert any(b'"wiper_blue_code":42' in payload for payload in instance.written)
    assert any(b'"wiper_green_code":84' in payload for payload in instance.written)

    drift_captures = captured.get("drift_captures")
    assert isinstance(drift_captures, list)
    assert len(drift_captures) == 1

    slug = "drift_capture_start_us0_end_us50_step_us10_osr4096_wiper_blue_0x2A_wiper_green_0x54"
    csv_path = output_dir / f"{slug}.csv"
    json_path = output_dir / f"{slug}.json"
    plot_path = output_dir / f"{slug}.png"

    assert csv_path.exists(), "drift capture CSV should be written"
    assert json_path.exists(), "drift capture JSON summary should be written"
    assert plot_path.exists(), "drift capture plot should be rendered"

    summary = json.loads(json_path.read_text(encoding="utf-8"))
    assert summary["warnings"] == ["buffer_overflow", "saturation"]
    assert summary["metadata"]["osr"] == 4096
    assert summary["metadata"]["wiper_blue"] == "0x2A"
    assert summary["metadata"]["wiper_green"] == "0x54"


def test_cli_aborts_when_device_reports_error(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys
) -> None:
    plan = tmp_path / "plan.json"
    plan.write_text(
        json.dumps(
            {
                "commands": [
                    {
                        "command": "channel_map",
                        "parameters": {"sweeps": 5},
                    }
                ]
            }
        ),
        encoding="utf-8",
    )

    monkeypatch.setattr("phoenix_benchmark.cli.serial.Serial", ErrorSerial)

    report_called = False

    def fake_create_report(*_args, **_kwargs):  # type: ignore[no-untyped-def]
        nonlocal report_called
        report_called = True
        raise AssertionError("Report should not be generated when device errors")

    monkeypatch.setattr("phoenix_benchmark.cli.create_report", fake_create_report)

    exit_code = main(
        [str(plan), "--port", "COM6", "--ready-timeout", "2", "--command-timeout", "5"]
    )
    assert exit_code == 1
    assert report_called is False

    captured = capsys.readouterr()
    assert "# error,channel_map_parse_failed" in captured.out
    assert "Device reported error" in captured.err
