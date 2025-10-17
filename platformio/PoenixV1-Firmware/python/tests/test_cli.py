from __future__ import annotations

import json
from pathlib import Path

from typing import List

import pytest  # type: ignore

from phoenix_benchmark.cli import main


class DummySerial:
    last_instance: "DummySerial | None" = None

    def __init__(self, *args, **kwargs) -> None:
        self.args = args
        self.kwargs = kwargs
        self.written: List[bytes] = []
        self._line_queue: List[bytes] = [
            b"# phoenix benchmark ready\n",
            b"# summary_table\n",
            b"State      Samples      Mean_A      Std_A      Min_A      Max_A      Mean_B      Std_B      Min_B      Max_B   Channel_Map\n",
            b"LED1          10      123.000        2.000      120.000      130.000      321.000        3.000      310.000      330.000   A=OK        \n",
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
                "led1_mean": 2.345,
                "led1_std": 0.567,
                "led1_min": 2.123,
                "led1_max": 2.789,
                "led2_mean": 3.456,
                "led2_std": 0.678,
                "led2_min": 3.210,
                "led2_max": 3.890,
                "sweep_us": 12345,
            }
            return (
                f"{label:<10}{sweeps:>9}  "
                f"{values['drain_mean']:>10.3f}  {values['drain_std']:>10.3f}  "
                f"{values['drain_min']:>10.3f}  {values['drain_max']:>10.3f}  "
                f"{values['led1_mean']:>10.3f}  {values['led1_std']:>10.3f}  "
                f"{values['led1_min']:>10.3f}  {values['led1_max']:>10.3f}  "
                f"{values['led2_mean']:>10.3f}  {values['led2_std']:>10.3f}  "
                f"{values['led2_min']:>10.3f}  {values['led2_max']:>10.3f}  "
                f"{values['sweep_us']:>10d}"
            )

        row = format_row().encode("utf-8") + b"\n"
        header = (
            "Value      Samples  Drain_Mean  Drain_Std  Drain_Min  Drain_Max  "
            "LED1_Mean  LED1_Std  LED1_Min  LED1_Max  LED2_Mean  LED2_Std  "
            "LED2_Min  LED2_Max  Sweep_us"
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
                            "dwell_us": 10,
                            "wiper_code": 170,
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

    def fake_create_report(lines, plan_path, output_dir):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = output_dir
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
    assert b'"wiper_code":170' in instance.written[1]

    assert captured["plan"] == plan
    assert captured["output"] == output_dir
    recorded_lines = captured["lines"]
    assert isinstance(recorded_lines, list)
    assert "# summary_table" in recorded_lines


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

    def fake_create_report(lines, plan_path, output_dir):  # type: ignore[no-untyped-def]
        captured["lines"] = list(lines)
        captured["plan"] = plan_path
        captured["output"] = output_dir
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

    def fake_create_report(*args, **kwargs):  # type: ignore[no-untyped-def]
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
