"""Reporting utilities for the Phoenix benchmark workflow."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Union

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

LABEL_WIDTH = 8
SAMPLES_WIDTH = 9
CHANNEL_WIDTH = 12
MAP_WIDTH = 12

__all__ = [
    "AdcSpeedSummaryRow",
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
            "channel_alignment": self.channel_alignment,
            "has_channel_metrics": self.has_channel_metrics,
        }


@dataclass(frozen=True)
class AdcSpeedSummaryRow:
    mode: str
    samples_per_second: float | None
    loop_microseconds: float | None
    error_count: int | None
    notes: str | None
    has_metrics: bool

    def to_dict(self) -> dict[str, object | None]:
        return {
            "mode": self.mode,
            "samples_per_second": self.samples_per_second,
            "loop_microseconds": self.loop_microseconds,
            "error_count": self.error_count,
            "notes": self.notes,
            "has_metrics": self.has_metrics,
        }


@dataclass(frozen=True)
class ReportArtifacts:
    output_dir: Path
    transcript_path: Path
    summary_json_path: Path
    plot_path: Path
    plot_paths: Dict[str, Path]
    report_markdown_path: Path
    scenarios: List[str]


@dataclass(frozen=True)
class _SummarySection:
    scenario: str
    header: str
    rows: List[str]

    def table_lines(self) -> List[str]:
        return [self.header, *self.rows]


ParsedSummary = Union[SummaryRow, AdcSpeedSummaryRow]


def parse_summary_table(lines: Iterable[str]) -> Dict[str, List[ParsedSummary]]:
    sections = _collect_summary_sections(list(lines))
    return _parse_sections(sections)


def create_report(
    lines: Iterable[str], plan_path: Path, output_dir: Path
) -> ReportArtifacts:
    output_dir.mkdir(parents=True, exist_ok=True)

    transcript_lines = list(lines)
    sections = _collect_summary_sections(transcript_lines)
    summaries = _parse_sections(sections)

    scenario_order = list(dict.fromkeys(section.scenario for section in sections))
    if not scenario_order:
        scenario_order = ["summary"]

    suffix = "_".join(scenario_order)

    transcript_path = output_dir / f"transcript_{suffix}.txt"
    transcript_path.write_text("\n".join(transcript_lines) + "\n", encoding="utf-8")

    summary_payload: List[dict[str, object]] = []
    for scenario in scenario_order:
        scenario_rows = summaries.get(scenario, [])
        row_payload = [row.to_dict() for row in scenario_rows]
        summary_payload.append({"scenario": scenario, "rows": row_payload})

    summary_json_path = output_dir / f"summary_{suffix}.json"
    summary_json_path.write_text(
        json.dumps(summary_payload, indent=2), encoding="utf-8"
    )

    plot_paths: Dict[str, Path] = {}
    if "channel_map" in summaries:
        channel_rows = [
            row for row in summaries["channel_map"] if isinstance(row, SummaryRow)
        ]
        plot_path = output_dir / "channel_map.png"
        _render_channel_plot(channel_rows, plot_path)
        plot_paths["channel_map"] = plot_path

    if "adc_speed" in summaries:
        adc_rows = [
            row for row in summaries["adc_speed"] if isinstance(row, AdcSpeedSummaryRow)
        ]
        adc_plot_path = output_dir / "adc_speed.png"
        _render_adc_speed_plot(adc_rows, adc_plot_path)
        plot_paths["adc_speed"] = adc_plot_path

    if not plot_paths and scenario_order:
        fallback_scenario = scenario_order[0]
        fallback_path = output_dir / f"{fallback_scenario}_plot.png"
        _render_channel_plot([], fallback_path)
        plot_paths[fallback_scenario] = fallback_path

    primary_plot_path = next(iter(plot_paths.values()))
    plot_relative = primary_plot_path.relative_to(output_dir)

    report_markdown_path = output_dir / "report.md"
    plot_relatives = {
        key: path.relative_to(output_dir) for key, path in plot_paths.items()
    }

    report_markdown_path.write_text(
        _render_markdown_report(plan_path, sections, plot_relatives),
        encoding="utf-8",
    )

    return ReportArtifacts(
        output_dir=output_dir,
        transcript_path=transcript_path,
        summary_json_path=summary_json_path,
        plot_path=primary_plot_path,
        report_markdown_path=report_markdown_path,
        scenarios=scenario_order,
        plot_paths=plot_paths,
    )


def _collect_summary_sections(lines: List[str]) -> List[_SummarySection]:
    sections: List[_SummarySection] = []
    current_scenario = "summary"
    collecting = False
    header: str | None = None
    rows: List[str] = []

    def flush() -> None:
        nonlocal header, rows, collecting
        if collecting and header is not None:
            sections.append(_SummarySection(current_scenario, header, rows.copy()))
        header = None
        rows = []
        collecting = False

    for raw in lines:
        line = raw.rstrip("\n")

        if line.startswith("# running,scenario="):
            scenario = _extract_scenario(line)
            if scenario:
                current_scenario = scenario

        if line.startswith("# summary_table"):
            flush()
            collecting = True
            header = None
            rows = []
            continue

        if collecting:
            if line == "":
                flush()
                continue
            if header is None:
                header = line
            else:
                rows.append(line)

    flush()
    return sections


def _parse_sections(sections: List[_SummarySection]) -> Dict[str, List[ParsedSummary]]:
    summaries: Dict[str, List[ParsedSummary]] = {}
    for section in sections:
        if section.header.startswith("State"):
            rows = _parse_channel_map_rows(section)
        elif section.header.startswith("Mode"):
            rows = _parse_adc_speed_rows(section)
        else:
            continue

        if section.scenario not in summaries:
            summaries[section.scenario] = []
        summaries[section.scenario].extend(rows)
    return summaries


def _extract_scenario(line: str) -> str | None:
    if "scenario=" not in line:
        return None
    fragment = line.split("scenario=", 1)[1]
    token = fragment.split(",", 1)[0]
    token = token.strip()
    return token or None


def _parse_channel_map_rows(section: _SummarySection) -> List[SummaryRow]:
    parsed: List[SummaryRow] = []
    for entry in section.table_lines()[1:]:
        if not entry.strip():
            continue
        parsed.append(_parse_summary_row(entry))
    return parsed


def _parse_adc_speed_rows(section: _SummarySection) -> List[AdcSpeedSummaryRow]:
    parsed: List[AdcSpeedSummaryRow] = []
    for entry in section.rows:
        data = entry.strip()
        if not data:
            continue

        parts = data.split()
        if len(parts) < 5:
            raise ValueError(f"Invalid adc_speed summary row: '{entry}'")

        mode = parts[0]
        samples = _parse_float(parts[1])
        loop = _parse_float(parts[2])
        error_token = parts[3]
        if error_token in {"", "--"}:
            error_count = None
        else:
            error_count = _parse_int(error_token)
        notes = " ".join(parts[4:]) if len(parts) > 4 else None
        has_metrics = (
            samples is not None and loop is not None and error_count is not None
        )

        parsed.append(
            AdcSpeedSummaryRow(
                mode=mode,
                samples_per_second=samples,
                loop_microseconds=loop,
                error_count=error_count,
                notes=notes,
                has_metrics=has_metrics,
            )
        )
    return parsed


def _parse_summary_row(line: str) -> SummaryRow:
    padded = line.ljust(LABEL_WIDTH + SAMPLES_WIDTH + 8 * CHANNEL_WIDTH + MAP_WIDTH)
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
    alignment = slice_field(MAP_WIDTH)

    floats = [_parse_float(value) for value in channel_values]
    mean_a, std_a, min_a, max_a, mean_b, std_b, min_b, max_b = floats

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


def _render_adc_speed_plot(rows: List[AdcSpeedSummaryRow], output_path: Path) -> None:
    metric_rows = [row for row in rows if row.samples_per_second is not None]

    if not metric_rows:
        fig, ax = plt.subplots(figsize=(8, 4))
        ax.set_title("No adc_speed metrics available")
        ax.axis("off")
        fig.tight_layout()
        fig.savefig(output_path, dpi=150)
        plt.close(fig)
        return

    labels = [row.mode for row in metric_rows]
    values = [row.samples_per_second or 0.0 for row in metric_rows]
    fig, ax = plt.subplots(figsize=(10, 5))
    bars = ax.bar(range(len(labels)), values, color="#4C72B0")
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels)
    ax.set_ylabel("Samples per second")
    ax.set_title("ADC throughput by mode")
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    for bar, row in zip(bars, metric_rows):
        if row.error_count is not None:
            annotation = f"errs: {row.error_count}"
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height(),
                annotation,
                ha="center",
                va="bottom",
                fontsize=8,
            )
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


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

    channel_a_mean = [row.mean_channel_a or 0.0 for row in metric_rows]
    channel_b_mean = [row.mean_channel_b or 0.0 for row in metric_rows]

    width = 0.35

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.bar(
        [i - width / 2 for i in indices], channel_a_mean, width=width, label="A mean"
    )
    ax.bar(
        [i + width / 2 for i in indices], channel_b_mean, width=width, label="B mean"
    )

    ax.set_xticks(list(indices))
    ax.set_xticklabels(labels)
    ax.set_ylabel("ADC code")
    ax.set_title("Channel mean comparison")
    ax.legend()
    ax.grid(axis="y", linestyle="--", alpha=0.4)
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _render_markdown_report(
    plan_path: Path, sections: List[_SummarySection], plot_paths: Dict[str, Path]
) -> str:
    lines = ["# Phoenix Benchmark Report", ""]
    lines.append(f"*Plan:* `{plan_path}`")
    lines.append("")
    if sections:
        for section in sections:
            lines.append(f"## Summary Table ({section.scenario})")
            lines.append("```")
            lines.append(section.header)
            lines.extend(section.rows)
            lines.append("```")
            if section.scenario in plot_paths:
                plot_name = plot_paths[section.scenario].as_posix()
                lines.append(f"![{section.scenario} plot]({plot_name})")
                lines.append("")
    else:
        lines.append("## Summary Table")
        lines.append("_(no summary available)_")
        lines.append("")
    return "\n".join(lines)
