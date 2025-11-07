from __future__ import annotations

import json
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
        f"{'Min_A':>12}"
        f"{'Max_A':>12}"
        f"{'Mean_B':>12}"
        f"{'Std_B':>12}"
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
    min_a: float,
    max_a: float,
    mean_b: float,
    std_b: float,
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
        f"{min_a:>12.3f}"
        f"{max_a:>12.3f}"
        f"{mean_b:>12.3f}"
        f"{std_b:>12.3f}"
        f"{min_b:>12.3f}"
        f"{max_b:>12.3f}"
        f"{alignment:<12}"
        f"{warnings:<14}"
    )


class DummySerial:
    last_instance: "DummySerial | None" = None

    def __init__(self, *args, **kwargs) -> None:
        self.args = args
        self.kwargs = kwargs
        self.written: List[bytes] = []
        self._line_queue: List[bytes] = [
            b"# phoenix benchmark ready\n",
            b"# summary_table\n",
            (_channel_map_header() + "\n").encode("utf-8"),
            (
                _channel_map_row(
                    "Blue",
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
                    "--",
                )
                + "\n"
            ).encode("utf-8"),
            b"\n",
            b"# benchmark_complete\n",
            b"# ready\n",
        ]
        DummySerial.last_instance = self

    def __enter__(self) -> "DummySerial":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        return None

    def write(self, data: bytes) -> int:
        self.written.append(data)
        return len(data)

    def flush(self) -> None:
        return None

    def readline(self) -> bytes:
        if not self._line_queue:
            return b""
        return self._line_queue.pop(0)


class ErrorSerial(DummySerial):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# running,scenario=channel_map\n",
            b"# error,channel_map_parse_failed,reason=unknown argument\n",
            b"# ready\n",
        ]


class AdcSpeedSerial(DummySerial):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# running,scenario=adc_speed,duration_ms=750,modes=blocking|irq\n",
            b"# summary_table\n",
            b"Mode     Samples_per_s     Loop_us    Errors Notes\n",
            b"Blocking        1234.500      500.000         0 Stable\n",
            b"IRQ             2345.000      250.000         1 adc_errors\n",
            b"\n",
            b"# benchmark_complete\n",
            b"# ready\n",
        ]


class OsrSweepSerial(DummySerial):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)

        def format_row() -> str:
            label = "OSR32"
            sweeps = 12
            values = {
                "drain_mean": 1.234,
                "drain_std": 0.456,
                "drain_min": 1.111,
                "drain_max": 1.888,
                "blue_mean": 2.345,
                "blue_std": 0.567,
                "blue_min": 2.123,
                "blue_max": 2.789,
                "green_mean": 3.456,
                "green_std": 0.678,
                "green_min": 3.210,
                "green_max": 3.890,
                "sweep_us": 12345,
            }
            return (
                f"{label:<10}{sweeps:>9}  "
                f"{values['drain_mean']:>10.3f}  {values['drain_std']:>10.3f}  "
                f"{values['drain_min']:>10.3f}  {values['drain_max']:>10.3f}  "
                f"{values['blue_mean']:>10.3f}  {values['blue_std']:>10.3f}  "
                f"{values['blue_min']:>10.3f}  {values['blue_max']:>10.3f}  "
                f"{values['green_mean']:>10.3f}  {values['green_std']:>10.3f}  "
                f"{values['green_min']:>10.3f}  {values['green_max']:>10.3f}  "
                f"{values['sweep_us']:>10d}"
            )

        row = format_row().encode("utf-8") + b"\n"
        header = (
            "Value      Samples  Drain_Mean  Drain_Std  Drain_Min  Drain_Max  "
            "Blue_Mean  Blue_Std  Blue_Min  Blue_Max  Green_Mean  Green_Std  "
            "Green_Min  Green_Max  Sweep_us"
        ).encode("utf-8") + b"\n"
        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# running,scenario=osr_sweep,pot=85,dwell_us=250,sweeps=12\n",
            b"# summary_table\n",
            header,
            row,
            b"\n",
            b"# benchmark_complete\n",
            b"# ready\n",
        ]


class PotSweepSerial(DummySerial):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# running,scenario=pot_sweep,sweeps_per_wiper=6,dwell_us=180,wiper_count=256\n",
            b"# summary_table\n",
            b"Wiper Blue_Max Green_Max Blue_Sat Green_Sat\n",
            b"0x10  7000000  6800000    no    no\n",
            b"0x20  7600000  6900000   yes    no\n",
            b"0x30  7800000  7900000   yes   yes\n",
            b"\n",
            b"# pot_sweep_recommendation,led=blue,wiper=0x10\n",
            b"# pot_sweep_recommendation,led=green,wiper=0x20\n",
            b"# pot_sweep_warnings,reason=saturation\n",
            b"# benchmark_complete\n",
            b"# ready\n",
        ]


