# Phoenix Benchmark Serial Protocol

This document tracks the evolving host ↔ firmware contract. Beginning with Phase 1, the firmware idles after boot, waits for structured commands issued by the host CLI, and returns to an idle `# ready` prompt after every run. Phase 2 adds the high-speed ADC throughput scenario while preserving the original framing rules, Phase 3 introduces the OSR sweep to characterise oversampling noise behaviour, Phase 4 adds the potentiometer saturation sweep, Phase 5 layers on dwell-time studies, and Phase 6 adds rapid drift capture bursts without altering the underlying transport.

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

### `pot_sweep`

Requests a full-range potentiometer sweep. The firmware measures every wiper code (0–255) on each invocation and only honours the sweep count and dwell overrides below.

| Field      | Type    | Required | Notes                                                                                                      |
| ---------- | ------- | -------- | ---------------------------------------------------------------------------------------------------------- |
| `sweeps`   | integer | no       | Optional sweeps per wiper position. Defaults to the firmware setting (currently 5).                        |
| `dwell_us` | integer | no       | Optional LED dwell in microseconds applied before each channel-map capture. Defaults to firmware settings. |

**Example**

```json
{"command": "pot_sweep", "parameters": {"sweeps": 6, "dwell_us": 180}}
```

Run headers include the fixed span (`wiper_count=256`) so logs can be matched against the CSV table without inspecting the payload.

### `dwell_sweep`

Requests a dwell-duration sensitivity sweep. The firmware iterates a linear dwell series, capturing channel-map statistics at each step while keeping potentiometer and OSR settings fixed.

| Field              | Type    | Required | Notes                                                                                                     |
| ------------------ | ------- | -------- | --------------------------------------------------------------------------------------------------------- |
| `sweeps_per_dwell` | integer | yes      | Number of channel-map cycles to run per dwell entry. Must be between 1 and 1000 inclusive.                |
| `start_dwell_us`   | integer | yes      | First dwell duration in microseconds. Must be within firmware limits (0–5,000,000 µs).                    |
| `end_dwell_us`     | integer | yes      | Final dwell duration in microseconds. Must be ≥ `start_dwell_us` and within firmware limits.              |
| `dwell_step_us`    | integer | yes      | Positive microsecond increment applied between dwell entries. Determines how many dwell rows are emitted. |

**Example**

```json
{"command": "dwell_sweep", "parameters": {"sweeps_per_dwell": 4, "start_dwell_us": 100, "end_dwell_us": 500, "dwell_step_us": 100}}
```

Run headers list the schedule bounds (`start_us`, `end_us`, `step_us`, `steps`). Per-row metadata lines surface dwell duration, sweeps completed, elapsed time, channel variance, and a warning mask so the host can highlight saturation or ADC recovery events.

Unknown command identifiers return a `# error,unsupported_command` line until the corresponding firmware feature ships.

### `drift_capture`

Requests a rapid dual-LED capture immediately after each LED transitions on. The firmware timestamps LED1/LED2 samples relative to the activation edge, stores them in a shared buffer, and emits metadata once both sequences complete.

| Field           | Type    | Required | Notes                                                                                                                                                |
| --------------- | ------- | -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- |
| `start_time_us` | integer | no       | First timestamp to report in microseconds. Defaults to `0` when omitted. Must be ≥ 0 and ≤ `end_time_us`.                                            |
| `end_time_us`   | integer | no       | Final elapsed timestamp to capture. Defaults to `100000`. Must be ≥ `start_time_us` and within the buffer-derived upper bound documented in Phase 6. |
| `step_delay_us` | integer | no       | Optional inter-sample pause. Defaults to `0` (tight loop). Positive values insert a guarded delay between samples.                                   |
| `osr`           | integer | no       | Optional ADC oversampling ratio override. Defaults to the most recent channel-map context when omitted.                                              |
| `wiper_code`    | integer | no       | Optional potentiometer wiper override applied before the burst. Defaults to the last channel-map wiper.                                              |

**Example**

```json
{"command": "drift_capture", "parameters": {"start_time_us": 0, "end_time_us": 50000, "step_delay_us": 10, "osr": 4096, "wiper_code": 42}}
```

**Serial output**

Drift captures defer all output until both LED buffers are populated, then emit the following sequence (prefixes not shown in JSON payloads):

