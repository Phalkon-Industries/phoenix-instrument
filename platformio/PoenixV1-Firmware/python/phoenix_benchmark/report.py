"""Reporting utilities for the Phoenix benchmark workflow."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, Iterable, List, Union

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import ScalarFormatter

from phoenix_benchmark.schema import (
    DRIFT_CAPTURE_ALLOWED_OSR_VALUES,
    DRIFT_CAPTURE_DEFAULT_END_US,
    DRIFT_CAPTURE_DEFAULT_START_US,
    DRIFT_CAPTURE_DEFAULT_STEP_US,
)

LABEL_WIDTH = 8
SAMPLES_WIDTH = 9
CHANNEL_WIDTH = 12
MAP_WIDTH = 12
WARNING_WIDTH = 14
POT_SWEEP_SATURATION_THRESHOLD = 7_549_746
DWELL_VARIANCE_THRESHOLD = 0.75
DWELL_SATURATION_MASK = 0x01
DWELL_WARNING_LABELS = (
    (DWELL_SATURATION_MASK, "saturation"),
    (0x02, "adc_error"),
    (0x04, "alignment"),
)

COLD_SWEEP_CHANNELS = (
    ("blue", "Blue", "#0057FF", "blue", "-"),
    ("green", "Green", "#2ECC71", "green", "-"),
    ("drain_blue", "Drain blue", "#0057FF", "db", "--"),
    ("drain_green", "Drain green", "#2ECC71", "dg", "--"),
)

DRIFT_CAPTURE_WARNING_BUFFER_OVERFLOW = 0x01
DRIFT_CAPTURE_WARNING_SATURATION = 0x02
DRIFT_CAPTURE_WARNING_RESTORE_FAILED = 0x04

DRIFT_CAPTURE_WARNING_LABELS = (
    (DRIFT_CAPTURE_WARNING_BUFFER_OVERFLOW, "buffer_overflow"),
    (DRIFT_CAPTURE_WARNING_SATURATION, "saturation"),
    (DRIFT_CAPTURE_WARNING_RESTORE_FAILED, "restore_failed"),
)

DRIFT_CAPTURE_DEFAULT_OSR = min(DRIFT_CAPTURE_ALLOWED_OSR_VALUES)

__all__ = [
    "AdcSpeedSummaryRow",
    "ColdSweepSummaryRow",
    "ColdSweepSampleRow",
    "DwellSweepSummaryRow",
    "OsrSweepSummaryRow",
    "PotSweepSummaryRow",
    "SummaryRow",
    "ReportArtifacts",
    "parse_summary_table",
    "parse_drift_capture_transcript",
    "generate_drift_capture_artifacts",
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
    warning_label: str | None
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
            "warning_label": self.warning_label,
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
class ColdSweepSummaryRow:
    channel: str
    sample_count: int
    mean: float | None
    stddev: float | None
    min_code: int | None
    max_code: int | None
    saturated: bool
    has_metrics: bool

    def to_dict(self) -> dict[str, object | None]:
        return {
            "channel": self.channel,
            "sample_count": self.sample_count,
            "mean": self.mean,
            "stddev": self.stddev,
            "min_code": self.min_code,
            "max_code": self.max_code,
            "saturated": self.saturated,
            "has_metrics": self.has_metrics,
        }


@dataclass(frozen=True)
class ColdSweepSampleRow:
    index: int
    drain_blue_code: int
    drain_green_code: int
    blue_code: int
    green_code: int
    saturation: str

    def to_dict(self) -> dict[str, object]:
        return {
            "index": self.index,
            "drain_blue_code": self.drain_blue_code,
            "drain_green_code": self.drain_green_code,
            "blue_code": self.blue_code,
            "green_code": self.green_code,
            "saturation": self.saturation,
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
    blue_mean: float | None
    blue_std: float | None
    blue_min: float | None
    blue_max: float | None
    green_mean: float | None
    green_std: float | None
    green_min: float | None
    green_max: float | None
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
            "blue_mean": self.blue_mean,
            "blue_std": self.blue_std,
            "blue_min": self.blue_min,
            "blue_max": self.blue_max,
            "green_mean": self.green_mean,
            "green_std": self.green_std,
            "green_min": self.green_min,
            "green_max": self.green_max,
            "sweep_duration_us": self.sweep_duration_us,
            "has_metrics": self.has_metrics,
        }


@dataclass(frozen=True)
class PotSweepSummaryRow:
    wiper_code: int
    blue_max_code: int
    green_max_code: int
    blue_saturated: bool
    green_saturated: bool

    def to_dict(self) -> dict[str, object]:
        return {
            "wiper_code": self.wiper_code,
            "blue_max_code": self.blue_max_code,
            "green_max_code": self.green_max_code,
            "blue_saturated": self.blue_saturated,
            "green_saturated": self.green_saturated,
        }


@dataclass(frozen=True)
class DwellSweepSummaryRow:
    dwell_us: int
    sweeps_completed: int
    drain_mean: float | None
    drain_std: float | None
    blue_mean: float | None
    blue_std: float | None
    green_mean: float | None
    green_std: float | None
    duration_us: int | None
    warning_mask: int
    has_metrics: bool

    def to_dict(self) -> dict[str, object | None]:
        return {
            "dwell_us": self.dwell_us,
            "sweeps_completed": self.sweeps_completed,
            "drain_mean": self.drain_mean,
            "drain_std": self.drain_std,
            "blue_mean": self.blue_mean,
            "blue_std": self.blue_std,
            "green_mean": self.green_mean,
            "green_std": self.green_std,
            "duration_us": self.duration_us,
            "warning_mask": self.warning_mask,
            "has_metrics": self.has_metrics,
        }


@dataclass(frozen=True)
class ReportArtifacts:
    output_dir: Path
    transcript_path: Path
    summary_json_path: Path
    plot_path: Path
    plot_paths: Dict[str, List[Path]]
    csv_paths: Dict[str, List[Path]]
    report_markdown_path: Path
    scenarios: List[str]
    pot_sweep_recommendations: Dict[str, str]
    pot_sweep_warning: str | None
    dwell_sweep_recommendations: Dict[str, object] = field(default_factory=dict)
    dwell_sweep_warning: str | None = None
    drift_capture_json_paths: Dict[str, Path] = field(default_factory=dict)
    drift_capture_csv_paths: Dict[str, Path] = field(default_factory=dict)
    drift_capture_plot_paths: Dict[str, Path] = field(default_factory=dict)


@dataclass(frozen=True)
class _SummarySection:
    scenario: str
    header: str
    rows: List[str]
    metadata: Dict[str, str]

    def table_lines(self) -> List[str]:
        return [self.header, *self.rows]


ParsedSummary = Union[
    SummaryRow,
    AdcSpeedSummaryRow,
    ColdSweepSummaryRow,
    OsrSweepSummaryRow,
    PotSweepSummaryRow,
    DwellSweepSummaryRow,
]


@dataclass(frozen=True)
class DriftCaptureSampleRow:
    blue_elapsed_us: int | None
    blue_code: int | None
    green_elapsed_us: int | None
    green_code: int | None

    def to_dict(self) -> Dict[str, int | None]:
        return {
            "blue_elapsed_us": self.blue_elapsed_us,
            "blue_code": self.blue_code,
            "green_elapsed_us": self.green_elapsed_us,
            "green_code": self.green_code,
        }


@dataclass(frozen=True)
class DriftCaptureBurst:
    index: int
    start_us: int
    end_us: int
    step_us: int
    osr: int
    wiper_code: str
    warning_mask: int
    combined_samples: List[DriftCaptureSampleRow]

    def slug(self) -> str:
        return (
            "drift_capture_start_us"
            f"{self.start_us}_end_us{self.end_us}_step_us{self.step_us}_osr{self.osr}_wiper_{self.wiper_code}"
        )

    @property
    def blue_sample_count(self) -> int:
        return sum(
            1 for row in self.combined_samples if row.blue_elapsed_us is not None
        )

    @property
    def green_sample_count(self) -> int:
        return sum(
            1 for row in self.combined_samples if row.green_elapsed_us is not None
        )

    @property
    def warning_labels(self) -> List[str]:
        return _decode_drift_warning_mask(self.warning_mask)

    @property
    def metadata(self) -> Dict[str, object]:
        return {
            "start_us": self.start_us,
            "end_us": self.end_us,
            "step_delay_us": self.step_us,
            "osr": self.osr,
            "wiper_code": self.wiper_code,
        }


@dataclass(frozen=True)
class DriftCaptureArtifactEntry:
    burst: DriftCaptureBurst
    plot: Path
    csv: Path
    json: Path


@dataclass(frozen=True)
class DriftCaptureArtifactsBundle:
    json_paths: Dict[str, Path]
    csv_paths: Dict[str, Path]
    plot_paths: Dict[str, Path]
    entries: List[DriftCaptureArtifactEntry]


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
    if scenario == "osr_sweep" and metadata:
        parts = [scenario]
        for key in ("pot", "dwell_us", "sweeps"):
            value = metadata.get(key)
            if value is None:
                continue
            sanitized = value.replace("/", "_").replace(" ", "_")
            parts.append(f"{key}{sanitized}")
        return "_".join(parts)

    if scenario == "pot_sweep" and metadata:
        parts = [scenario]
        for key in ("sweeps_per_wiper", "dwell_us", "wiper_count"):
            value = metadata.get(key)
            if value is None:
                continue
            sanitized = value.replace("/", "_").replace(" ", "_")
            parts.append(f"{key}{sanitized}")
        return "_".join(parts)

    if scenario == "dwell_sweep" and metadata:
        parts = [scenario]
        for key in ("sweeps_per_dwell", "start_us", "end_us", "step_us", "steps"):
            value = metadata.get(key)
            if value is None:
                continue
            sanitized = value.replace("/", "_").replace(" ", "_")
            parts.append(f"{key}{sanitized}")
        return "_".join(parts)

    return scenario


def _extract_pot_sweep_metadata(
    lines: Iterable[str],
) -> tuple[Dict[str, str], str | None]:
    recommendations: Dict[str, str] = {}
    warning: str | None = None
    for raw in lines:
        line = raw.strip()
        if line.startswith("# pot_sweep_recommendation"):
            tokens = _extract_metadata(line)
            led = tokens.get("led")
            wiper = tokens.get("wiper")
            if led and wiper:
                normalized = wiper.strip()
                if normalized.lower().startswith("0x") and len(normalized) > 2:
                    normalized = "0x" + normalized[2:].upper()
                recommendations[led] = normalized
        elif line.startswith("# pot_sweep_warnings"):
            tokens = _extract_metadata(line)
            reason = tokens.get("reason")
            if reason:
                warning = reason
    return recommendations, warning


def _extract_dwell_sweep_warning(lines: Iterable[str]) -> str | None:
    for raw in lines:
        line = raw.strip()
        if line.startswith("# dwell_sweep_warnings"):
            tokens = _extract_metadata(line)
            reason = tokens.get("reason")
            if reason:
                return reason
    return None


def _parse_cold_sweep_samples(lines: List[str]) -> List[ColdSweepSampleRow]:
    samples: List[ColdSweepSampleRow] = []
    index = 0
    while index < len(lines):
        line = lines[index].strip()
        if line != "# cold_sweep_samples":
            index += 1
            continue

        index += 1
        if index >= len(lines):
            break

        header_line = lines[index].strip()
        if not header_line.startswith("Index"):
            continue

        index += 1
        while index < len(lines):
            row_line = lines[index]
            stripped = row_line.strip()
            if stripped == "":
                index += 1
                break
            if stripped.startswith("#"):
                break

            parts = row_line.split()
            if len(parts) < 6:
                raise ValueError(f"Invalid cold_sweep sample row: '{row_line.strip()}'")

            saturation_token = " ".join(parts[5:]) if len(parts) > 6 else parts[5]
            samples.append(
                ColdSweepSampleRow(
                    index=int(parts[0]),
                    drain_blue_code=int(parts[1]),
                    drain_green_code=int(parts[2]),
                    blue_code=int(parts[3]),
                    green_code=int(parts[4]),
                    saturation=saturation_token.strip(),
                )
            )
            index += 1
        break

    return samples


def _extract_cold_sweep_metadata(lines: Iterable[str]) -> Dict[str, object]:
    captured_sweeps: int | None = None
    timestamp_us: int | None = None
    warning: str | None = None

    for raw in lines:
        line = raw.strip()
        if line.startswith("# cold_sweep_metadata"):
            tokens = _extract_metadata(line)
            captured_sweeps = _parse_optional_int_token(tokens.get("captured_sweeps"))
            timestamp_us = _parse_optional_int_token(tokens.get("timestamp_us"))
        elif line.startswith("# cold_sweep_warnings"):
            tokens = _extract_metadata(line)
            warning = tokens.get("reason")

    metadata: Dict[str, object] = {}
    if captured_sweeps is not None:
        metadata["captured_sweeps"] = captured_sweeps
    if timestamp_us is not None:
        metadata["timestamp_us"] = timestamp_us
    if warning is not None:
        metadata["warning"] = warning
    return metadata


def parse_summary_table(lines: Iterable[str]) -> Dict[str, List[ParsedSummary]]:
    sections = _collect_summary_sections(list(lines))
    return _parse_sections(sections)


def create_report(
    lines: Iterable[str],
    plan_path: Path,
    output_dir: Path,
    drift_captures: List[DriftCaptureBurst] | None = None,
) -> ReportArtifacts:
    output_dir.mkdir(parents=True, exist_ok=True)

    transcript_lines = list(lines)
    sections = _collect_summary_sections(transcript_lines)
    summaries = _parse_sections(sections)
    drift_bursts = (
        list(drift_captures)
        if drift_captures is not None
        else parse_drift_capture_transcript(transcript_lines)
    )

    scenario_order = list(dict.fromkeys(section.scenario for section in sections))
    if not scenario_order:
        scenario_order = ["summary"]

    if drift_bursts and "drift_capture" not in scenario_order:
        scenario_order.append("drift_capture")

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

    if drift_bursts and "drift_capture" not in scenario_slug_order:
        scenario_slug_order.append("drift_capture")
        scenario_slug_map.setdefault("drift_capture", "drift_capture")

    suffix = "_".join(scenario_slug_order)

    transcript_path = output_dir / f"transcript_{suffix}.txt"
    transcript_path.write_text("\n".join(transcript_lines) + "\n", encoding="utf-8")

    pot_recommendations, pot_warning = _extract_pot_sweep_metadata(transcript_lines)
    dwell_warning = _extract_dwell_sweep_warning(transcript_lines)
    cold_samples = _parse_cold_sweep_samples(transcript_lines)
    cold_metadata = _extract_cold_sweep_metadata(transcript_lines)
    scenario_extras: Dict[str, Dict[str, object]] = {}
    if "pot_sweep" in summaries:
        scenario_extras["pot_sweep"] = {
            "recommendations": dict(sorted(pot_recommendations.items())),
            "warning": pot_warning or "none",
            "saturation_threshold": POT_SWEEP_SATURATION_THRESHOLD,
        }

    cold_summary_rows: List[ColdSweepSummaryRow] = [
        row
        for row in summaries.get("cold_sweep", [])
        if isinstance(row, ColdSweepSummaryRow)
    ]
    cold_run_metadata: Dict[str, str] = {}
    for section in sections:
        if section.scenario == "cold_sweep":
            cold_run_metadata = section.metadata.copy()
            break

    if cold_summary_rows or cold_metadata or cold_samples:
        saturated_channels = sorted(
            {row.channel for row in cold_summary_rows if row.saturated}
        )
        cold_extras: Dict[str, object] = {
            "warning": cold_metadata.get("warning", "none"),
            "sample_count": len(cold_samples),
        }
        captured = cold_metadata.get("captured_sweeps")
        if captured is not None:
            cold_extras["captured_sweeps"] = captured
        timestamp = cold_metadata.get("timestamp_us")
        if timestamp is not None:
            cold_extras["timestamp_us"] = timestamp
        if saturated_channels:
            cold_extras["saturated_channels"] = saturated_channels
        if cold_run_metadata:
            cold_extras["run_metadata"] = dict(sorted(cold_run_metadata.items()))

        scenario_extras["cold_sweep"] = cold_extras

    dwell_rows: List[DwellSweepSummaryRow] = [
        row
        for row in summaries.get("dwell_sweep", [])
        if isinstance(row, DwellSweepSummaryRow)
    ]
    dwell_recommendations = _compute_dwell_recommendations(dwell_rows)
    if dwell_rows or dwell_warning:
        scenario_extras["dwell_sweep"] = {
            "recommendations": dwell_recommendations,
            "warning": dwell_warning or "none",
        }

    if drift_bursts:
        scenario_extras["drift_capture"] = {
            "burst_count": len(drift_bursts),
            "bursts": [
                {
                    "index": burst.index,
                    "slug": burst.slug(),
                    "warning_mask": burst.warning_mask,
                    "warning_labels": burst.warning_labels,
                    "blue_sample_count": burst.blue_sample_count,
                    "green_sample_count": burst.green_sample_count,
                    "metadata": burst.metadata,
                }
                for burst in drift_bursts
            ],
        }

    summary_payload: List[dict[str, object]] = []
    for scenario in scenario_order:
        scenario_rows = summaries.get(scenario, [])
        row_payload = [row.to_dict() for row in scenario_rows]
        summary_payload.append(
            {
                "scenario": scenario,
                "rows": row_payload,
                "extras": scenario_extras.get(scenario, {}),
            }
        )

    summary_json_path = output_dir / f"summary_{suffix}.json"
    summary_json_path.write_text(
        json.dumps(summary_payload, indent=2), encoding="utf-8"
    )

    drift_bundle = generate_drift_capture_artifacts(drift_bursts, output_dir)
    drift_json_paths = drift_bundle.json_paths
    drift_csv_paths = drift_bundle.csv_paths
    drift_plot_paths = drift_bundle.plot_paths
    drift_entries = drift_bundle.entries

    plot_paths: Dict[str, List[Path]] = {}
    csv_paths: Dict[str, List[Path]] = {}
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

    if cold_summary_rows or cold_samples:
        slug = scenario_slug_map.get("cold_sweep", "cold_sweep")
        plot_path = output_dir / f"{slug}_samples.png"
        _render_cold_sweep_plot(cold_samples, cold_summary_rows, plot_path)
        plot_paths["cold_sweep"] = [plot_path]
        csv_path = output_dir / f"{slug}_samples.csv"
        _write_cold_sweep_csv(cold_samples, csv_path)
        csv_paths.setdefault("cold_sweep", []).append(csv_path)

    if "pot_sweep" in summaries:
        pot_rows = [
            row for row in summaries["pot_sweep"] if isinstance(row, PotSweepSummaryRow)
        ]
        slug = scenario_slug_map.get("pot_sweep", "pot_sweep")
        csv_path = output_dir / f"{slug}.csv"
        _write_pot_sweep_csv(pot_rows, csv_path)
        csv_paths["pot_sweep"] = [csv_path]
        pot_plot_path = output_dir / f"{slug}_profile.png"
        _render_pot_sweep_plot(pot_rows, pot_plot_path, POT_SWEEP_SATURATION_THRESHOLD)
        plot_paths["pot_sweep"] = [pot_plot_path]

    if dwell_rows:
        slug = scenario_slug_map.get("dwell_sweep", "dwell_sweep")
        csv_path = output_dir / f"{slug}.csv"
        _write_dwell_sweep_csv(dwell_rows, csv_path)
        csv_paths["dwell_sweep"] = [csv_path]
        variance_path = output_dir / f"{slug}_variance.png"
        duration_path = output_dir / f"{slug}_duration.png"
        _render_dwell_variance_plot(dwell_rows, variance_path, DWELL_VARIANCE_THRESHOLD)
        _render_dwell_duration_plot(dwell_rows, duration_path)
        plot_paths["dwell_sweep"] = [variance_path, duration_path]

    if drift_bursts:
        plot_paths["drift_capture"] = [
            drift_plot_paths[burst.slug()]
            for burst in drift_bursts
            if burst.slug() in drift_plot_paths
        ]
        csv_paths["drift_capture"] = [
            drift_csv_paths[burst.slug()]
            for burst in drift_bursts
            if burst.slug() in drift_csv_paths
        ]

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
    csv_relatives = {
        key: [path.relative_to(output_dir) for path in paths]
        for key, paths in csv_paths.items()
    }

    report_markdown_path.write_text(
        _render_markdown_report(
            plan_path,
            sections,
            plot_relatives,
            summaries,
            scenario_extras,
            csv_relatives,
            drift_entries,
        ),
        encoding="utf-8",
    )

    return ReportArtifacts(
        output_dir=output_dir,
        transcript_path=transcript_path,
        summary_json_path=summary_json_path,
        plot_path=primary_plot_path,
        plot_paths=plot_paths,
        csv_paths=csv_paths,
        report_markdown_path=report_markdown_path,
        scenarios=scenario_order,
        pot_sweep_recommendations=dict(sorted(pot_recommendations.items())),
        pot_sweep_warning=pot_warning,
        dwell_sweep_recommendations=dwell_recommendations,
        dwell_sweep_warning=dwell_warning,
        drift_capture_json_paths=drift_json_paths,
        drift_capture_csv_paths=drift_csv_paths,
        drift_capture_plot_paths=drift_plot_paths,
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
        elif section.header.startswith("Channel"):
            rows = _parse_cold_sweep_rows(section)
        elif section.header.startswith("Value"):
            rows = _parse_osr_sweep_rows(section)
        elif section.header.startswith("Wiper"):
            rows = _parse_pot_sweep_rows(section)
        elif section.header.startswith("Dwell_us"):
            rows = _parse_dwell_sweep_rows(section)
        else:
            continue

        if section.scenario not in summaries:
            summaries[section.scenario] = []
        summaries[section.scenario].extend(rows)
    return summaries


def _decode_drift_warning_mask(mask: int) -> List[str]:
    return [label for bit, label in DRIFT_CAPTURE_WARNING_LABELS if mask & bit]


def _parse_optional_int_field(value: str) -> int | None:
    token = value.strip().lower()
    if token in {"", "--", "nan"}:
        return None
    try:
        return int(token)
    except ValueError:
        try:
            return int(token, 0)
        except ValueError as exc:  # pragma: no cover - guard for malformed logs
            raise ValueError(f"Invalid drift capture integer field: '{value}'") from exc


def _normalise_wiper_code(raw: str | None) -> str:
    if raw is None:
        return "0x00"
    value = raw.strip()
    if not value:
        return "0x00"
    if value.lower().startswith("0x"):
        return "0x" + value[2:].upper()
    try:
        numeric = int(value)
    except ValueError:
        return value
    return f"0x{numeric:02X}"


def _parse_int_token(value: str | int | None, *, default: int) -> int:
    if value is None:
        return default
    if isinstance(value, int):
        return value
    token = value.strip()
    if token == "":
        return default
    try:
        return int(token, 0)
    except ValueError as exc:  # pragma: no cover - guard for malformed logs
        raise ValueError(f"Invalid integer token '{value}'") from exc


def _format_drift_optional(value: int | None) -> str:
    return "--" if value is None else str(value)


def parse_drift_capture_transcript(lines: Iterable[str]) -> List[DriftCaptureBurst]:
    entries = list(lines)
    bursts: List[DriftCaptureBurst] = []
    index = 0
    burst_counter = 0

    while index < len(entries):
        line = entries[index]
        if not line.startswith("# drift_capture,metadata"):
            index += 1
            continue

        metadata_tokens = _extract_metadata(line)
        start_us = _parse_int_token(
            metadata_tokens.get("start_us"), default=DRIFT_CAPTURE_DEFAULT_START_US
        )
        end_us = _parse_int_token(
            metadata_tokens.get("end_us"), default=DRIFT_CAPTURE_DEFAULT_END_US
        )
        step_us = _parse_int_token(
            metadata_tokens.get("step_delay_us"), default=DRIFT_CAPTURE_DEFAULT_STEP_US
        )
        osr = _parse_int_token(
            metadata_tokens.get("osr"), default=DRIFT_CAPTURE_DEFAULT_OSR
        )
        wiper_code = _normalise_wiper_code(metadata_tokens.get("wiper_code"))

        index += 1
        if index >= len(entries):
            break
        results_line = entries[index]
        if not results_line.startswith("# drift_capture,results"):
            continue

        results_tokens = _extract_metadata(results_line)
        warning_mask = _parse_int_token(results_tokens.get("warning_mask"), default=0)

        index += 1
        if index >= len(entries):
            break

        header_line = entries[index]
        if header_line.startswith("#"):
            continue

        index += 1
        samples: List[DriftCaptureSampleRow] = []
        while index < len(entries):
            sample_line = entries[index]
            if sample_line == "":
                index += 1
                break
            if sample_line.startswith("#"):
                break
            parts = sample_line.split("\t")
            if len(parts) != 4:
                raise ValueError(f"Invalid drift capture sample row: '{sample_line}'")
            blue_elapsed = _parse_optional_int_field(parts[0])
            blue_code = _parse_optional_int_field(parts[1])
            green_elapsed = _parse_optional_int_field(parts[2])
            green_code = _parse_optional_int_field(parts[3])
            samples.append(
                DriftCaptureSampleRow(
                    blue_elapsed_us=blue_elapsed,
                    blue_code=blue_code,
                    green_elapsed_us=green_elapsed,
                    green_code=green_code,
                )
            )
            index += 1

        burst_counter += 1
        bursts.append(
            DriftCaptureBurst(
                index=burst_counter,
                start_us=start_us,
                end_us=end_us,
                step_us=step_us,
                osr=osr,
                wiper_code=wiper_code,
                warning_mask=warning_mask,
                combined_samples=samples,
            )
        )
        continue

    return bursts


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


def _parse_cold_sweep_rows(section: _SummarySection) -> List[ColdSweepSummaryRow]:
    parsed: List[ColdSweepSummaryRow] = []
    for entry in section.rows:
        data = entry.strip()
        if not data:
            continue

        parts = data.split()
        if len(parts) < 7:
            raise ValueError(f"Invalid cold_sweep summary row: '{entry}'")

        channel = parts[0]
        sample_count = _parse_int(parts[1])
        mean = _parse_float(parts[2])
        stddev = _parse_float(parts[3])
        min_code = _parse_optional_int(parts[4])
        max_code = _parse_optional_int(parts[5])
        saturated = parts[6].lower() == "yes"
        has_metrics = (
            mean is not None
            and stddev is not None
            and min_code is not None
            and max_code is not None
        )

        parsed.append(
            ColdSweepSummaryRow(
                channel=channel,
                sample_count=sample_count,
                mean=mean,
                stddev=stddev,
                min_code=min_code,
                max_code=max_code,
                saturated=saturated,
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
                blue_mean=metrics[4],
                blue_std=metrics[5],
                blue_min=metrics[6],
                blue_max=metrics[7],
                green_mean=metrics[8],
                green_std=metrics[9],
                green_min=metrics[10],
                green_max=metrics[11],
                sweep_duration_us=sweep_duration,
                has_metrics=has_metrics,
            )
        )
    return parsed


def _parse_pot_sweep_rows(section: _SummarySection) -> List[PotSweepSummaryRow]:
    parsed: List[PotSweepSummaryRow] = []
    for entry in section.rows:
        data = entry.strip()
        if not data:
            continue

        parts = data.split()
        if len(parts) != 5:
            raise ValueError(f"Invalid pot_sweep summary row: '{entry}'")

        wiper_token, blue_token, green_token, blue_sat, green_sat = parts
        try:
            wiper_code = int(wiper_token, 16)
        except ValueError as exc:
            raise ValueError(f"Invalid pot_sweep wiper code: '{wiper_token}'") from exc

        blue_max_code = _parse_int(blue_token)
        green_max_code = _parse_int(green_token)
        blue_saturated = blue_sat.lower() == "yes"
        green_saturated = green_sat.lower() == "yes"

        parsed.append(
            PotSweepSummaryRow(
                wiper_code=wiper_code,
                blue_max_code=blue_max_code,
                green_max_code=green_max_code,
                blue_saturated=blue_saturated,
                green_saturated=green_saturated,
            )
        )
    return parsed


def _parse_dwell_sweep_rows(section: _SummarySection) -> List[DwellSweepSummaryRow]:
    parsed: List[DwellSweepSummaryRow] = []
    for entry in section.rows:
        data = entry.strip()
        if not data:
            continue

        parts = data.split()
        if len(parts) != 10:
            raise ValueError(f"Invalid dwell_sweep summary row: '{entry}'")

        dwell_us = _parse_int(parts[0])
        sweeps_completed = _parse_int(parts[1])
        metric_tokens = parts[2:8]
        duration_token = parts[8]
        warning_token = parts[9]

        metrics = [_parse_float(token) for token in metric_tokens]
        has_metrics = all(value is not None for value in metrics)

        duration_us: int | None
        if duration_token in {"", "--"}:
            duration_us = None
        else:
            duration_us = _parse_int(duration_token)

        try:
            if warning_token.lower().startswith("0x"):
                warning_mask = int(warning_token, 16)
            else:
                warning_mask = _parse_int(warning_token)
        except ValueError as exc:
            raise ValueError(
                f"Invalid dwell_sweep warning mask: '{warning_token}'"
            ) from exc

        drain_mean, drain_std, blue_mean, blue_std, green_mean, green_std = metrics

        parsed.append(
            DwellSweepSummaryRow(
                dwell_us=dwell_us,
                sweeps_completed=sweeps_completed,
                drain_mean=drain_mean,
                drain_std=drain_std,
                blue_mean=blue_mean,
                blue_std=blue_std,
                green_mean=green_mean,
                green_std=green_std,
                duration_us=duration_us,
                warning_mask=warning_mask,
                has_metrics=has_metrics,
            )
        )
    return parsed


def _max_std(row: DwellSweepSummaryRow) -> float | None:
    values = [row.drain_std, row.blue_std, row.green_std]
    metrics = [value for value in values if value is not None]
    if not metrics:
        return None
    return max(metrics)


def _compute_dwell_recommendations(
    rows: List[DwellSweepSummaryRow],
) -> Dict[str, object]:
    metric_rows = [row for row in rows if row.has_metrics]
    if not metric_rows:
        return {}

    stable_rows = [row for row in metric_rows if row.warning_mask == 0]
    if not stable_rows:
        return {}

    def candidate_key(row: DwellSweepSummaryRow) -> tuple[int, float]:
        metric = _max_std(row)
        return (row.dwell_us, metric if metric is not None else float("inf"))

    compliant_rows = [
        row
        for row in stable_rows
        if (_max_std(row) is not None and _max_std(row) <= DWELL_VARIANCE_THRESHOLD)
    ]

    if compliant_rows:
        recommended = min(compliant_rows, key=candidate_key)
        stable_dwells = sorted({row.dwell_us for row in compliant_rows})
    else:
        recommended = min(stable_rows, key=candidate_key)
        stable_dwells = sorted({row.dwell_us for row in stable_rows})

    return {
        "recommended": recommended.dwell_us,
        "stable_dwells": stable_dwells,
        "threshold": DWELL_VARIANCE_THRESHOLD,
    }


def _decode_dwell_warning_mask(mask: int) -> str:
    labels = [label for bit, label in DWELL_WARNING_LABELS if mask & bit]
    return "|".join(labels) if labels else "none"


def _parse_summary_row(line: str) -> SummaryRow:
    padded = line.ljust(
        LABEL_WIDTH + SAMPLES_WIDTH + 8 * CHANNEL_WIDTH + MAP_WIDTH + WARNING_WIDTH
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
    alignment = slice_field(MAP_WIDTH)
    warning_field = slice_field(WARNING_WIDTH)

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
        warning_label=warning_field if warning_field else None,
        has_channel_metrics=has_channel_metrics,
    )


def _parse_int(value: str) -> int:
    try:
        return int(value)
    except ValueError as exc:  # pragma: no cover - guard against malformed logs
        raise ValueError(f"Invalid integer field in summary row: '{value}'") from exc


def _parse_optional_int(value: str) -> int | None:
    token = value.strip()
    if token in {"", "--"}:
        return None
    try:
        return int(token)
    except ValueError as exc:  # pragma: no cover - guard against malformed logs
        raise ValueError(f"Invalid integer field in summary row: '{value}'") from exc


def _parse_optional_int_token(value: str | None) -> int | None:
    if value is None:
        return None
    return _parse_optional_int(value)


def _parse_float(value: str) -> float | None:
    if value in {"", "--"}:
        return None
    try:
        return float(value)
    except ValueError as exc:  # pragma: no cover
        raise ValueError(f"Invalid float field in summary row: '{value}'") from exc


def _format_optional_float(value: float | None, digits: int = 3) -> str:
    if value is None:
        return "--"
    return f"{value:.{digits}f}"


def _format_optional_int(value: int | None) -> str:
    if value is None:
        return "--"
    return str(value)


def _format_optional_text(value: str | None) -> str:
    return value if value else "--"


def _apply_osr_log_axis(axes: Iterable[plt.Axes], values: List[int]) -> None:
    if not values:
        return

    for axis in axes:
        axis.set_xscale("log", base=2)
        axis.set_xticks(values)
        formatter = ScalarFormatter()
        formatter.set_scientific(False)
        axis.xaxis.set_major_formatter(formatter)
        axis.tick_params(axis="x", labelrotation=45)


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


def _render_cold_sweep_plot(
    samples: List[ColdSweepSampleRow],
    summary_rows: List[ColdSweepSummaryRow],
    output_path: Path,
) -> None:
    has_samples = bool(samples)
    metric_rows = [row for row in summary_rows if row.has_metrics]
    has_metrics = bool(metric_rows)

    if not has_samples:
        message = (
            "Cold sweep captured summary metrics only"
            if has_metrics
            else "No cold_sweep data available"
        )
        _render_placeholder_plot(message, output_path)
        return

    fig, axes = plt.subplots(
        len(COLD_SWEEP_CHANNELS),
        1,
        figsize=(10, 2.5 * len(COLD_SWEEP_CHANNELS)),
        sharex=True,
    )

    if isinstance(axes, plt.Axes):
        axes_list = [axes]
    else:
        axes_iterable = getattr(axes, "flat", axes)
        axes_list = list(axes_iterable)

    indices = [row.index for row in samples]

    for axis, (channel, label, color, token, linestyle) in zip(
        axes_list, COLD_SWEEP_CHANNELS
    ):
        attr = f"{channel}_code"
        values = [getattr(row, attr) for row in samples]
        axis.plot(
            indices,
            values,
            color=color,
            linestyle=linestyle,
            linewidth=1.5,
        )

        saturated_indices: List[int] = []
        saturated_values: List[int] = []
        for row, value in zip(samples, values):
            saturation_tokens = {
                part.strip()
                for part in row.saturation.split("|")
                if part.strip() and row.saturation.lower() != "none"
            }
            if token in saturation_tokens:
                saturated_indices.append(row.index)
                saturated_values.append(value)

        if saturated_indices:
            axis.scatter(
                saturated_indices,
                saturated_values,
                color=color,
                edgecolors="black",
                zorder=3,
                marker="o",
                s=25,
            )

        axis.set_ylabel("ADC code")
        axis.set_title(label)
        axis.grid(axis="both", linestyle="--", alpha=0.4)
        axis.label_outer()

    axes_list[-1].set_xlabel("Sweep index")

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
    blue_points = [
        (row.osr_value, row.blue_std) for row in sorted_rows if row.blue_std is not None
    ]
    green_points = [
        (row.osr_value, row.green_std)
        for row in sorted_rows
        if row.green_std is not None
    ]

    if not (drain_points or blue_points or green_points):
        _render_placeholder_plot("No OSR sweep metrics available", output_path)
        return

    fig, ax_drain = plt.subplots(figsize=(10, 6))
    ax_blue = ax_drain.twinx()
    ax_green = ax_drain.twinx()
    ax_green.spines["right"].set_position(("axes", 1.15))
    ax_green.set_frame_on(True)
    ax_green.patch.set_visible(False)

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

    if blue_points:
        x_vals, y_vals = zip(*blue_points)
        (line_blue,) = ax_blue.plot(
            x_vals,
            y_vals,
            marker="s",
            color="#55A868",
            label="Blue σ",
        )
        handles.append(line_blue)
        labels.append("Blue σ")
        ax_blue.set_ylabel("Blue σ", color="#55A868")
        ax_blue.tick_params(axis="y", colors="#55A868")
    else:
        ax_blue.set_ylabel("Blue σ (n/a)", color="#55A868")
        ax_blue.tick_params(axis="y", colors="#55A868")
        ax_blue.set_yticks([])

    if green_points:
        x_vals, y_vals = zip(*green_points)
        (line_green,) = ax_green.plot(
            x_vals,
            y_vals,
            marker="^",
            color="#C44E52",
            label="Green σ",
        )
        handles.append(line_green)
        labels.append("Green σ")
        ax_green.set_ylabel("Green σ", color="#C44E52")
        ax_green.tick_params(axis="y", colors="#C44E52")
    else:
        ax_green.set_ylabel("Green σ (n/a)", color="#C44E52")
        ax_green.tick_params(axis="y", colors="#C44E52")
        ax_green.set_yticks([])

    x_axis = [row.osr_value for row in sorted_rows if row.osr_value is not None]
    ax_drain.set_xlabel("OSR setting")
    _apply_osr_log_axis([ax_drain, ax_blue, ax_green], x_axis)
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
    _apply_osr_log_axis([ax], x_vals)

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _write_cold_sweep_csv(rows: List[ColdSweepSampleRow], output_path: Path) -> None:
    header = "index,drain_blue_code,drain_green_code,blue_code,green_code,saturation"
    lines = [header]
    for row in rows:
        lines.append(
            ",".join(
                [
                    str(row.index),
                    str(row.drain_blue_code),
                    str(row.drain_green_code),
                    str(row.blue_code),
                    str(row.green_code),
                    row.saturation,
                ]
            )
        )
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_pot_sweep_csv(rows: List[PotSweepSummaryRow], output_path: Path) -> None:
    header = "wiper_code,blue_max_code,green_max_code,blue_saturated,green_saturated"
    lines = [header]
    for row in rows:
        lines.append(
            ",".join(
                [
                    str(row.wiper_code),
                    str(row.blue_max_code),
                    str(row.green_max_code),
                    "yes" if row.blue_saturated else "no",
                    "yes" if row.green_saturated else "no",
                ]
            )
        )
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_dwell_sweep_csv(rows: List[DwellSweepSummaryRow], output_path: Path) -> None:
    header = (
        "dwell_us,sweeps_completed,drain_mean,drain_std,"
        "blue_mean,blue_std,green_mean,green_std,duration_us,warning_mask"
    )
    lines = [header]
    for row in rows:
        values = [
            str(row.dwell_us),
            str(row.sweeps_completed),
            _format_optional_float(row.drain_mean),
            _format_optional_float(row.drain_std),
            _format_optional_float(row.blue_mean),
            _format_optional_float(row.blue_std),
            _format_optional_float(row.green_mean),
            _format_optional_float(row.green_std),
            _format_optional_int(row.duration_us),
            f"0x{row.warning_mask:02X}",
        ]
        lines.append(",".join(values))
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _write_drift_capture_json(burst: DriftCaptureBurst, output_path: Path) -> None:
    payload = {
        "index": burst.index,
        "slug": burst.slug(),
        "metadata": burst.metadata,
        "warning_mask": burst.warning_mask,
        "warnings": burst.warning_labels,
        "blue_sample_count": burst.blue_sample_count,
        "green_sample_count": burst.green_sample_count,
        "samples": [row.to_dict() for row in burst.combined_samples],
    }
    output_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def _write_drift_capture_csv(burst: DriftCaptureBurst, output_path: Path) -> None:
    header = "blue_elapsed_us,blue_code,green_elapsed_us,green_code"
    lines = [header]
    for sample in burst.combined_samples:
        lines.append(
            ",".join(
                [
                    _format_drift_optional(sample.blue_elapsed_us),
                    _format_drift_optional(sample.blue_code),
                    _format_drift_optional(sample.green_elapsed_us),
                    _format_drift_optional(sample.green_code),
                ]
            )
        )
    output_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _render_drift_capture_plot(burst: DriftCaptureBurst, output_path: Path) -> None:
    blue_points = [
        (row.blue_elapsed_us, row.blue_code)
        for row in burst.combined_samples
        if row.blue_elapsed_us is not None and row.blue_code is not None
    ]
    green_points = [
        (row.green_elapsed_us, row.green_code)
        for row in burst.combined_samples
        if row.green_elapsed_us is not None and row.green_code is not None
    ]

    if not blue_points and not green_points:
        _render_placeholder_plot("No drift capture samples recorded", output_path)
        return

    fig, ax_primary = plt.subplots(figsize=(10, 5))
    legend_handles = []

    if blue_points:
        x_vals_blue, y_vals_blue = zip(*blue_points)
        (line_blue,) = ax_primary.plot(
            x_vals_blue,
            y_vals_blue,
            color="#4C72B0",
            label="Blue",
        )
        legend_handles.append(line_blue)
        ax_primary.set_ylabel("Blue ADC code")
    else:
        ax_primary.set_ylabel("ADC code")

    secondary_axis = None
    if green_points:
        x_vals_green, y_vals_green = zip(*green_points)
        if blue_points:
            secondary_axis = ax_primary.twinx()
            secondary_axis.set_ylabel("Green ADC code")
            (line_green,) = secondary_axis.plot(
                x_vals_green,
                y_vals_green,
                color="#55A868",
                label="Green",
            )
        else:
            (line_green,) = ax_primary.plot(
                x_vals_green,
                y_vals_green,
                color="#55A868",
                label="Green",
            )
            ax_primary.set_ylabel("Green ADC code")
        legend_handles.append(line_green)

    ax_primary.set_xlabel("Elapsed (us)")
    ax_primary.set_title(
        f"Drift capture burst {burst.index} (wiper {burst.wiper_code})"
    )
    ax_primary.grid(axis="both", linestyle="--", alpha=0.4)

    if legend_handles:
        legend_labels = [handle.get_label() for handle in legend_handles]
        ax_primary.legend(legend_handles, legend_labels, loc="best")

    if burst.warning_labels:
        warning_text = ", ".join(burst.warning_labels)
        ax_primary.text(
            0.02,
            0.95,
            f"Warnings: {warning_text}",
            transform=ax_primary.transAxes,
            fontsize=9,
            ha="left",
            va="top",
            bbox={"boxstyle": "round", "facecolor": "white", "alpha": 0.7},
        )

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def generate_drift_capture_artifacts(
    bursts: List[DriftCaptureBurst], output_dir: Path
) -> DriftCaptureArtifactsBundle:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_paths: Dict[str, Path] = {}
    csv_paths: Dict[str, Path] = {}
    plot_paths: Dict[str, Path] = {}
    entries: List[DriftCaptureArtifactEntry] = []

    for burst in bursts:
        slug = burst.slug()
        json_path = output_dir / f"{slug}.json"
        csv_path = output_dir / f"{slug}.csv"
        plot_path = output_dir / f"{slug}.png"
        _write_drift_capture_json(burst, json_path)
        _write_drift_capture_csv(burst, csv_path)
        _render_drift_capture_plot(burst, plot_path)
        json_paths[slug] = json_path
        csv_paths[slug] = csv_path
        plot_paths[slug] = plot_path
        entries.append(
            DriftCaptureArtifactEntry(
                burst=burst,
                plot=plot_path.relative_to(output_dir),
                csv=csv_path.relative_to(output_dir),
                json=json_path.relative_to(output_dir),
            )
        )

    return DriftCaptureArtifactsBundle(
        json_paths=json_paths,
        csv_paths=csv_paths,
        plot_paths=plot_paths,
        entries=entries,
    )


def _render_pot_sweep_plot(
    rows: List[PotSweepSummaryRow], output_path: Path, saturation_threshold: int
) -> None:
    if not rows:
        _render_placeholder_plot("No pot sweep metrics available", output_path)
        return

    sorted_rows = sorted(rows, key=lambda row: row.wiper_code)
    wipers = [row.wiper_code for row in sorted_rows]
    blue_codes = [row.blue_max_code for row in sorted_rows]
    green_codes = [row.green_max_code for row in sorted_rows]

    fig, ax = plt.subplots(figsize=(10, 6))
    ax.plot(wipers, blue_codes, marker="o", color="#4C72B0", label="Blue max")
    ax.plot(wipers, green_codes, marker="s", color="#55A868", label="Green max")
    ax.axhline(
        saturation_threshold,
        color="#C44E52",
        linestyle="--",
        label="Saturation threshold",
    )
    ax.set_xlabel("Wiper code")
    ax.set_ylabel("ADC code")
    ax.set_title("Potentiometer sweep LED headroom")
    ax.grid(axis="both", linestyle="--", alpha=0.4)
    ax.legend()

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _render_dwell_variance_plot(
    rows: List[DwellSweepSummaryRow], output_path: Path, threshold: float
) -> None:
    metric_rows = [row for row in rows if row.has_metrics]
    if not metric_rows:
        _render_placeholder_plot("No dwell sweep metrics available", output_path)
        return

    drain_points = [
        (row.dwell_us, row.drain_std)
        for row in metric_rows
        if row.drain_std is not None
    ]
    blue_points = [
        (row.dwell_us, row.blue_std) for row in metric_rows if row.blue_std is not None
    ]
    green_points = [
        (row.dwell_us, row.green_std)
        for row in metric_rows
        if row.green_std is not None
    ]

    if not (drain_points or blue_points or green_points):
        _render_placeholder_plot("No dwell sweep metrics available", output_path)
        return

    fig, ax = plt.subplots(figsize=(10, 6))

    handles = []
    labels = []

    if drain_points:
        x_vals, y_vals = zip(*drain_points)
        (line_drain,) = ax.plot(
            x_vals, y_vals, marker="o", color="#4C72B0", label="Drain σ"
        )
        handles.append(line_drain)
        labels.append("Drain σ")

    if blue_points:
        x_vals, y_vals = zip(*blue_points)
        (line_blue,) = ax.plot(
            x_vals, y_vals, marker="s", color="#55A868", label="Blue σ"
        )
        handles.append(line_blue)
        labels.append("Blue σ")

    if green_points:
        x_vals, y_vals = zip(*green_points)
        (line_green,) = ax.plot(
            x_vals, y_vals, marker="^", color="#C44E52", label="Green σ"
        )
        handles.append(line_green)
        labels.append("Green σ")

    ax.axhline(
        threshold,
        color="#8172B3",
        linestyle="--",
        linewidth=1.0,
        label=f"σ threshold ({threshold:.2f})",
    )

    warning_rows = [
        row for row in metric_rows if (row.warning_mask & DWELL_SATURATION_MASK) != 0
    ]
    warning_points = [
        (row.dwell_us, _max_std(row))
        for row in warning_rows
        if _max_std(row) is not None
    ]
    if warning_points:
        x_vals, y_vals = zip(*warning_points)
        scatter = ax.scatter(
            x_vals, y_vals, color="#DD8452", marker="x", label="saturation warning"
        )
        handles.append(scatter)
        labels.append("saturation warning")

    ax.set_xlabel("Dwell (µs)")
    ax.set_ylabel("Standard deviation")
    ax.set_title("Dwell sweep variance by channel")
    ax.grid(axis="both", linestyle="--", alpha=0.4)
    if handles:
        ax.legend(handles, labels, loc="best")

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _render_dwell_duration_plot(
    rows: List[DwellSweepSummaryRow], output_path: Path
) -> None:
    duration_rows = [row for row in rows if row.duration_us is not None]
    if not duration_rows:
        _render_placeholder_plot("No dwell sweep duration data", output_path)
        return

    x_vals = [row.dwell_us for row in duration_rows if row.duration_us is not None]
    y_vals = [row.duration_us for row in duration_rows if row.duration_us is not None]

    fig, ax = plt.subplots(figsize=(10, 5))
    ax.plot(x_vals, y_vals, marker="o", color="#4C72B0")

    warning_rows = [
        row for row in duration_rows if (row.warning_mask & DWELL_SATURATION_MASK) != 0
    ]
    warning_points = [
        (row.dwell_us, row.duration_us)
        for row in warning_rows
        if row.duration_us is not None
    ]
    if warning_points:
        x_warn, y_warn = zip(*warning_points)
        ax.scatter(
            x_warn, y_warn, color="#DD8452", marker="x", label="saturation warning"
        )
        ax.legend(loc="best")

    ax.set_xlabel("Dwell (µs)")
    ax.set_ylabel("Sweep duration (µs)")
    ax.set_title("Dwell sweep duration per dwell value")
    ax.grid(axis="both", linestyle="--", alpha=0.4)

    fig.tight_layout()
    fig.savefig(output_path, dpi=150)
    plt.close(fig)


def _build_cold_sweep_table(rows: List[ParsedSummary]) -> List[str]:
    cold_rows = [row for row in rows if isinstance(row, ColdSweepSummaryRow)]
    if not cold_rows:
        return []

    headers = [
        "Channel",
        "Samples",
        "Mean",
        "StdDev",
        "Min",
        "Max",
        "Saturated",
    ]
    table = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]

    for row in cold_rows:
        values = [
            row.channel,
            str(row.sample_count),
            _format_optional_float(row.mean),
            _format_optional_float(row.stddev),
            _format_optional_int(row.min_code),
            _format_optional_int(row.max_code),
            "yes" if row.saturated else "no",
        ]
        table.append("| " + " | ".join(values) + " |")

    return table


def _build_channel_map_table(rows: List[ParsedSummary]) -> List[str]:
    channel_rows = [row for row in rows if isinstance(row, SummaryRow)]
    if not channel_rows:
        return []

    headers = [
        "State",
        "Samples",
        "Mean A",
        "Std A",
        "Min A",
        "Max A",
        "Mean B",
        "Std B",
        "Min B",
        "Max B",
        "Channel map",
        "Warnings",
    ]
    table = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]

    for row in channel_rows:
        values = [
            row.label,
            str(row.sample_count),
            _format_optional_float(row.mean_channel_a),
            _format_optional_float(row.std_channel_a),
            _format_optional_float(row.min_channel_a),
            _format_optional_float(row.max_channel_a),
            _format_optional_float(row.mean_channel_b),
            _format_optional_float(row.std_channel_b),
            _format_optional_float(row.min_channel_b),
            _format_optional_float(row.max_channel_b),
            _format_optional_text(row.channel_alignment),
            _format_optional_text(row.warning_label),
        ]
        table.append("| " + " | ".join(values) + " |")

    return table


def _build_adc_speed_table(rows: List[ParsedSummary]) -> List[str]:
    adc_rows = [row for row in rows if isinstance(row, AdcSpeedSummaryRow)]
    if not adc_rows:
        return []

    headers = ["Mode", "Samples/s", "Loop (us)", "Errors", "Notes"]
    table = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]

    for row in adc_rows:
        values = [
            row.mode,
            _format_optional_float(row.samples_per_second),
            _format_optional_float(row.loop_microseconds),
            _format_optional_int(row.error_count),
            _format_optional_text(row.notes),
        ]
        table.append("| " + " | ".join(values) + " |")

    return table


def _build_osr_sweep_table(rows: List[ParsedSummary]) -> List[str]:
    osr_rows = [row for row in rows if isinstance(row, OsrSweepSummaryRow)]
    if not osr_rows:
        return []

    headers = [
        "OSR",
        "Samples",
        "Drain mean",
        "Drain std",
        "Drain min",
        "Drain max",
        "Blue mean",
        "Blue std",
        "Blue min",
        "Blue max",
        "Green mean",
        "Green std",
        "Green min",
        "Green max",
        "Sweep (us)",
    ]
    table = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]

    for row in osr_rows:
        osr_label = str(row.osr_value) if row.osr_value is not None else row.label
        values = [
            osr_label,
            str(row.sample_count),
            _format_optional_float(row.drain_mean),
            _format_optional_float(row.drain_std),
            _format_optional_float(row.drain_min),
            _format_optional_float(row.drain_max),
            _format_optional_float(row.blue_mean),
            _format_optional_float(row.blue_std),
            _format_optional_float(row.blue_min),
            _format_optional_float(row.blue_max),
            _format_optional_float(row.green_mean),
            _format_optional_float(row.green_std),
            _format_optional_float(row.green_min),
            _format_optional_float(row.green_max),
            _format_optional_int(row.sweep_duration_us),
        ]
        table.append("| " + " | ".join(values) + " |")

    return table


def _build_pot_sweep_table(rows: List[ParsedSummary]) -> List[str]:
    pot_rows = [row for row in rows if isinstance(row, PotSweepSummaryRow)]
    if not pot_rows:
        return []

    headers = [
        "Wiper",
        "Blue max code",
        "Green max code",
        "Blue saturated",
        "Green saturated",
    ]
    table = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]

    for row in pot_rows:
        values = [
            f"0x{row.wiper_code:02X}",
            str(row.blue_max_code),
            str(row.green_max_code),
            "yes" if row.blue_saturated else "no",
            "yes" if row.green_saturated else "no",
        ]
        table.append("| " + " | ".join(values) + " |")

    return table


def _build_dwell_sweep_table(rows: List[ParsedSummary]) -> List[str]:
    dwell_rows = [row for row in rows if isinstance(row, DwellSweepSummaryRow)]
    if not dwell_rows:
        return []

    headers = [
        "Dwell (µs)",
        "Sweeps",
        "Drain std",
        "Blue std",
        "Green std",
        "Duration (µs)",
        "Warnings",
    ]
    table = [
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join(["---"] * len(headers)) + " |",
    ]

    for row in dwell_rows:
        warnings = _decode_dwell_warning_mask(row.warning_mask)
        values = [
            str(row.dwell_us),
            str(row.sweeps_completed),
            _format_optional_float(row.drain_std),
            _format_optional_float(row.blue_std),
            _format_optional_float(row.green_std),
            _format_optional_int(row.duration_us),
            warnings,
        ]
        table.append("| " + " | ".join(values) + " |")

    return table


def _render_section_table(scenario: str, rows: List[ParsedSummary]) -> List[str]:
    if scenario == "channel_map":
        return _build_channel_map_table(rows)
    if scenario == "adc_speed":
        return _build_adc_speed_table(rows)
    if scenario == "cold_sweep":
        return _build_cold_sweep_table(rows)
    if scenario == "osr_sweep":
        return _build_osr_sweep_table(rows)
    if scenario == "pot_sweep":
        return _build_pot_sweep_table(rows)
    if scenario == "dwell_sweep":
        return _build_dwell_sweep_table(rows)
    return []


def _render_cold_sweep_section(
    rows: List[ParsedSummary],
    plot_paths: List[Path],
    extras: Dict[str, object],
    csv_paths: List[Path],
) -> List[str]:
    lines: List[str] = ["## Summary Table (cold_sweep)"]

    lines.append(
        "_Warm-up guidance_: Run the cold sweep immediately after power-on to confirm the sensors settle before longer captures. Review the sample plot to check that the ADC codes converge without saturation."
    )
    lines.append("")

    captured = extras.get("captured_sweeps") if isinstance(extras, dict) else None
    timestamp = extras.get("timestamp_us") if isinstance(extras, dict) else None
    sample_count = extras.get("sample_count") if isinstance(extras, dict) else None
    warning = extras.get("warning") if isinstance(extras, dict) else "none"
    run_metadata = extras.get("run_metadata") if isinstance(extras, dict) else {}
    saturated = extras.get("saturated_channels") if isinstance(extras, dict) else []

    detail_tokens = []
    if captured is not None:
        detail_tokens.append(f"captured sweeps: {captured}")
    if sample_count is not None:
        detail_tokens.append(f"samples parsed: {sample_count}")
    if timestamp is not None:
        detail_tokens.append(f"timestamp: {timestamp} µs")
    if detail_tokens:
        lines.append(
            "_Run details_: " + " · ".join(str(token) for token in detail_tokens)
        )
        lines.append("")

    if isinstance(run_metadata, dict) and run_metadata:
        config_summary = ", ".join(
            f"{key}={value}" for key, value in sorted(run_metadata.items())
        )
        lines.append(f"_Requested configuration_: {config_summary}")
        lines.append("")

    warning_label = warning if isinstance(warning, str) and warning else "none"
    lines.append(f"_Warnings_: {warning_label}")
    lines.append("")

    if isinstance(saturated, list) and saturated:
        channels = ", ".join(str(channel) for channel in saturated)
        lines.append(f"_Saturated channels_: {channels}")
        lines.append("")

    table_lines = _build_cold_sweep_table(rows)
    if table_lines:
        lines.extend(table_lines)
    else:
        lines.append("_(no cold sweep metrics)_")
    lines.append("")

    if csv_paths:
        csv_link = csv_paths[0].as_posix()
        lines.append(f"[Download CSV]({csv_link})")
        lines.append("")

    if plot_paths:
        for index, path in enumerate(plot_paths, start=1):
            suffix = f" #{index}" if len(plot_paths) > 1 else ""
            lines.append(f"![cold_sweep plot{suffix}]({path.as_posix()})")
        lines.append("")

    return lines


def _render_pot_sweep_section(
    rows: List[ParsedSummary],
    plot_paths: List[Path],
    extras: Dict[str, object],
    csv_paths: List[Path],
) -> List[str]:
    lines: List[str] = ["## Summary Table (pot_sweep)"]

    recommendations = extras.get("recommendations") if extras else None
    warning = extras.get("warning") if extras else None

    if isinstance(recommendations, dict) and recommendations:
        lines.append("| LED | Recommended Wiper |")
        lines.append("| --- | --- |")
        for led, wiper in sorted(recommendations.items()):
            lines.append(f"| {led.upper()} | {wiper} |")
    else:
        lines.append("_No recommendations recorded._")
    lines.append("")

    warning_label = warning if isinstance(warning, str) and warning else "none"
    lines.append(f"_Warnings_: {warning_label}")
    lines.append("")

    lines.append("<details>")
    lines.append("<summary>Full potentiometer sweep results</summary>")
    lines.append("")
    table_lines = _build_pot_sweep_table(rows)
    if table_lines:
        lines.extend(table_lines)
    else:
        lines.append("_(no pot sweep metrics)_")
    lines.append("")
    if csv_paths:
        csv_link = csv_paths[0].as_posix()
        lines.append(f"[Download CSV]({csv_link})")
        lines.append("")
    lines.append("</details>")
    lines.append("")

    if plot_paths:
        for index, path in enumerate(plot_paths, start=1):
            suffix = f" #{index}" if len(plot_paths) > 1 else ""
            lines.append(f"![pot_sweep plot{suffix}]({path.as_posix()})")
        lines.append("")

    return lines


def _render_dwell_sweep_section(
    rows: List[ParsedSummary],
    plot_paths: List[Path],
    extras: Dict[str, object],
    csv_paths: List[Path],
) -> List[str]:
    lines: List[str] = ["## Summary Table (dwell_sweep)"]

    recommendations = extras.get("recommendations") if extras else None
    warning = extras.get("warning") if extras else None

    if isinstance(recommendations, dict) and recommendations:
        recommended = recommendations.get("recommended")
        stable = recommendations.get("stable_dwells", [])
        threshold = recommendations.get("threshold")
        threshold_label = (
            f"{float(threshold):.2f}" if isinstance(threshold, (int, float)) else "--"
        )
        recommended_label = f"{recommended} µs" if recommended is not None else "--"
        lines.append(
            f"_Recommended dwell_: {recommended_label} (σ ≤ {threshold_label})"
        )
        if isinstance(stable, list) and stable:
            stable_values = ", ".join(f"{value} µs" for value in stable)
            lines.append(f"_Stable candidates_: {stable_values}")
    else:
        lines.append("_No dwell recommendations available._")
    lines.append("")

    warning_label = warning if isinstance(warning, str) and warning else "none"
    lines.append(f"_Warnings_: {warning_label}")
    lines.append("")

    table_lines = _build_dwell_sweep_table(rows)
    if table_lines:
        lines.extend(table_lines)
    else:
        lines.append("_(no dwell sweep metrics)_")
    lines.append("")

    if csv_paths:
        csv_link = csv_paths[0].as_posix()
        lines.append(f"[Download CSV]({csv_link})")
        lines.append("")

    if plot_paths:
        for index, path in enumerate(plot_paths, start=1):
            suffix = f" #{index}" if len(plot_paths) > 1 else ""
            lines.append(f"![dwell_sweep plot{suffix}]({path.as_posix()})")
        lines.append("")

    return lines


def _render_markdown_report(
    plan_path: Path,
    sections: List[_SummarySection],
    plot_paths: Dict[str, List[Path]],
    summaries: Dict[str, List[ParsedSummary]],
    scenario_extras: Dict[str, Dict[str, object]],
    csv_paths: Dict[str, List[Path]],
    drift_entries: List[DriftCaptureArtifactEntry],
) -> str:
    lines = ["# Phoenix Benchmark Report", ""]
    lines.append(f"*Plan:* `{plan_path}`")
    lines.append("")
    if sections:
        for section in sections:
            extras = scenario_extras.get(section.scenario, {})
            if section.scenario == "cold_sweep":
                cold_lines = _render_cold_sweep_section(
                    summaries.get(section.scenario, []),
                    plot_paths.get(section.scenario, []),
                    extras,
                    csv_paths.get(section.scenario, []),
                )
                lines.extend(cold_lines)
                continue
            if section.scenario == "pot_sweep":
                pot_lines = _render_pot_sweep_section(
                    summaries.get(section.scenario, []),
                    plot_paths.get(section.scenario, []),
                    extras,
                    csv_paths.get(section.scenario, []),
                )
                lines.extend(pot_lines)
                continue

            if section.scenario == "dwell_sweep":
                dwell_lines = _render_dwell_sweep_section(
                    summaries.get(section.scenario, []),
                    plot_paths.get(section.scenario, []),
                    extras,
                    csv_paths.get(section.scenario, []),
                )
                lines.extend(dwell_lines)
                continue

            lines.append(f"## Summary Table ({section.scenario})")
            table_lines = _render_section_table(
                section.scenario, summaries.get(section.scenario, [])
            )
            if table_lines:
                lines.extend(table_lines)
            else:
                lines.append("```")
                lines.append(section.header)
                lines.extend(section.rows)
                lines.append("```")
            lines.append("")
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

    if drift_entries:
        lines.append("## Drift Capture")
        lines.append("")
        lines.append(
            "| Burst | Start (us) | End (us) | Step (us) | OSR | Wiper | Blue Samples | Green Samples | Warnings |"
        )
        lines.append("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
        for entry in drift_entries:
            burst = entry.burst
            warning_label = (
                ", ".join(burst.warning_labels) if burst.warning_labels else "none"
            )
            lines.append(
                "| {index} | {start} | {end} | {step} | {osr} | {wiper} | {blue} | {green} | {warnings} |".format(
                    index=burst.index,
                    start=burst.start_us,
                    end=burst.end_us,
                    step=burst.step_us,
                    osr=burst.osr,
                    wiper=burst.wiper_code,
                    blue=burst.blue_sample_count,
                    green=burst.green_sample_count,
                    warnings=warning_label,
                )
            )
        lines.append("")

        for entry in drift_entries:
            burst = entry.burst
            lines.append(f"### Burst {burst.index}")
            lines.append("")
            lines.append(f"![drift_capture plot]({entry.plot.as_posix()})")
            lines.append("")
            artifact_links = [
                f"[CSV]({entry.csv.as_posix()})",
                f"[JSON]({entry.json.as_posix()})",
            ]
            lines.append("Artifacts: " + " · ".join(artifact_links))
            lines.append("")
            lines.append("<details>")
            lines.append(f"<summary>Burst {burst.index} samples</summary>")
            lines.append("")
            lines.append(
                "| Elapsed Blue (us) | Code Blue | Elapsed Green (us) | Code Green |"
            )
            lines.append("| --- | --- | --- | --- |")
            for sample in burst.combined_samples:
                lines.append(
                    "| {blue_elapsed} | {blue_code} | {green_elapsed} | {green_code} |".format(
                        blue_elapsed=_format_drift_optional(sample.blue_elapsed_us),
                        blue_code=_format_drift_optional(sample.blue_code),
                        green_elapsed=_format_drift_optional(sample.green_elapsed_us),
                        green_code=_format_drift_optional(sample.green_code),
                    )
                )
            lines.append("")
            lines.append("</details>")
            lines.append("")
    return "\n".join(lines)
