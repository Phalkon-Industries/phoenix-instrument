# Phoenix Instrument Firmware – Contributor Checklist

Use this checklist whenever you begin work in this repository. It captures the norms that keep firmware, host tooling, and documentation consistent.

## 1. Read the style guide
- Start with `docs/style-guide.md`. It covers naming rules (for example, store function return values in `return_code`), pointer usage, comment expectations, and formatting patterns specific to our Arduino targets.

## 2. Understand the project layout
- Firmware lives under `src/` and reusable drivers in `lib/`.
- Shared headers are in `include/`.
- Unity-based tests reside in `test/`.
- Host scripts and Python utilities live in `python/`.
- Pin mappings and board-wide constants are defined in `src/main.hpp`; reference them instead of adding literals.

## 3. Practice strict TDD
- Add or update Unity tests **before** changing behavior.
- Ensure tests fail first, then implement the fix or feature.
- Avoid removing tests unless the documented requirements change; adjust names or expectations as needed.
- When a plan is required, consult `docs/tdd-workflow.md` for the expected structure.

## 4. Use the PlatformIO flows
- Build the main firmware with `pio run -e main`.
- Run targeted Unity suites while iterating (`pio test -e main -vv -f <pattern>`).
- Run the full Unity suite (`pio test -e main -vv`) before shipping significant work.
- Always invoke `pio` commands from the repository root—avoid `cd ... && pio ...` wrappers that bypass automation.
- Run `clang-format` on every modified C++ source or header before committing.
- After firmware changes, compile the affected environments (for example, `pio run -e phoenix_benchmark_example`) to prove they still build.

## 5. Test on real hardware
- Assume target boards are on your bench. Exercise new code on-device via PlatformIO.
- Do not introduce mocks or host-side simulators unless explicitly instructed.

## 6. Guard integrations
- Check existing modules in `include/` and `lib/` before adding new dependencies.
- Prefer extending the current drivers and helpers instead of introducing parallel implementations.

## 7. Explain the “why” in code
- Inline comments should justify non-obvious steps (for example, reset sequencing, timing delays, datasheet quirks).
- Update user-facing documentation or READMEs when visible behavior changes.

## 8. Commit cleanly
- Use merge-headline format `FW: <message>` when integrating to `main`.
- Keep topic-branch commits atomic and descriptive (see `docs/git-workflow.md` for details).
- Stage only related changes per commit and mention the validation you ran if it is not obvious.

## 9. Keep tests honest
- Never weaken or delete failing tests just to achieve a green run.
- Only modify test expectations when the documented requirements change, and explain why in your change notes.


Keep this checklist alongside `docs/git-workflow.md`, `docs/style-guide.md`, and `docs/tdd-workflow.md` whenever you begin a new task. Together they cover process, style, testing discipline, and environment requirements.
