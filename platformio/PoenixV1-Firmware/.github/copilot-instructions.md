# Phoenix Instrument Firmware – AI Contributor Guide

1. **Read the style guide.** `docs/style-guide.md`, duplicated below as well, defines naming, documentation depth (including helper comments), pointer rules, formatting expectations, and Arduino-specific patterns not enforced by tooling. Follow the naming guidance exactly—for example, capture function return values in a variable named `return_code` rather than the shorthand `rc`.
	- Naming quick refs: files/use `lower_snake_case`; types/enums/structs/classes use `CamelCase`; functions and local variables use `lower_snake_case`; static globals are prefixed with `g_`.
	- Reserve `_test_` in symbol names for helpers consumed exclusively by Unity/unit tests. If production code needs the helper, rename it to drop the `_test_` qualifier before using it elsewhere.
	- Constants: macros use `UPPER_SNAKE_CASE`; file- or function-local constants use `k_` + `lower_snake_case` (e.g. `k_spi_clock_hz`); enum values use `UPPER_SNAKE_CASE`.
	- Namespaces are generally avoided; prefer `static` linkage unless integrating with Arduino/third-party quirks that require namespaces.
	- Guard helpers live in `lib/phoenix_common/phoenix_guard.hpp`; prefer the shared `GUARD`/`GUARD_NONNULL`/`GUARD_INITIALIZED` macros over ad-hoc pointer checks so modules surface consistent Phoenix error codes.
	- Phoenix Benchmark public APIs: export `phoenix_benchmark_<module>_<verb>` C-style entry points (e.g. `phoenix_benchmark_channel_map_run`) and match structs/enums with `PhoenixBenchmark<Module><Role>` (e.g. `PhoenixBenchmarkChannelMapDefaults`). Keep these in plain headers without namespaces for clean C/C++ linkage.
	- Unity tests: name tests in descriptive `lower_snake_case` (e.g. `test_adc_read_returns_error_on_null_buffer`).
Read the style guide and confirm your updates and changes match the style guide, making changes as needed, before returning.
2. **Review the git workflow before you open a branch.** `.github/git-workflow-copilot.md` captures the assistant-specific summary; the full, human-facing guide lives in `docs/git-workflow.md`. Refresh both when you start a feature or bugfix so branch names, commit styles, and merge headlines stay consistent.
3. **Know the layout.** Application entry points live under `src/`, reusable drivers and helpers under `lib/`, and Unity-based integration tests under `test/`. Pin mappings and board-wide constants live in `src/main.hpp`; reuse them instead of literal values.
4. **Follow strict TDD workflows.** Always add or update Unity tests before changing behavior or adding functionality. Tests must fail before completing the implementation. Avoid deleting tests; only adjust them when names or behavior change. When asked to implement new features or plan new libraries, review and follow `.github/TDD.md`; reference `docs/tdd-workflow.md` when summarizing steps for human contributors.
	- Unity test structure: test functions are `static void test_<scenario>(void)` and live under `test/<suite_name>/`.
	- Use `setUp()`/`tearDown()` for shared initialization/reset instead of duplicating logic; ensure hardware state is restored on exit.
		- Include order in tests: include the Unity header first (`unity.h`), then project headers, then any standard/Arduino headers.
	- Ensure `setup()` calls `UNITY_SETUP_SERIAL_DEFAULT();` before `RUN_TEST` calls and that `UNITY_END();` is balanced.
