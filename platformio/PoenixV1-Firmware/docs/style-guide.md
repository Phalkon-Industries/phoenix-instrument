# Phoenix Instrument Firmware Style Guide

This guide covers conventions not enforced automatically by `clang-format`. Follow these expectations whenever you introduce, rename, or refactor code.

## 1. Naming Conventions
- **Files:** Use `lower_snake_case` (e.g. `mcp356x.cpp`, `unity_config.cpp`).
- **Namespaces:** Avoid unless interacting with Arduino libraries; prefer `static` linkage instead.
- **Types / structs / enums / classes:** Use `CamelCase` (upper camel). Example: `struct AdcSampleBuffer`.
- **Functions and free helpers:** Use `lower_snake_case`. Example: `read_config_value()`.
- **Public phoenix benchmark APIs:** Export scenario entry points with the C-style prefix
  `phoenix_benchmark_<module>_<verb>` so they link cleanly from C and C++. For example,
  `phoenix_benchmark_channel_map_run`, `phoenix_benchmark_channel_map_format_summary_row`.
  - Match accompanying structs/enums with the `PhoenixBenchmark<Module><Role>` pattern (e.g.
    `PhoenixBenchmarkChannelMapDefaults`).
  - Keep these declarations in plain headers without namespaces; reserve namespaces for Arduino/third-party
    integration quirks only.
- **Unity test names:** Use clear test identifiers in `lower_snake_case` following Unity conventions—prefer `test_action` or `test_unit_action` so the name describes the behavior and expected result (e.g. `test_adc_read_returns_error_on_null_buffer`, `test_config_load_sets_defaults`).
- **Static globals:** Prefix with `g_` (e.g. `g_initialized`). Prefer file-local `static` over `extern`.
- **Local variables:** Use `lower_snake_case` (e.g. `config0_before`, `status_after`). Favour descriptive names over terse shorthands; for function return values capture them in a variable named `return_code` instead of `rc`.
- **Constants:**
  - C macros: `UPPER_SNAKE_CASE` (e.g. `MCP356X_STATUS_DR_MASK`).
  - File- or function-local `static const` values: prefix with `k_` and keep the remainder `lower_snake_case` (e.g. `k_spi_clock_hz`).
- **Enum values:** Use `UPPER_SNAKE_CASE` to match existing register definitions.

## 2. General C++ Practices
- Prefer explicit types over `auto` unless the API forces the type (e.g. iterator loops).
- Keep each function narrowly scoped; document any required side effects or shared state.
- Run `clang-format` on every C/C++ source or header you modify before committing so the codebase stays consistent with `.clang-format`. Non-code files are explicitly excluded from this requirement.
- Turn on “format on save” in your IDE when possible.
- C-style public APIs should live in headers that can be included from C or C++. If a header must be consumed by both,
  add the usual `#ifdef __cplusplus` guard with `extern "C" { ... }`.

## 3. Header Layout and Includes
- Include the module's public header first (e.g. `#include "mcp356x.hpp"`), then a blank line, then Arduino/standard-library headers.
- Avoid duplicate includes; keep headers idempotent via `#ifndef` guards named `FILE_BASENAME_HPP`.

## 4. Documentation Expectations
- Public-facing APIs declared in headers use Doxygen-style blocks describing purpose, parameters, return codes, and side effects.
- Internal helpers still carry concise doc comments so IDE tooltips remain informative; focus on intent, inputs, outputs, and notable side effects.
- Inline comments describe the processing step in plain English before the code executes it. Summarize what the step accomplishes and why it belongs here; prefer one full sentence and let `clang-format` wrap.
- Start sequential comments with an explicit `Step N:` prefix (use suffixes `Step 2a`, `Step 2b`, etc. for sub-steps) so readers can follow the ordered progression (`// Step 2: Sample drain channel to clear residual charge.`). Single, stand-alone comments that are not part of a sequence may omit the prefix, but multi-line workflows must use it consistently.
- Avoid restating the obvious expression; explain the intent, constraints, datasheet references, or timing notes that justify the implementation.
- Use banner comments (`// ===================== ... =====================`) when grouping related registers or constants.

## 5. Error Handling Patterns
- Return early on invalid arguments or runtime guardrails (e.g. `if (buffer == NULL) return MCP356X_ERR_INVALID_ARG;`).
- Propagate driver error codes unchanged so tests can assert on them.
- Prefer small helper functions over complex branching; for example, register framing lives in `mcp356x_command_byte`.

## 6. Test Structure
- Unity test files live under `test/<suite_name>/`. Each test function is `static void test_<scenario>(void)`.
- Use Unity's `setUp()` and `tearDown()` hooks to establish and reset shared state instead of duplicating initialization logic in every test.
- Ensure all hardware state modifications are undone before exit (reset registers, restore mux values, etc.). Prefer placing this work in `tearDown()` when consistent across tests.
- `setup()` must call `UNITY_SETUP_SERIAL_DEFAULT();` before `RUN_TEST` calls and `UNITY_END();` to balance initialization.
- When iterating, use explicit bounds/limits (e.g. `for (int attempt = 0; attempt < 10; ++attempt)`), not `while (true)`.
- Always include `<unity.h>` before project headers and standard library headers used by the test.

## 7. Magic Numbers & Constants
- Replace repeated literals with named constants either in headers (e.g. `#define MCP356X_EXPECTED_STATUS_START 0x17u`) or local `const` values.
- Explain delay values (e.g. `delay(2);  // allow registers to settle after POR reset`).

## 8. Pointer & Ownership Conventions
- Driver APIs accept raw pointers; validate them (`result == NULL`) before use.
- Use stack buffers and avoid dynamic allocation. Multi-byte transfers rely on fixed-size arrays (e.g. `uint8_t adc_bytes[3]`).
- When a pointer argument is optional, document that passing `NULL` is allowed.

## 9. Arduino-Specific Practices
- Always drive chip-select pins to a known idle state immediately after `pinMode` (e.g. `digitalWrite(g_chip_select_pin, HIGH);`).

## 10. Driver State Management
- Do not mirror device registers in software unless a datasheet explicitly requires write-read verification. Avoid shadow copies so firmware stays tightly coupled to the real hardware state; future optimisations can revisit this if a concrete benefit appears.

## 11. Commits
- Firmware commits use the `FW: Message` prefix.
- Pair code changes with matching tests in the same commit.
- Keep commits self-contained and limited to a single feature or fix whenever possible.
