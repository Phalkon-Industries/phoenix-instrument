# Phoenix Benchmark Overview

This document captures the *currently implemented* capabilities of the Phoenix benchmarking firmware and the related workflows. Update it whenever a feature lands so it stays aligned with the actual code.

## Current Capabilities

- Firmware idles after boot, waits for a `channel_map` command, performs the sweep, and then returns to a fresh `# ready` prompt.
- The summary table includes a `Channel_Map` column that compares each LED's observed dominant channel against the expected routing (`A=OK`, `B!=A`, etc.).
- Run headers print the dominance thresholds (`channel_map_config`) used for the pairing heuristic so lab notes can show the exact detection parameters.
- The `phoenix_benchmark` example sketch delegates execution to the shared `phoenix_benchmark::channel_map` library and consumes the exported state descriptors for summary formatting.
- The Python CLI runs the plan, captures the serial transcript, and produces a Markdown report that bundles the raw summary table, a JSON export, and a bar chart of channel min/max values.

## Host Workflow
1. **Configuration** – Build the firmware (`pio run -e phoenix_benchmark_example`) with any desired compile-time overrides.
2. **Dry-run Plans** – Use the Python CLI (within the shared conda environment) to validate command plans without touching hardware. The sample plan lives at `docs/phoenix-benchmark/sample_plans/channel_map_phase1.json`:
   ```powershell
   conda run -n phoenix-benchmark python python/phoenix_benchmark/cli.py docs/phoenix-benchmark/sample_plans/channel_map_phase1.json --dry-run
   ```
   The tool echoes the serial payloads it will transmit during execution.
       A ready-to-use template lives at `docs/phoenix-benchmark/sample_plans/channel_map_phase1.json`.
3. **Execution** – Connect hardware, then run the CLI against a plan and serial port:
   ```powershell
   conda run -n phoenix-benchmark python python/phoenix_benchmark/cli.py docs/phoenix-benchmark/sample_plans/channel_map_phase1.json --port COM6 --ready-timeout 10 --command-timeout 120
   ```
   The CLI waits for the firmware `# ready` banner, streams each command, prints device output to stdout, and stores every line in a transcript buffer.
4. **Automated Report** – After the firmware signals `# benchmark_complete`, the CLI writes a report bundle (`transcript.txt`, `summary.json`, `report.md`, `channel_map.png`) to the chosen output folder (default: `~/Downloads/phoenix-benchmark/<timestamp>`). The Markdown file embeds the raw table and generated plot for easy sharing.
5. **Extended Analysis** – Use the JSON or transcript artifacts in separate notebooks or scripts as needed; this repository remains focused on the source code and first-line automation flow.

## Output Structure
- Lines beginning with `#` communicate metadata (configuration details, error messages) and the final completion notice. The CLI persists these lines verbatim inside `transcript.txt` for auditing.
- The summary table lists each state (and the overall cycle) with aligned columns: samples, channel statistics (mean/std/min/max), timing metrics, and the channel alignment verdict. The CLI parses this table to populate the exported JSON and the channel dominance plot.

## Revision History
| Date       | Notes                                                                                                            |
| ---------- | ---------------------------------------------------------------------------------------------------------------- |
| 2025-10-11 | Example sketch now calls the shared channel-map library; documented the new reuse surface.                       |
| 2025-10-08 | Updated to document the command-driven Phase 1 workflow, automated reporting, and CLI-to-firmware JSON contract. |
