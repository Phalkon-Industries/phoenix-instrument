# Phoenix Benchmark Overview

This document captures the *currently implemented* capabilities of the Phoenix benchmarking firmware and the related workflows. Update it whenever a feature lands so it stays aligned with the actual code.

## Current Capabilities

- Firmware idles after boot, accepts `channel_map`, `osr_sweep`, `dwell_sweep`, `pot_sweep`, or `adc_speed` commands, executes the requested scenario, and then returns to a fresh `# ready` prompt so hosts can pipeline multiple operations.
- The dwell sweep scenario walks a linear series of LED settle times (`start_dwell_us` → `end_dwell_us` with `dwell_step_us` spacing), reruns the channel-map capture loop for each dwell window, and records per-step variance plus warning masks so operators can pick a dwell that balances stability and runtime.
- The potentiometer sweep scenario always scans every digi-pot wiper code (0–255) while allowing hosts to override only the sweep count and LED dwell via `sweeps` and `dwell_us`; summary rows now call out recommended wipers per LED when saturation is detected.
- The OSR sweep scenario iterates every supported oversampling ratio preset, capturing drain and LED variance plus sweep duration so operators can spot the lowest-noise configuration quickly.
- The command parser is dual-mode: it prefers JSON payloads that match the host tooling schema and automatically falls back to key-value arguments such as `channel_map dwell_us=50 sweeps=10`. Both forms converge to the same option structure and apply firmware defaults when fields are omitted.
- The summary table includes a `Channel_Map` column that compares each LED's observed dominant channel against the expected routing (`A=OK`, `B!=A`, etc.).
- Run headers print the dominance thresholds (`channel_map_config`) used for the pairing heuristic so lab notes can show the exact detection parameters.
- The `phoenix_benchmark` example sketch delegates execution to the shared C-style channel-map library (`lib/phoenix_benchmark/channel_map/`) and consumes the exported state descriptors for summary formatting.
- The Python CLI runs the plan, captures the serial transcript, and produces a Markdown report that bundles Markdown tables, a JSON export, CSV exports, and scenario-aware plots. Mixed plans (channel map + OSR sweep + dwell sweep + pot sweep + ADC throughput) appear in a single report with per-scenario tables and rendered plots.

### Library Surface

The reusable library lives in `lib/phoenix_benchmark/channel_map/` and exposes C-style entry points for firmware modules:

- `phoenix_benchmark_channel_map_initialise(const PhoenixBenchmarkChannelMapDefaults& defaults)` primes the internal state machine and records the default sweep configuration shared by the parser and runner.
- `phoenix_benchmark_channel_map_parse_command(const char* line)` normalises JSON or key-value input into a `PhoenixBenchmarkChannelMapOptions` struct. Errors return descriptive strings that the sketch can forward over Serial.
- `phoenix_benchmark_channel_map_run(const PhoenixBenchmarkChannelMapOptions& options, PhoenixBenchmarkStateAccumulator* accumulators, const PhoenixBenchmarkChannelMapOutputCallbacks& callbacks)` executes the full sweep using the provided hardware drivers.
- Formatter helpers such as `phoenix_benchmark_channel_map_format_summary_header` and `phoenix_benchmark_channel_map_format_summary_row` produce fixed-width table output without duplicating formatting logic in callers.

- `lib/phoenix_benchmark/adc_speed/` exposes the throughput surface used by the example sketch: `phoenix_benchmark_adc_speed_parse_command`, `phoenix_benchmark_adc_speed_run`, and formatter helpers that mirror the channel-map table conventions. Blocking and IRQ sampling share the same execution entry point so hosts can toggle each mode without changing firmware binaries.
- `lib/phoenix_benchmark/osr_sweep/` reuses the channel-map hardware preparation helpers, iterates the oversampling presets, and records per-setting drain/LED metrics alongside elapsed sweep timing.

Consumers outside the example sketch can include `channel_map/channel_map.hpp` or `adc_speed/adc_speed.hpp` and reuse the same APIs without copying orchestration code.

