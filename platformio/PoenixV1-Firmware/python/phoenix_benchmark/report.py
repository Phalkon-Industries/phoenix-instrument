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
    "OsrSweepSummaryRow",
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
class OsrSweepSummaryRow:
    label: str
    osr_value: int | None
    sample_count: int
    drain_mean: float | None
    drain_std: float | None
    drain_min: float | None
    drain_max: float | None
    led1_mean: float | None
    led1_std: float | None
    led1_min: float | None
    led1_max: float | None
    led2_mean: float | None
    led2_std: float | None
    led2_min: float | None
    led2_max: float | None
    sweep_duration_us: int | None
    has_metrics: bool

    def to_dict(self) -> dict[str, object | None]:
        return {
            "label": self.label,
            "osr_value": self.osr_value,
            "sample_count": self.sample_count,
            "drain_mean": self.drain_mean,
            "drain_std": self.drain_std,
            "drain_min": self.drain_min,
            "drain_max": self.drain_max,
            "led1_mean": self.led1_mean,
            "led1_std": self.led1_std,
            "led1_min": self.led1_min,
            "led1_max": self.led1_max,
            "led2_mean": self.led2_mean,
            "led2_std": self.led2_std,
            "led2_min": self.led2_min,
            "led2_max": self.led2_max,
            "sweep_duration_us": self.sweep_duration_us,
            "has_metrics": self.has_metrics,
        }


@dataclass(frozen=True)
class ReportArtifacts:
    output_dir: Path
    transcript_path: Path
    summary_json_path: Path
    plot_path: Path
    plot_paths: Dict[str, List[Path]]
    report_markdown_path: Path
    scenarios: List[str]


@dataclass(frozen=True)
class _SummarySection:
    scenario: str
    header: str
    rows: List[str]
    metadata: Dict[str, str]

    def table_lines(self) -> List[str]:
        return [self.header, *self.rows]


ParsedSummary = Union[SummaryRow, AdcSpeedSummaryRow, OsrSweepSummaryRow]


def _extract_metadata(line: str) -> Dict[str, str]:
    trimmed = line.lstrip("# ").strip()
    tokens = [token.strip() for token in trimmed.split(",")]
    metadata: Dict[str, str] = {}
    for token in tokens:
        if not token or "=" not in token:
            continue
        key, value = token.split("=", 1)
        if key == "scenario":
            continue
        metadata[key] = value
    return metadata


def _build_scenario_slug(scenario: str, metadata: Dict[str, str]) -> str:
    if scenario != "osr_sweep" or not metadata:
        return scenario

    parts = [scenario]
    for key in ("pot", "dwell_us", "sweeps"):
        value = metadata.get(key)
        if value is None:
            continue
        sanitized = value.replace("/", "_").replace(" ", "_")
        parts.append(f"{key}{sanitized}")
    return "_".join(parts)


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

    scenario_slug_map: Dict[str, str] = {}
    scenario_slug_order: List[str] = []
    for section in sections:
        slug = _build_scenario_slug(section.scenario, section.metadata)
        if section.scenario not in scenario_slug_map:
            scenario_slug_map[section.scenario] = slug
        if slug not in scenario_slug_order:
            scenario_slug_order.append(slug)
    if not scenario_slug_order:
        scenario_slug_order = [scenario_order[0]]
        scenario_slug_map.setdefault(scenario_order[0], scenario_order[0])

    suffix = "_".join(scenario_slug_order)

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

    plot_paths: Dict[str, List[Path]] = {}
    if "channel_map" in summaries:
        channel_rows = [
            row for row in summaries["channel_map"] if isinstance(row, SummaryRow)
        ]
        slug = scenario_slug_map.get("channel_map", "channel_map")
        plot_path = output_dir / f"{slug}.png"
        _render_channel_plot(channel_rows, plot_path)
        plot_paths["channel_map"] = [plot_path]

    if "adc_speed" in summaries:
        adc_rows = [
            row for row in summaries["adc_speed"] if isinstance(row, AdcSpeedSummaryRow)
        ]
        slug = scenario_slug_map.get("adc_speed", "adc_speed")
        adc_plot_path = output_dir / f"{slug}.png"
        _render_adc_speed_plot(adc_rows, adc_plot_path)
        plot_paths["adc_speed"] = [adc_plot_path]

    if "osr_sweep" in summaries:
        osr_rows = [
            row for row in summaries["osr_sweep"] if isinstance(row, OsrSweepSummaryRow)
        ]
        slug = scenario_slug_map.get("osr_sweep", "osr_sweep")
        osr_plot_paths = _render_osr_sweep_plots(osr_rows, output_dir, slug)
        plot_paths["osr_sweep"] = osr_plot_paths

    fallback_plot_path: Path | None = None
    if not plot_paths and scenario_order:
        fallback_slug = scenario_slug_order[0]
        fallback_key = scenario_order[0]
        fallback_plot_path = output_dir / f"{fallback_slug}_plot.png"
        _render_channel_plot([], fallback_plot_path)
        plot_paths[fallback_key] = [fallback_plot_path]

    primary_candidates = [paths[0] for paths in plot_paths.values() if paths]
    if not primary_candidates:
        raise RuntimeError("No plots generated for report")
    primary_plot_path = primary_candidates[0]

    report_markdown_path = output_dir / "report.md"
    plot_relatives = {
        key: [path.relative_to(output_dir) for path in paths]
        for key, paths in plot_paths.items()
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
        plot_paths=plot_paths,
        report_markdown_path=report_markdown_path,
        scenarios=scenario_order,
    )


