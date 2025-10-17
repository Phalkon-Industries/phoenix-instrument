# Phoenix Benchmark Serial Protocol

This document tracks the evolving host ↔ firmware contract. Beginning with Phase 1, the firmware idles after boot, waits for structured commands issued by the host CLI, and returns to an idle `# ready` prompt after every run. Phase 2 adds the high-speed ADC throughput scenario while preserving the original framing rules, and Phase 3 introduces the OSR sweep to characterise oversampling noise behaviour without altering the transport.

## Message Framing

- Commands are newline-delimited UTF-8 JSON objects written to the MCU's virtual COM port.
- Firmware responses remain compatible with the existing text stream (`#` metadata, optional CSV rows, and the final summary table).
- Each command object MUST include a `command` field and MAY include an optional `parameters` object.
- The Python CLI serialises each plan entry with `json.dumps(..., separators=(",", ":"))` so the payload occupies a single line; the firmware trims whitespace and dispatches based on the `command` string.

## Commands

### `channel_map`

Requests a single sweep that identifies which ADC channel responds to each LED state.

| Field      | Type    | Required | Notes                                                                                    |
| ---------- | ------- | -------- | ---------------------------------------------------------------------------------------- |
| `sweeps`   | integer | yes      | Number of complete Drain → LED1 → LED2 cycles to capture. Must be positive.              |
| `dwell_us` | integer | no       | Optional microsecond dwell override per state. When omitted, firmware defaults are used. |

**Example**

```json
{"command": "channel_map", "parameters": {"sweeps": 100, "dwell_us": 100}}
```

### `adc_speed`

Requests a timed throughput measurement using the configured sampling modes.

| Field             | Type    | Required | Notes                                                                                          |
| ----------------- | ------- | -------- | ---------------------------------------------------------------------------------------------- |
| `duration_ms`     | integer | yes      | Benchmark runtime per mode. Must be positive.                                                  |
| `enable_blocking` | boolean | no       | When omitted, defaults to `true`. Set to `false` to skip blocking-mode sampling.               |
| `enable_irq`      | boolean | no       | When omitted, defaults to `true`. Set to `false` to skip IRQ sampling (or `enable_irq=false`). |

**Example**

```json
{"command": "adc_speed", "parameters": {"duration_ms": 750, "enable_blocking": true, "enable_irq": true}}
```

### `osr_sweep`

Requests a full oversampling ratio sweep while holding potentiometer and dwell settings constant for the run.

| Field        | Type    | Required | Notes                                                                                          |
| ------------ | ------- | -------- | ---------------------------------------------------------------------------------------------- |
| `sweeps`     | integer | no       | Optional per-OSR sweep count. Defaults to the firmware setting (currently 10) when omitted.    |
| `dwell_us`   | integer | no       | Optional microsecond dwell override shared by every OSR preset. Defaults to firmware settings. |
| `wiper_code` | integer | no       | Optional potentiometer wiper override applied before the sweep. Defaults to firmware settings. |

**Example**

```json
{"command": "osr_sweep", "parameters": {"sweeps": 10, "dwell_us": 100, "wiper_code": 0}}
```

The firmware iterates the OSR presets in ascending order, prints per-run metadata (`# running,scenario=osr_sweep,pot=...`) and emits a summary table that tags each row with the current OSR (e.g., `OSR1024`).

Unknown command identifiers return a `# error,unsupported_command` line until the corresponding firmware feature ships.

### End-to-end flow

1. Operator invokes the CLI with a plan file. The CLI prints a numbered preview of the JSON lines that will be transmitted.
2. When `--port` is provided (non dry-run), the CLI opens the serial port at 115200 baud, waits for the firmware's `# phoenix benchmark ready` + `# ready` banner, and stores every line it receives in a transcript buffer while echoing it to stdout.
3. Each queued command is streamed verbatim as one JSON line followed by `\n`. The firmware's command loop trims the newline, dispatches to the scenario-specific parser (`phoenix_benchmark_channel_map_parse_command`, `phoenix_benchmark_osr_sweep_parse_command`, or `phoenix_benchmark_adc_speed_parse_command`), and executes the request. Unsupported identifiers emit `# error,unsupported_command` before the dispatcher returns to idle.
4. During execution the firmware prints the scenario metadata (`# running,scenario=...`) plus summary tables. Channel-map runs emit the legacy per-state table; OSR sweeps emit one row per oversampling preset alongside runtime metrics; ADC-speed runs emit a mode-oriented throughput table. The CLI mirrors the output and records it for later parsing.
5. After each `# benchmark_complete` line, the firmware prints a fresh `# ready` prompt. The CLI stays attached until the plan is exhausted, then persists the transcript, parses every captured table, and produces a Markdown report with scenario-tagged sections inside the selected output directory. Artifact file names now embed the scenario list to aid long-term archiving (e.g., `transcript_channel_map_osr_sweep_adc_speed.txt`).

## Host Expectations

- The CLI (`python/phoenix_benchmark/cli.py`) validates plan files, opens the requested serial port, writes each command, and streams the firmware response back to stdout. Use `--dry-run` to skip the serial session when validating plans offline.
- Host plans store an ordered list of command objects under a `commands` (or `sequence`) array.
- Tooling serialises each entry with compact separators so commands remain single-line friendly for terminal usage.
- The CLI awaits the firmware's `# ready` prompt before issuing the first command and prints every line the device emits so logs can be redirected or parsed downstream.
- After a run completes, the CLI writes a `transcript_<scenarios>.txt`, `summary_<scenarios>.json`, and `report.md` bundle (plus scenario-specific plots) to the chosen output folder. The Markdown report embeds every summary table—including the OSR sweep results—and links plots with log₂ OSR axes and diagonally rotated tick labels for readability.

## Firmware Expectations

- Firmware now waits for supported commands (`channel_map`, `osr_sweep`, `adc_speed`) before executing work. Upon receiving the request it echoes a `# running` metadata line, performs the scenario, and prints `# benchmark_complete` followed by a renewed `# ready` prompt.
- During Phase 1, channel-to-LED association is reported in the summary table via the new `Channel_Map` column:
  - `A=OK` or `B=OK` indicates that the observed dominant ADC channel matches the expected LED routing.
  - `B!=A`, `A!=B`, or `??!=A/B` highlight mismatches or ambiguous responses.
- Min/max ADC codes are now included per channel so host automation can surface saturation without parsing the streamed CSV. OSR sweeps report per-preset drain/LED statistics plus sweep timing, and throughput runs report samples-per-second, loop timing, and ADC error counts per enabled mode; warnings surface via the `Notes` column when errors occur.

For a complete example covering the three supported scenarios, see `docs/phoenix-benchmark/sample_runs/osr_sweep_demo/`.
