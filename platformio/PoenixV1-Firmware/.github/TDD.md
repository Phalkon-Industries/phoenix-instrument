# Test-Driven Development Workflow

This firmware repo follows a strict test-first process for every feature and refactor. Use the checklist below whenever you touch the codebase.

1. **Design tests with the API in mind.**
   - Capture the proposed public surface in a Unity test file *before* creating or editing headers.
   - Describe each behaviour with clear assertions, covering success cases, guard rails, and observable side effects.
   - Keep the tests compiling by introducing forward declarations or placeholder includes as needed.

2. **Stub the implementation.**
   - Introduce the matching header/source pair with the new symbols but keep the bodies trivial (for example, return `LED_ROUTER_OK` or early-out without touching hardware).
   - Avoid real logic or state mutations at this stage; the goal is to make the project compile so the red test run is meaningful.

3. **Run the tests immediately.**
   - Execute the focused Unity suite (e.g. `pio test -e main -vv -f *led_router*`).
   - Confirm that the newly authored tests fail, establishing the red baseline the TDD loop relies on.

4. **Pause for review.**
   - Share the failing tests and proposed API for review *before* writing the real implementation.
   - Incorporate feedback on naming, coverage, and ergonomics while the code changes remain small.

5. **Implement to make tests pass.**
   - Once the tests and API are approved, flesh out the production code, iterating until the suite turns green.
   - Add or adjust tests as new edge cases emerge, keeping the red → green → refactor rhythm tight.

6. **Refactor with confidence.**
   - With green tests in place, tidy the implementation, remove duplication, and document reasoning without altering behaviour.
   - Re-run both targeted and regression suites before merging (for example, `pio test -e main -vv`).

7. **Document the outcome.**
   - Update READMEs, style guides, or protocol docs if behaviour visible to end users changes.
   - Mention the specific test suites you ran in commit messages or review notes.

Notes:
- Run `pio` commands from the repository root; avoid wrapping with `cd ... &&` to keep automation intact.
- Use targeted Unity filters while iterating (for example, `pio test -e main -vv -f *module_name*`) and run the full suite before merging.

Following these steps ensures every behavioural change is intentional, reviewed, and backed by automated coverage before it lands on hardware.