def _collect_summary_sections(lines: List[str]) -> List[_SummarySection]:
    sections: List[_SummarySection] = []
    current_scenario = "summary"
    current_metadata: Dict[str, str] = {}
    collecting = False
    header: str | None = None
    rows: List[str] = []

    def flush() -> None:
        nonlocal header, rows, collecting
        if collecting and header is not None:
            sections.append(
                _SummarySection(
                    current_scenario, header, rows.copy(), current_metadata.copy()
                )
            )
        header = None
        rows = []
        collecting = False

    for raw in lines:
        line = raw.rstrip("\n")

        if line.startswith("# running,scenario="):
            scenario = _extract_scenario(line)
            if scenario:
                current_scenario = scenario
                current_metadata = _extract_metadata(line)

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
        elif section.header.startswith("Value"):
            rows = _parse_osr_sweep_rows(section)
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


def _parse_osr_sweep_rows(section: _SummarySection) -> List[OsrSweepSummaryRow]:
    parsed: List[OsrSweepSummaryRow] = []
    for entry in section.rows:
        data = entry.strip()
        if not data:
            continue

        parts = data.split()
        if len(parts) != 15:
            raise ValueError(f"Invalid osr_sweep summary row: '{entry}'")

        label = parts[0]
        sample_count = _parse_int(parts[1])
        metric_tokens = parts[2:14]
        sweep_token = parts[14]

        metrics = [_parse_float(token) for token in metric_tokens]
        sweep_duration = (
            _parse_int(sweep_token) if sweep_token not in {"", "--"} else None
        )

        has_metrics = sweep_duration is not None and all(
            value is not None for value in metrics
        )

        osr_value: int | None
        label_upper = label.upper()
        if label_upper.startswith("OSR"):
            try:
                osr_value = int(label_upper[3:])
            except ValueError:
                osr_value = None
        else:
            osr_value = None

        parsed.append(
            OsrSweepSummaryRow(
                label=label,
                osr_value=osr_value,
                sample_count=sample_count,
                drain_mean=metrics[0],
                drain_std=metrics[1],
                drain_min=metrics[2],
                drain_max=metrics[3],
                led1_mean=metrics[4],
                led1_std=metrics[5],
                led1_min=metrics[6],
                led1_max=metrics[7],
                led2_mean=metrics[8],
                led2_std=metrics[9],
                led2_min=metrics[10],
                led2_max=metrics[11],
                sweep_duration_us=sweep_duration,
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


def _render_placeholder_plot(message: str, output_path: Path) -> None:
    fig, ax = plt.subplots(figsize=(8, 4))
    ax.set_title(message)
    ax.axis("off")
    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _render_adc_speed_plot(rows: List[AdcSpeedSummaryRow], output_path: Path) -> None:
    metric_rows = [row for row in rows if row.samples_per_second is not None]

    if not metric_rows:
        _render_placeholder_plot("No adc_speed metrics available", output_path)
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
        _render_placeholder_plot("No channel metrics available", output_path)
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


def _render_osr_sweep_plots(
    rows: List[OsrSweepSummaryRow], output_dir: Path, slug: str
) -> List[Path]:
    stddev_path = output_dir / f"{slug}_stddev.png"
    duration_path = output_dir / f"{slug}_duration.png"
    _render_osr_standard_deviation_plot(rows, stddev_path)
    _render_osr_duration_plot(rows, duration_path)
    return [stddev_path, duration_path]


def _sort_osr_rows(rows: List[OsrSweepSummaryRow]) -> List[OsrSweepSummaryRow]:
    return sorted(
        rows,
        key=lambda row: (row.osr_value if row.osr_value is not None else float("inf")),
    )


def _render_osr_standard_deviation_plot(
    rows: List[OsrSweepSummaryRow], output_path: Path
) -> None:
    sorted_rows = [row for row in _sort_osr_rows(rows) if row.osr_value is not None]
    drain_points = [
        (row.osr_value, row.drain_std)
        for row in sorted_rows
        if row.drain_std is not None
    ]
    led1_points = [
        (row.osr_value, row.led1_std) for row in sorted_rows if row.led1_std is not None
    ]
    led2_points = [
        (row.osr_value, row.led2_std) for row in sorted_rows if row.led2_std is not None
    ]

    if not (drain_points or led1_points or led2_points):
        _render_placeholder_plot("No OSR sweep metrics available", output_path)
        return

    fig, ax_drain = plt.subplots(figsize=(10, 6))
    ax_led1 = ax_drain.twinx()
    ax_led2 = ax_drain.twinx()
    ax_led2.spines["right"].set_position(("axes", 1.15))
    ax_led2.set_frame_on(True)
    ax_led2.patch.set_visible(False)

    handles = []
    labels = []

    if drain_points:
        x_vals, y_vals = zip(*drain_points)
        (line_drain,) = ax_drain.plot(
            x_vals,
            y_vals,
            marker="o",
            color="#4C72B0",
            label="Drain σ",
        )
        handles.append(line_drain)
        labels.append("Drain σ")
        ax_drain.set_ylabel("Drain σ", color="#4C72B0")
        ax_drain.tick_params(axis="y", colors="#4C72B0")
    else:
        ax_drain.set_ylabel("Drain σ (n/a)", color="#4C72B0")
        ax_drain.tick_params(axis="y", colors="#4C72B0")
        ax_drain.set_yticks([])

    if led1_points:
        x_vals, y_vals = zip(*led1_points)
        (line_led1,) = ax_led1.plot(
            x_vals,
            y_vals,
            marker="s",
            color="#55A868",
            label="LED1 σ",
        )
        handles.append(line_led1)
        labels.append("LED1 σ")
        ax_led1.set_ylabel("LED1 σ", color="#55A868")
        ax_led1.tick_params(axis="y", colors="#55A868")
    else:
        ax_led1.set_ylabel("LED1 σ (n/a)", color="#55A868")
        ax_led1.tick_params(axis="y", colors="#55A868")
        ax_led1.set_yticks([])

    if led2_points:
        x_vals, y_vals = zip(*led2_points)
        (line_led2,) = ax_led2.plot(
            x_vals,
            y_vals,
            marker="^",
            color="#C44E52",
            label="LED2 σ",
        )
        handles.append(line_led2)
        labels.append("LED2 σ")
        ax_led2.set_ylabel("LED2 σ", color="#C44E52")
        ax_led2.tick_params(axis="y", colors="#C44E52")
    else:
        ax_led2.set_ylabel("LED2 σ (n/a)", color="#C44E52")
        ax_led2.tick_params(axis="y", colors="#C44E52")
        ax_led2.set_yticks([])

    x_axis = [row.osr_value for row in sorted_rows if row.osr_value is not None]
    ax_drain.set_xlabel("OSR setting")
    if x_axis:
        ax_drain.set_xticks(x_axis)
    ax_drain.set_title("OSR sweep standard deviation")
    ax_drain.grid(axis="both", linestyle="--", alpha=0.4)

    if handles:
        ax_drain.legend(handles, labels, loc="upper right")

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _render_osr_duration_plot(
    rows: List[OsrSweepSummaryRow], output_path: Path
) -> None:
    duration_rows = [
        row
        for row in _sort_osr_rows(rows)
        if row.osr_value is not None and row.sweep_duration_us is not None
    ]

    if not duration_rows:
        _render_placeholder_plot("No OSR sweep duration data", output_path)
        return

    x_vals = [row.osr_value for row in duration_rows if row.osr_value is not None]
    y_vals = [row.sweep_duration_us for row in duration_rows]

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(x_vals, y_vals, marker="o", color="#4C72B0")
    ax.set_xlabel("OSR setting")
    ax.set_ylabel("Sweep duration (us)")
    ax.set_title("OSR sweep duration per setting")
    ax.grid(axis="both", linestyle="--", alpha=0.4)
    ax.set_xticks(x_vals)

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _render_markdown_report(
    plan_path: Path,
    sections: List[_SummarySection],
    plot_paths: Dict[str, List[Path]],
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
                for index, path in enumerate(plot_paths[section.scenario], start=1):
                    plot_name = path.as_posix()
                    suffix = (
                        f" #{index}" if len(plot_paths[section.scenario]) > 1 else ""
                    )
                    lines.append(f"![{section.scenario} plot{suffix}]({plot_name})")
                lines.append("")
    else:
        lines.append("## Summary Table")
        lines.append("_(no summary available)_")
        lines.append("")
    return "\n".join(lines)
