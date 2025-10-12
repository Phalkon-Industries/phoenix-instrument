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

    exit_code = main([str(plan), "--port", "COM6", "--ready-timeout", "2", "--command-timeout", "5"])
    assert exit_code == 1
    assert report_called is False

    captured = capsys.readouterr()
    assert "# error,channel_map_parse_failed" in captured.out
    assert "Device reported error" in captured.err
