# Phoenix Benchmark Serial Protocol

This document tracks the evolving host ↔ firmware contract. Beginning with Phase 1, the firmware idles after boot, waits for structured commands issued by the host CLI, and returns to an idle `# ready` prompt after every run. The envelope defined below remains the foundation for later phases so host tooling and firmware can converge safely.

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

Additional commands (gain matrix, dwell sensitivity, etc.) will be added in later phases. Unknown command identifiers return a `# error,unsupported_command` line until the corresponding firmware feature ships.

### End-to-end flow

1. Operator invokes the CLI with a plan file. The CLI prints a numbered preview of the JSON lines that will be transmitted.
2. When `--port` is provided (non dry-run), the CLI opens the serial port at 115200 baud, waits for the firmware's `# phoenix benchmark ready` + `# ready` banner, and stores every line it receives in a transcript buffer while echoing it to stdout.
3. Each queued command is streamed verbatim as one JSON line followed by `\n`. The firmware's command loop trims the newline, calls `parse_channel_map_command()` (Phase 1), and executes the request. Unsupported identifiers emit `# error,unsupported_command` before the dispatcher returns to idle.
4. During execution the firmware prints the legacy summary table plus any metadata lines. The CLI continues to mirror the output and records it for later parsing.
5. After the `# benchmark_complete` line, the firmware prints a fresh `# ready` prompt. The CLI closes the serial port, persists the transcript, parses the summary table, and produces a Markdown report with plots inside the selected output directory.

## Host Expectations

- The Phase 1 CLI (`python/phoenix_benchmark/cli.py`) validates plan files, opens the requested serial port, writes each command, and streams the firmware response back to stdout. Use `--dry-run` to skip the serial session when validating plans offline.
- Host plans store an ordered list of command objects under a `commands` (or `sequence`) array.
- Tooling serialises each entry with compact separators so commands remain single-line friendly for terminal usage.
- The CLI awaits the firmware's `# ready` prompt before issuing the first command and prints every line the device emits so logs can be redirected or parsed downstream.
- After a run completes, the CLI writes a `transcript.txt`, `summary.json`, `channel_map.png`, and `report.md` bundle to the chosen output folder. The Markdown report embeds the raw summary table and the generated plot so lab staff can share results immediately.

## Firmware Expectations

- Firmware now waits for a `channel_map` command before executing the characterization sweep. Upon receiving the request it echoes a `# running` metadata line, performs the sweep, and prints `# benchmark_complete` followed by a renewed `# ready` prompt.
- During Phase 1, channel-to-LED association is reported in the summary table via the new `Channel_Map` column:
  - `A=OK` or `B=OK` indicates that the observed dominant ADC channel matches the expected LED routing.
  - `B!=A`, `A!=B`, or `??!=A/B` highlight mismatches or ambiguous responses.
- Min/max ADC codes are now included per channel so host automation can surface saturation without parsing the streamed CSV.
