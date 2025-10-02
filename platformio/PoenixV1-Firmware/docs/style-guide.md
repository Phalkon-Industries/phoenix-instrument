# Phoenix Instrument Firmware Style Guide

This guide covers conventions **not** enforced automatically by `clang-format`. Follow these expectations whenever you introduce, rename, or refactor code.

## 1. Naming Conventions
- **Files:** Use lower_snake_case (e.g. `mcp356x.cpp`, `unity_config.cpp`).
- **Namespaces:** Avoid unless interacting with Arduino libraries; prefer `static` linkage instead.
- **Types / structs / enums / classes:** `CamelCase` (upper camel). Example: `struct AdcSampleBuffer`.
- **Functions and free helpers:** `lower_snake_case`. Example: `read_config_value()`
- **Unity test names:** Use clear test identifiers in lower_snake_case following Unity conventions—prefer `test_action` or `test_unit_action` so the name describes the behavior and expected result (e.g., `test_adc_read_returns_error_on_null_buffer` or `test_config_load_sets_defaults`).
- **Static globals:** Prefix with `g_` (e.g. `g_initialized`). Prefer file-local `static` over `extern`.
- **Local variables:** `lower_snake_case` (e.g. `config0_before`, `status_after`).
- **Constants:**
  - C macros: `UPPER_SNAKE_CASE` (`MCP356X_STATUS_DR_MASK`).
  - File- or function-local `static const` values: prefix with `k_` and keep the remainder lower_snake_case (e.g. `k_spi_clock_hz`).
- **Enum values:** `UPPER_SNAKE_CASE` to match existing register definitions.

## 2. General C++ Practices
- Prefer explicit types over `auto` unless the type is enforced by the API (e.g. iterator loops).
- Keep each function narrowly scoped; document any required side effects or shared state.
- Run `clang-format` on every file you touch before committing so the codebase stays consistent with `.clang-format`. Recommend turning on "format on save" in IDE settings.

## 3. Header Layout and Includes
- Include the module's public header first (e.g. `#include "mcp356x.hpp"`), then a blank line, then Arduino/standard-library headers.
- Avoid duplicate includes; keep headers idempotent via `#ifndef` guards named `FILE_BASENAME_HPP`.

## 4. Documentation Expectations
- Public-facing APIs declared in headers use Doxygen-style blocks describing purpose, parameters, return codes, and side effects.
- Internal helpers still carry concise doc comments so IDE tooltips remain informative; focus on intent, inputs, outputs, and notable side effects.
- Inline comments explain **why** a step exists (datasheet citation, timing requirement), not what the code already makes obvious.
- Sections may be separated by banner comments (`// ===================== ... =====================`) when grouping related registers or constants.

## 5. Error Handling Patterns
- Return early on invalid arguments or runtime guardrails (`if (buffer == NULL) return MCP356X_ERR_INVALID_ARG;`).
- Propagate driver error codes unchanged so tests can assert on them.
- Prefer small helper functions over complex branching; for example, register framing lives in `mcp356x_command_byte`.

## 6. Test Structure
- Unity test files live under `test/<suite_name>/`. Each test function is `static void test_<scenario>(void)`.
- Use Unity's `setUp()` and `tearDown()` hooks (the suite constructor/destructor) to establish and reset shared state instead of duplicating initialization logic in every test.
- All hardware state modifications are undone before exit (reset registers, restore mux values, etc.)—prefer placing this work in `tearDown()` when consistent across tests.
- `setup()` must call `UNITY_SETUP_SERIAL_DEFAULT();` before `RUN_TEST` calls and `UNITY_END();` to balance initialization.
- When iterating, use explicit bounds/limits (`for (int attempt = 0; attempt < 10; ++attempt)`), not `while(true)`.
- Always include `<unity.h>` before project headers and standard library headers used by the test.

## 7. Magic Numbers & Constants
- Replace repeated literals with named constants either in headers (`#define MCP356X_EXPECTED_STATUS_START 0x17u`) or local `const` values.
- Delay values should be explained (e.g. `delay(2);  // allow registers to settle after POR reset`).

## 8. Pointer & Ownership Conventions
- Driver APIs accept raw pointers; validate them (`result == NULL`) before use.
- Use stack buffers and avoid dynamic allocation. Multi-byte transfers rely on fixed-size arrays (`uint8_t adc_bytes[3]`).
- When a pointer argument is optional, document that passing `NULL` is allowed.

## 9. Arduino-Specific Practices
- Always drive chip-select pins to a known idle state immediately after `pinMode` (`digitalWrite(g_chip_select_pin, HIGH);`).

## 10. Commits
- Firmware commits use the `FW: Message` prefix.
- Pair code changes with matching tests in the same commit.
- Keep commits self-contained and limited to a single feature or fix whenever possible.