## Host Workflow
1. **Configuration** – Build the firmware (`pio run -e phoenix_benchmark_example`) with any desired compile-time overrides.
2. **Dry-run Plans** – Use the Python CLI (within the shared conda environment) to validate command plans without touching hardware. Sample plans now cover both individual and combined scenarios (`docs/phoenix-benchmark/sample_plans/channel_map_phase1.json` and `docs/phoenix-benchmark/sample_plans/combined_benchmarks.json`):
   ```powershell
   conda run -n phoenix-benchmark python python/phoenix_benchmark/cli.py docs/phoenix-benchmark/sample_plans/channel_map_phase1.json --dry-run
   ```
   The tool echoes the serial payloads it will transmit during execution.
   The combined template demonstrates issuing a `channel_map` sweep, running the OSR sweep, sweeping LED dwell (`dwell_sweep`), scanning the potentiometer (`pot_sweep`), and finishing with an `adc_speed` throughput run.
3. **Execution** – Connect hardware, then run the CLI against a plan and serial port:
   ```powershell
   conda run -n phoenix-benchmark python python/phoenix_benchmark/cli.py docs/phoenix-benchmark/sample_plans/combined_benchmarks.json --port COM6 --ready-timeout 10 --command-timeout 240
   ```
   The CLI waits for the firmware `# ready` banner, streams each command, prints device output to stdout, and stores every line in a transcript buffer. Provide `--output <path>` to capture artifacts inside a preferred workspace location.
   The firmware also accepts manual overrides directly from a serial terminal using the key-value syntax: `channel_map sweeps=25 dwell_us=50 wiper_code=128` or `adc_speed duration_ms=750 enable_irq=false`.
4. **Automated Report** – After each scenario signals `# benchmark_complete`, the CLI writes a report bundle (`transcript_<scenarios>.txt`, `summary_<scenarios>.json`, `report.md`, plus scenario-specific plots and CSVs) to the chosen output folder (default: `~/Downloads/phoenix-benchmark/<timestamp>`). Markdown tables now summarize every scenario, dwell-sweep charts map dwell vs variance/runtime, and OSR plots render on a log₂ x-axis with diagonally angled tick labels to reduce overlap.
5. **Extended Analysis** – Use the JSON or transcript artifacts in separate notebooks or scripts as needed; this repository remains focused on the source code and first-line automation flow.

## Output Structure
- Lines beginning with `#` communicate metadata (configuration details, error messages) and the final completion notice. The CLI persists these lines verbatim inside the transcript artifact.
- Channel-map summary tables list each state (and the overall cycle) with aligned columns: samples, channel statistics (mean/std/min/max), timing metrics, and the channel alignment verdict. The CLI parses this table to populate the exported JSON and the channel dominance plot.
- ADC-speed summary tables contain one row per enabled mode (blocking and/or IRQ) with samples-per-second, average loop timing, error counts, and free-form notes that highlight warnings such as ADC error spikes.
- OSR sweep summary tables list every oversampling preset with drain/LED statistics and elapsed sweep time. The CLI renders paired plots comparing drain/LED standard deviation across presets and sweep duration vs OSR on a log₂ axis.
- Dwell sweep summary tables include dwell duration, per-channel variance, sweep timing, and warning masks that highlight saturation or ADC recovery events. Companion plots trace dwell duration against variance and runtime to guide dwell selection.

## Revision History
| Date       | Notes                                                                                                                      |
| ---------- | -------------------------------------------------------------------------------------------------------------------------- |
| 2025-10-21 | Documented dwell sweep workflow, refreshed host automation notes, and removed references to repository-stored sample runs. |
| 2025-10-17 | Documented pot sweep defaults (full-range, dwell override), refreshed OSR sweep notes, and published sample run artifacts. |
| 2025-10-16 | Added ADC throughput coverage, mixed-plan reporting, and sample logs for channel map + adc_speed runs.                     |
| 2025-10-12 | Documented the dual-mode parser, reusable library API, and manual key-value workflow for the channel-map module.           |
| 2025-10-11 | Example sketch now calls the shared channel-map library; documented the new reuse surface.                                 |
| 2025-10-08 | Updated to document the command-driven Phase 1 workflow, automated reporting, and CLI-to-firmware JSON contract.           |
