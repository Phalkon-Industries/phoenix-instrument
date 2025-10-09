"""Reporting utilities for the Phoenix benchmark workflow."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

LABEL_WIDTH = 8
SAMPLES_WIDTH = 9
CHANNEL_WIDTH = 12
DURATION_WIDTH = 15
MAP_WIDTH = 12

__all__ = [
    "SummaryRow",
    "ReportArtifacts",
    "parse_summary_table",
    "create_report",
]


@dataclass(frozen=True)
class SummaryRow:
    label: str
    sample_count: int
    mean_channel_a: float | None
    std_channel_a: float | None
    min_channel_a: float | None
    max_channel_a: float | None
    mean_channel_b: float | None
    std_channel_b: float | None
    min_channel_b: float | None
    max_channel_b: float | None
    step_mean_us: float | None
    step_std_us: float | None
    step_range_us: float | None
    channel_alignment: str | None
    has_channel_metrics: bool

    def to_dict(self) -> dict[str, object]:
        return {
            "label": self.label,
            "sample_count": self.sample_count,
            "mean_channel_a": self.mean_channel_a,
            "std_channel_a": self.std_channel_a,
            "min_channel_a": self.min_channel_a,
            "max_channel_a": self.max_channel_a,
            "mean_channel_b": self.mean_channel_b,
            "std_channel_b": self.std_channel_b,
            "min_channel_b": self.min_channel_b,
            "max_channel_b": self.max_channel_b,
            "step_mean_us": self.step_mean_us,
            "step_std_us": self.step_std_us,
            "step_range_us": self.step_range_us,
            "channel_alignment": self.channel_alignment,
            "has_channel_metrics": self.has_channel_metrics,
        }


@dataclass(frozen=True)
class ReportArtifacts:
    output_dir: Path
    transcript_path: Path
    summary_json_path: Path
    plot_path: Path
    report_markdown_path: Path


def parse_summary_table(lines: Iterable[str]) -> List[SummaryRow]:
    summary_lines = _extract_summary_lines(lines)
    if not summary_lines:
        return []

    header = summary_lines[0]
    _validate_header(header)

    rows: List[SummaryRow] = []
    for line in summary_lines[1:]:
        if not line.strip():
            continue
        rows.append(_parse_summary_row(line))
    return rows


def create_report(
    lines: Iterable[str], plan_path: Path, output_dir: Path
) -> ReportArtifacts:
    output_dir.mkdir(parents=True, exist_ok=True)

    transcript_path = output_dir / "transcript.txt"
    transcript_path.write_text("\n".join(lines) + "\n", encoding="utf-8")

    summary_lines = _extract_summary_lines(lines)
    rows = parse_summary_table(lines)

    summary_json_path = output_dir / "summary.json"
    summary_json_path.write_text(
        json.dumps([row.to_dict() for row in rows], indent=2), encoding="utf-8"
    )

    plot_path = output_dir / "channel_map.png"
    _render_channel_plot(rows, plot_path)

    report_markdown_path = output_dir / "report.md"
    report_markdown_path.write_text(
        _render_markdown_report(
            plan_path, summary_lines, plot_path.relative_to(output_dir)
        ),
        encoding="utf-8",
    )

    return ReportArtifacts(
        output_dir=output_dir,
        transcript_path=transcript_path,
        summary_json_path=summary_json_path,
        plot_path=plot_path,
        report_markdown_path=report_markdown_path,
    )


def _extract_summary_lines(lines: Iterable[str]) -> List[str]:
    collecting = False
    summary: List[str] = []
    for raw in lines:
        line = raw.rstrip("\n")
        if not collecting and line.startswith("# summary_table"):
            collecting = True
            continue
        if not collecting:
            continue
        if line == "":
            break
        summary.append(line)
    return summary


def _validate_header(header: str) -> None:
    expected = (
        "State      Samples      Mean_A      Std_A      Min_A      Max_A      Mean_B      Std_B      Min_B      Max_B"
        "   Step_us_mean    Step_us_std  Step_us_range  Channel_Map"
    )
    if header.strip() != expected.strip():
        return


def _parse_summary_row(line: str) -> SummaryRow:
    padded = line.ljust(
        LABEL_WIDTH + SAMPLES_WIDTH + 8 * CHANNEL_WIDTH + 3 * DURATION_WIDTH + MAP_WIDTH
    )
    cursor = 0

    def slice_field(width: int) -> str:
        nonlocal cursor
        end = cursor + width
        segment = padded[cursor:end]
        cursor = end
        return segment.strip()

    label = slice_field(LABEL_WIDTH)
    sample_count = _parse_int(slice_field(SAMPLES_WIDTH))

    channel_values = [slice_field(CHANNEL_WIDTH) for _ in range(8)]
    duration_values = [slice_field(DURATION_WIDTH) for _ in range(3)]
    alignment = slice_field(MAP_WIDTH)

    floats = [_parse_float(value) for value in channel_values]
    mean_a, std_a, min_a, max_a, mean_b, std_b, min_b, max_b = floats

    step_mean, step_std, step_range = (_parse_float(value) for value in duration_values)

    has_channel_metrics = mean_a is not None and mean_b is not None

    return SummaryRow(
        label=label,
        sample_count=sample_count,
        mean_channel_a=mean_a,
        std_channel_a=std_a,
        min_channel_a=min_a,
        max_channel_a=max_a,
        mean_channel_b=mean_b,
        std_channel_b=std_b,
        min_channel_b=min_b,
        max_channel_b=max_b,
        step_mean_us=step_mean,
        step_std_us=step_std,
        step_range_us=step_range,
        channel_alignment=alignment if alignment else None,
        has_channel_metrics=has_channel_metrics,
    )


def _parse_int(value: str) -> int:
    try:
        return int(value)
    except ValueError as exc:  # pragma: no cover - guard against malformed logs
        raise ValueError(f"Invalid integer field in summary row: '{value}'") from exc


def _parse_float(value: str) -> float | None:
    if value in {"", "--"}:
        return None
    try:
        return float(value)
    except ValueError as exc:  # pragma: no cover
        raise ValueError(f"Invalid float field in summary row: '{value}'") from exc


def _render_channel_plot(rows: List[SummaryRow], output_path: Path) -> None:
    metric_rows = [row for row in rows if row.has_channel_metrics]

    if not metric_rows:
        fig, ax = plt.subplots(figsize=(8, 4))
        ax.set_title("No channel metrics available")
        ax.axis("off")
        fig.tight_layout()
        fig.savefig(output_path, dpi=150)
        plt.close(fig)
        return

    labels = [row.label for row in metric_rows]
    indices = range(len(labels))

    channel_a_min = [row.min_channel_a or 0.0 for row in metric_rows]
    channel_a_max = [row.max_channel_a or 0.0 for row in metric_rows]
    channel_b_min = [row.min_channel_b or 0.0 for row in metric_rows]
    channel_b_max = [row.max_channel_b or 0.0 for row in metric_rows]

    width = 0.2
    offsets = [-1.5 * width, -0.5 * width, 0.5 * width, 1.5 * width]

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.bar([i + offsets[0] for i in indices], channel_a_min, width=width, label="A min")
    ax.bar([i + offsets[1] for i in indices], channel_a_max, width=width, label="A max")
    ax.bar([i + offsets[2] for i in indices], channel_b_min, width=width, label="B min")
    ax.bar([i + offsets[3] for i in indices], channel_b_max, width=width, label="B max")

    ax.set_xticks(list(indices))
    ax.set_xticklabels(labels)
    ax.set_ylabel("ADC code")
    ax.set_title("Channel dominance summary")
    ax.legend()
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _render_markdown_report(
    plan_path: Path, summary_lines: List[str], plot_name: Path
) -> str:
    lines = ["# Phoenix Benchmark Report", ""]
    lines.append(f"*Plan:* `{plan_path}`")
    lines.append("")
    lines.append("## Summary Table")
    lines.append("```")
    lines.extend(summary_lines)
    lines.append("```")
    lines.append("")
    lines.append(f"![Channel map]({plot_name.as_posix()})")
    lines.append("")
    return "\n".join(lines)