1. `# running,scenario=drift_capture,...` – announces the scenario with resolved metadata.
2. `# drift_capture,metadata,start_us=...,end_us=...,step_delay_us=...,osr=...,wiper_code=...` – records the applied capture settings.
3. `# drift_capture,results,led1_samples=<N1>,led2_samples=<N2>,warning_mask=<mask>` – reports sample counts and the combined warning bitmask (`0x01` buffer overflow, `0x02` saturation, `0x04` restore failure).
4. `Elapsed_LED1_us	Code_LED1	Elapsed_LED2_us	Code_LED2` header followed by tab-separated sample rows. Missing values are printed as `nan` to keep LED1/LED2 timelines aligned.
5. Blank line terminates the sample section prior to `# benchmark_complete`.

The CLI translates `nan` tokens into `None` values, persists the aligned samples to CSV/JSON, and emits a `# drift_capture_summary,bursts=...,slugs=...,warnings=...` line summarising the run.

### End-to-end flow

1. Operator invokes the CLI with a plan file. The CLI prints a numbered preview of the JSON lines that will be transmitted.
2. When `--port` is provided (non dry-run), the CLI opens the serial port at 115200 baud, waits for the firmware's `# phoenix benchmark ready` + `# ready` banner, and stores every line it receives in a transcript buffer while echoing it to stdout.
3. Each queued command is streamed verbatim as one JSON line followed by `\n`. The firmware's command loop trims the newline, dispatches to the scenario-specific parser (`phoenix_benchmark_channel_map_parse_command`, `phoenix_benchmark_osr_sweep_parse_command`, `phoenix_benchmark_dwell_sweep_parse_command`, `phoenix_benchmark_pot_sweep_parse_command`, or `phoenix_benchmark_adc_speed_parse_command`), and executes the request. Unsupported identifiers emit `# error,unsupported_command` before the dispatcher returns to idle.
4. During execution the firmware prints the scenario metadata (`# running,scenario=...`) plus summary tables. Channel-map runs emit the legacy per-state table; OSR sweeps emit one row per oversampling preset alongside runtime metrics; dwell sweeps emit one row per dwell window with variance, timing, and warning masks; pot sweeps emit wiper recommendations and saturation flags; ADC-speed runs emit a mode-oriented throughput table. The CLI mirrors the output and records it for later parsing.
5. After each `# benchmark_complete` line, the firmware prints a fresh `# ready` prompt. The CLI stays attached until the plan is exhausted, then persists the transcript, parses every captured table (including the drift-capture metadata/results block), and produces a Markdown report with scenario-tagged sections inside the selected output directory. Artifact file names now embed the scenario list to aid long-term archiving (e.g., `transcript_channel_map_drift_capture_osr_sweep_adc_speed.txt`).

## Host Expectations

- The CLI (`python/phoenix_benchmark/cli.py`) validates plan files, opens the requested serial port, writes each command, and streams the firmware response back to stdout. Use `--dry-run` to skip the serial session when validating plans offline.
- Host plans store an ordered list of command objects under a `commands` (or `sequence`) array.
- Tooling serialises each entry with compact separators so commands remain single-line friendly for terminal usage.
- The CLI awaits the firmware's `# ready` prompt before issuing the first command and prints every line the device emits so logs can be redirected or parsed downstream.
- After a run completes, the CLI writes a `transcript_<scenarios>.txt`, `summary_<scenarios>.json`, and `report.md` bundle (plus scenario-specific plots and CSV exports) to the chosen output folder. The Markdown report embeds every summary table—including OSR, dwell, pot sweeps, and drift captures—and links plots with scenario-specific axes (log₂ for OSR, linear dwell, wiper code for potentiometer, elapsed microseconds for drift capture).

## Firmware Expectations

- Firmware now waits for supported commands (`channel_map`, `osr_sweep`, `dwell_sweep`, `pot_sweep`, `adc_speed`, `drift_capture`) before executing work. Upon receiving the request it echoes a `# running` metadata line, performs the scenario, and prints `# benchmark_complete` followed by a renewed `# ready` prompt.
- During Phase 1, channel-to-LED association is reported in the summary table via the new `Channel_Map` column:
  - `A=OK` or `B=OK` indicates that the observed dominant ADC channel matches the expected LED routing.
  - `B!=A`, `A!=B`, or `??!=A/B` highlight mismatches or ambiguous responses.
- Min/max ADC codes are now included per channel so host automation can surface saturation without parsing the streamed CSV. OSR sweeps report per-preset drain/LED statistics plus sweep timing; dwell sweeps report per-dwell variance, warning masks, and runtime; pot sweeps report LED-specific saturation flags and recommended wiper codes; throughput runs report samples-per-second, loop timing, and ADC error counts per enabled mode; drift captures flag warning masks alongside aligned LED timelines; warnings surface via the `Notes`, `Warning Mask`, or drift warning columns when issues occur.