5. **Use PlatformIO flows.** Build with `pio run -e main`. After firmware or driver edits, prefer targeted Unity runs during iteration (e.g. `pio test -e main -vv -f *mcp356x*`) and keep full-suite runs (`pio test -e main -vv`) as a pre-commit gate. Invoke PlatformIO from the workspace root and call `pio` commands directly—do **not** prepend a `cd ... &&` prefix, because that breaks the auto-approve command list. Run `clang-format` on every modified code file before committing (not text or markdown files). Before declaring a phase complete or handing off firmware work, compile the affected PlatformIO environments (for example `pio run -e phoenix_benchmark_example`) to prove the sources build cleanly. **Always execute the relevant PlatformIO tests and capture their outcome before returning responses that claim code is ready.** Also follow header hygiene: include the module's public header first, then a blank line, then Arduino/standard-library headers; use include guards named `FILE_BASENAME_HPP`. If a header must be consumable from both C and C++, wrap declarations with the usual `extern "C"` guards.
6. **Test on real hardware.** Assume the target boards are on your desk—exercise new code directly on-device via PlatformIO; do not introduce mocks, fakes, or host-side simulators unless explicitly instructed.
7. **Guard integrations.** Check existing modules in `include/` and `lib/` before adding new dependencies; prefer extending current drivers.
8. **Explain the why.** Inline comments must spell out the rationale for non-obvious steps (resets before fast commands, timing delays, datasheet caveats). Write them as one-line sentences and let `clang-format` wrap. Use explicit `Step N:` prefixes for multi-step workflows (e.g. `// Step 2: Sample drain channel to clear residual charge.`); use `Step 2a/2b` for sub-steps. Banner comments are acceptable when grouping related registers/constants. Update documentation or READMEs whenever behavior visible to users shifts.
	- For every file-local helper (`static` function) implemented outside headers, add a short leading comment summarising why the helper exists; reviewers rely on this to understand intent quickly. Header-exposed functions already documented with Doxygen blocks do not need the extra helper comment.
9. **Work to the plan.** When you’re executing work against a written plan or checklist, update the plan’s checkboxes or status notes as you complete each step. If a plan is requested, look for the relevant `plan.md` under `.github/plans/`; if none exists, create a new plan file in that directory and document the steps there. When planning, always plan to implement TDD workflow by thinking with test cases first and then implementation. Always stop at the end of a phase of the plan for further instruction before continuing onto or completing next phase steps.
10. **Commit cleanly.** Use the `FW: Message` format, stage only related changes, and cite the tests you ran if not obvious. Pair code changes with matching tests in the same commit, and keep commits self-contained and focused.
11. **Ask when unsure.** Surface assumptions about hardware or workflows rather than guessing—physical devices make correctness critical. When a build or test fails due to missing headers or configuration issues, investigate root causes, and provide guidance or ask the maintainer for guidance instead of layering new files or brute-force workarounds.
12. **Keep tests honest.** Never relax, delete, or tamper with failing tests just to achieve a passing run. Only change tests when the documented requirements themselves change, and call out the rationale explicitly.
13. **Use the shared Python environment.** Host tooling lives under `python/`. Create or update the `phoenix-python` conda/mamba environment with `conda env update -f python/environment.yml -n phoenix-python`. When working on benchmark features, run the focused suite with `conda run -n phoenix-python pytest -q python/tests/phoenix_benchmark`; reserve the mock BLE suite (`conda run -n phoenix-python pytest -q python/tests/mock_ble`) for BLE-specific changes. Do not install packages globally; add them to `python/environment.yml` and `pyproject.toml` instead.
14. **Eliminate magic numbers.** When a literal (especially hex masks or register values) appears, either replace it with a named constant or add an inline comment that cites the datasheet section producing the value. Prefer extracting repeated literals into named constants in headers or local `const` values.
15. **Audit comments before replying.** Double-check that every modified source and test file carries the style-guide-compliant documentation (including Step-prefixed inline comments where required, and full doxygen comments for header functions) before returning a response that concludes the work.

16. **Error handling patterns.** Return early on invalid arguments or guardrails; propagate driver error codes unchanged so tests can assert on them; prefer small helpers over complex branching.

17. **Pointer and ownership conventions.** Driver APIs accept raw pointers; validate them (`ptr == NULL`) before use. Prefer stack buffers and fixed-size arrays over dynamic allocation. When a pointer argument is optional, document that `NULL` is allowed.

18. **Arduino-specific practices.** Always drive chip-select pins to a known idle state immediately after `pinMode` (e.g. `digitalWrite(g_chip_select_pin, HIGH);`).

19. **Driver state management.** Avoid mirroring device registers in software unless the datasheet requires write-read verification. Keep firmware tightly coupled to real hardware state; consider optimizations only with measured benefit.


====
Style Guide
====
# Phoenix Instrument Firmware Style Guide

This guide covers conventions not enforced automatically by `clang-format`. Follow these expectations whenever you introduce, rename, or refactor code.