class DwellSweepSerial(DummySerial):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)

        def format_row(
            dwell: int,
            sweeps: int,
            drain_mean: float | None,
            drain_std: float | None,
            blue_mean: float | None,
            blue_std: float | None,
            green_mean: float | None,
            green_std: float | None,
            duration: int,
            warning_mask: int,
        ) -> bytes:
            def render_metric(value: float | None, width: int) -> str:
                if value is None:
                    return "--".rjust(width)
                return f"{value:>{width}.3f}"

            segments = [
                f"{dwell:>8d}",
                f"{sweeps:>6d}",
                render_metric(drain_mean, 10),
                render_metric(drain_std, 9),
                render_metric(blue_mean, 10),
                render_metric(blue_std, 8),
                render_metric(green_mean, 10),
                render_metric(green_std, 8),
                f"{duration:>12d}",
                f"0x{warning_mask:02X}",
            ]
            return ("  ".join(segments) + "\n").encode("utf-8")

        header = (
            "Dwell_us  Sweeps  Drain_Mean  Drain_Std  Blue_Mean  Blue_Std  "
            "Green_Mean  Green_Std  Duration_us  Warning_Mask\n"
        ).encode("utf-8")

        row_stable = format_row(
            100, 4, 1.234, 0.111, 2.345, 0.222, 3.456, 0.333, 50_000, 0x00
        )
        row_saturation = format_row(
            200, 4, 1.400, 0.250, 2.500, 0.350, 3.600, 0.450, 60_000, 0x01
        )
        row_incomplete = format_row(
            300, 2, None, None, None, None, None, None, 30_000, 0x00
        )

        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# running,scenario=dwell_sweep,sweeps_per_dwell=4,start_us=100,end_us=300,step_us=100,steps=3\n",
            b"# summary_table\n",
            header,
            row_stable,
            row_saturation,
            row_incomplete,
            b"\n",
            b"# dwell_sweep_warnings,reason=saturation\n",
            b"# benchmark_complete\n",
            b"# ready\n",
        ]


class DriftCaptureSerial(DummySerial):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# running,scenario=drift_capture,start_us=0,end_us=50,step_us=10\n",
            b"# drift_capture,metadata,start_us=0,end_us=50,step_delay_us=10,osr=4096,wiper_code=0x2A\n",
            b"# drift_capture,results,blue_samples=3,green_samples=2,warning_mask=0x03\n",
            b"Elapsed_Blue_us\tCode_Blue\tElapsed_Green_us\tCode_Green\n",
            b"0\t1024\tnan\tnan\n",
            b"10\t1100\t0\t950\n",
            b"20\t1200\t10\t960\n",
            b"\n",
            b"# benchmark_complete\n",
            b"# ready\n",
        ]


class ColdSweepSerial(DummySerial):
    def __init__(self, *args, **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self._line_queue = [
            b"# phoenix benchmark ready\n",
            b"# running,scenario=cold_sweep,sweeps=500,dwell_blue_us=10,dwell_green_us=100\n",
            b"# cold_sweep_summary,timestamp_us=123456,samples=4,warnings=0x00\n",
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

    instance = DummySerial.last_instance
    assert instance is not None
    assert instance.args[0] == "COM9"
    assert pytest.approx(instance.kwargs["timeout"]) == 1.0
    assert len(instance.written) >= 2
    assert instance.written[0] == b"\n"
    assert b'"sweeps":5' in instance.written[1]
    assert b'"wiper_code"' not in instance.written[1]
    assert b'"dwell_us"' not in instance.written[1]

    assert captured["plan"] == plan
    assert captured["output"] == output_dir
    recorded_lines = captured["lines"]
    assert isinstance(recorded_lines, list)
    assert "# summary_table" in recorded_lines


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
                            "wiper_code": 42,
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
    assert any(b'"wiper_code":42' in payload for payload in instance.written)

    drift_captures = captured.get("drift_captures")
    assert isinstance(drift_captures, list)
    assert len(drift_captures) == 1

    slug = "drift_capture_start_us0_end_us50_step_us10_osr4096_wiper_0x2A"
    csv_path = output_dir / f"{slug}.csv"
    json_path = output_dir / f"{slug}.json"
    plot_path = output_dir / f"{slug}.png"

    assert csv_path.exists(), "drift capture CSV should be written"
    assert json_path.exists(), "drift capture JSON summary should be written"
    assert plot_path.exists(), "drift capture plot should be rendered"

    summary = json.loads(json_path.read_text(encoding="utf-8"))
    assert summary["warnings"] == ["buffer_overflow", "saturation"]
    assert summary["metadata"]["osr"] == 4096
    assert summary["metadata"]["wiper_code"] == "0x2A"


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
