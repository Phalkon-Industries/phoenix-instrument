"""Command-line interface for Phoenix benchmark orchestration."""

from __future__ import annotations

import argparse
import datetime as _dt
import sys
import time
from pathlib import Path
from typing import Iterable, List

if (
    __package__ is None or __package__ == ""
):  # pragma: no cover - executed in script mode
    package_root = Path(__file__).resolve().parents[1]
    if str(package_root) not in sys.path:
        sys.path.insert(0, str(package_root))

import serial

from phoenix_benchmark.report import create_report
from phoenix_benchmark.schema import BenchmarkCommand, load_command_plan


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="phoenix-benchmark",
        description=(
            "Execute Phoenix benchmark plans against connected hardware, capture the serial transcript, "
            "and generate an automated Markdown report with plots."
        ),
    )
    parser.add_argument(
        "plan", type=Path, help="Path to a JSON command plan to validate"
    )
    parser.add_argument(
        "--port",
        dest="port",
        default=None,
        help="Serial port that exposes the Phoenix benchmark firmware (for example, COM9 or /dev/ttyUSB0).",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Parse and echo the command plan without contacting hardware.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Directory for generated reports. Defaults to ~/Downloads/phoenix-benchmark/<timestamp>.",
    )
    parser.add_argument(
        "--ready-timeout",
        type=float,
        default=DEFAULT_READY_TIMEOUT_SECONDS,
        help=(
            "Seconds to wait for the firmware '# ready' banner before aborting. "
            "Use 0 for an immediate failure if no data is received."
        ),
    )
    parser.add_argument(
        "--command-timeout",
        type=float,
        default=DEFAULT_COMMAND_TIMEOUT_SECONDS,
        help=(
            "Seconds to wait for benchmark output between serial lines before aborting. "
            "Applies separately to each command in the plan."
        ),
    )
    return parser


def _render_preview(commands: Iterable[BenchmarkCommand]) -> str:
    lines: List[str] = []
    for index, command in enumerate(commands, start=1):
        descriptor = command.to_serial_line().strip()
        lines.append(f"{index:02d}: {descriptor}")
    return "\n".join(lines)


DEFAULT_READY_TIMEOUT_SECONDS = 30.0
DEFAULT_COMMAND_TIMEOUT_SECONDS = 180.0


def main(argv: Iterable[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(list(argv) if argv is not None else None)

    commands = load_command_plan(args.plan)
    preview = _render_preview(commands)

    sys.stdout.write("# phoenix benchmark command plan\n")
    sys.stdout.write(preview + "\n")

    if args.dry_run:
        return 0

    if args.port is None:
        parser.error("--port is required unless --dry-run is supplied")

    ready_timeout = max(0.0, float(args.ready_timeout))
    command_timeout = max(0.0, float(args.command_timeout))

    output_dir = _resolve_output_dir(args.output)
    transcript: List[str] = []

    try:
        with serial.Serial(args.port, 115200, timeout=1.0) as connection:
            _request_ready_prompt(connection)
            _await_ready_prompt(connection, transcript, ready_timeout)
            _execute_plan(connection, commands, transcript, command_timeout)
    except serial.SerialException as exc:  # pragma: no cover - sanity guard
        sys.stderr.write(f"# ERROR: {exc}\n")
        return 1
    except KeyboardInterrupt:  # pragma: no cover - user initiated
        sys.stderr.write("# aborted_by_user\n")
        return 130

    artifacts = create_report(transcript, args.plan, output_dir)
    sys.stdout.write(f"# report_written,{artifacts.report_markdown_path}\n")

    return 0


def _await_ready_prompt(
    connection: serial.Serial, transcript: List[str], ready_timeout: float
) -> None:
    deadline = (
        time.monotonic() + ready_timeout if ready_timeout > 0 else time.monotonic()
    )
    while True:
        line = connection.readline()
        if not line:
            if ready_timeout == 0 or (
                ready_timeout > 0 and time.monotonic() > deadline
            ):
                raise serial.SerialException(
                    "Timed out waiting for ready prompt from device"
                )
            continue
        decoded = line.decode("utf-8", errors="replace").strip()
        sys.stdout.write(decoded + "\n")
        transcript.append(decoded)
        if decoded.endswith("ready"):
            break
    deadline = (
        time.monotonic() + ready_timeout if ready_timeout > 0 else time.monotonic()
    )


def _execute_plan(
    connection: serial.Serial,
    commands: Iterable[BenchmarkCommand],
    transcript: List[str],
    command_timeout: float,
) -> None:
    for command in commands:
        payload = command.to_serial_line().encode("utf-8")
        connection.write(payload)
        connection.flush()
        _stream_until_complete(connection, transcript, command_timeout)


def _stream_until_complete(
    connection: serial.Serial, transcript: List[str], command_timeout: float
) -> None:
    deadline = (
        time.monotonic() + command_timeout if command_timeout > 0 else time.monotonic()
    )
    while True:
        line = connection.readline()
        if not line:
            if command_timeout == 0 or (
                command_timeout > 0 and time.monotonic() > deadline
            ):
                raise serial.SerialException(
                    "Timed out waiting for benchmark completion"
                )
            continue
        decoded = line.decode("utf-8", errors="replace").strip()
        sys.stdout.write(decoded + "\n")
        transcript.append(decoded)
        if decoded.endswith("benchmark_complete"):
            break
    deadline = (
        time.monotonic() + command_timeout if command_timeout > 0 else time.monotonic()
    )


def _request_ready_prompt(connection: serial.Serial) -> None:
    # Send a blank line to prompt the firmware to re-emit its ready banner when
    # the host attaches after an earlier session.
    connection.write(b"\n")
    connection.flush()


def _resolve_output_dir(requested: Path | None) -> Path:
    if requested is not None:
        return requested

    downloads = Path.home() / "Downloads"
    timestamp = _dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    if downloads.exists():
        return downloads / "phoenix-benchmark" / timestamp
    return Path.cwd() / "phoenix-benchmark-reports" / timestamp


if __name__ == "__main__":
    raise SystemExit(main())