## 1. Naming Conventions
- **Files:** Use `lower_snake_case` (e.g. `mcp356x.cpp`, `unity_config.cpp`).
- **Namespaces:** Avoid unless interacting with Arduino libraries; prefer `static` linkage instead.
- **Types / structs / enums / classes:** Use `CamelCase` (upper camel). Example: `struct AdcSampleBuffer`.
- **Functions and free helpers:** Use `lower_snake_case`. Example: `read_config_value()`.
- **Test-only helpers:** Reserve `_test_` in symbol names (functions, globals, or accessors) for code exercised exclusively by Unity/unit tests. If production sources need the helper, rename it to drop `_test_` before promoting it to shared use.
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
- Give every internal helper or module-level variable internal linkage with `static`; avoid anonymous namespaces so the
  linkage intent stays obvious to C and C++ consumers alike.

## 3. Header Layout and Includes
- Include the module's public header first (e.g. `#include "mcp356x.hpp"`), then a blank line, then Arduino/standard-library headers.
- Avoid duplicate includes; keep headers idempotent via `#ifndef` guards named `FILE_BASENAME_HPP`.

## 4. Documentation Expectations
- Public-facing APIs declared in headers use Doxygen-style blocks describing purpose, parameters, return codes, and side effects.
- Internal helpers still carry concise doc comments so IDE tooltips remain informative; focus on intent, inputs, outputs, and notable side effects.
- File-local helpers implemented as `static` functions must include a brief leading comment (one sentence is sufficient) explaining the helper's purpose and why it exists. When the function already carries a Doxygen block in a header, the additional helper comment is not required.
- Inline comments describe the processing step in plain English before the code executes it. Summarize what the step accomplishes and why it belongs here; prefer one full sentence and let `clang-format` wrap.
- Start sequential comments with an explicit `Step N:` prefix (use suffixes `Step 2a`, `Step 2b`, etc. for sub-steps) so readers can follow the ordered progression (`// Step 2: Sample drain channel to clear residual charge.`). Single, stand-alone comments that are not part of a sequence may omit the prefix, but multi-line workflows must use it consistently.
- Avoid restating the obvious expression; explain the intent, constraints, datasheet references, or timing notes that justify the implementation.
- Use banner comments (`// ===================== ... =====================`) when grouping related registers or constants.

## 5. Error Handling Patterns
- Return early on invalid arguments or runtime guardrails (e.g. `if (buffer == NULL) return MCP356X_ERR_INVALID_ARG;`).
- Prefer the shared guard macros from `lib/phoenix_common/phoenix_guard.hpp` (`GUARD`, `GUARD_NONNULL`, `GUARD_INITIALIZED`) for pointer validation and dependency checks so modules share consistent return codes.
- Propagate driver error codes unchanged so tests can assert on them.
- Prefer small helper functions over complex branching; for example, register framing lives in `mcp356x_command_byte`.

## 6. Test Structure
- Unity test files live under `test/<suite_name>/`. Each test function is `static void test_<scenario>(void)`.
- Use Unity's `setUp()` and `tearDown()` hooks to establish and reset shared state instead of duplicating initialization logic in every test.
- Ensure all hardware state modifications are undone before exit (reset registers, restore mux values, etc.). Prefer placing this work in `tearDown()` when consistent across tests.
- `setup()` must call `UNITY_SETUP_SERIAL_DEFAULT();` before `RUN_TEST` calls and `UNITY_END();` to balance initialization.
- Every Arduino Unity sketch includes `<Adafruit_TinyUSB.h>` before `<Arduino.h>` so the USB stack is ready when `UNITY_SETUP_SERIAL_DEFAULT()` opens the port. The typical include order is:
  ```c++
  #include <unity.h>
  #include "unity_config.h"
  #include <Adafruit_TinyUSB.h>
  #include <Arduino.h>
  ```
- The `setup()`/`loop()` harness uses the shared macro defined in `test/unity_config.h`:
  ```c++
  void setup() {
    UNITY_SETUP_SERIAL_DEFAULT();
    RUN_TEST(test_my_feature_behaves);
    UNITY_END();
  }

  void loop() {
    // Unity tests run once; leave loop empty.
  }
  ```
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
