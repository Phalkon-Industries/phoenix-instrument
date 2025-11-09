# Phoenix Instrument Firmware – TDD Workflow

Every firmware or host change in this repository follows a test-first loop. Use this guide to make sure your work stays in step with the rest of the team.

## 1. Design tests first
- Capture the desired behaviour in a Unity test (or Python test for host tooling) **before** touching production code.
- Focus on public APIs and observable side effects.
- Cover the happy path, guard rails, and failure modes in separate assertions.
- Use forward declarations or minimal includes if the types are not defined yet—tests should compile even if they fail.

## 2. Stub the implementation
- Add the required headers and source files with placeholder bodies so the project builds.
- Keep logic trivial (for example, return a sentinel value or short-circuit) so the new tests still fail.

## 3. Run the tests immediately
- Execute the narrowest Unity subset that exercises your change, such as:
  ```powershell
  pio test -e main -vv -f *led_router*
  ```
- For host-side benchmark work, run only the Phoenix benchmark Python suite:
  ```powershell
  conda run -n phoenix-python pytest -q python/tests/phoenix_benchmark
  ```
- Confirm the new tests fail. This “red” baseline protects the feedback loop.

## 4. Pause and review
- Share the failing tests and proposed API for feedback before writing the production logic.
- Adjust names, coverage, or ergonomics while the change set is still small.

## 5. Implement to make tests pass
- Flesh out the real implementation once the test plan is approved.
- Iterate until the previously failing tests turn green.
- Add additional tests when new edge cases appear, keeping the red → green → refactor rhythm tight.

## 6. Refactor with confidence
- With green tests in place, tidy the code: remove duplication, improve structure, and document any non-obvious decisions.
- Re-run the focused tests plus any relevant regression suites (for example, `pio test -e main -vv`) before merging.

## 7. Document the outcome
- Update READMEs, style guides, or protocol docs if behaviour visible to end users changes.
- Mention the test suites you ran in commit messages or review notes.

Following these steps ensures every behavioural change is intentional, reviewed, and backed by automated coverage before it lands on hardware.
