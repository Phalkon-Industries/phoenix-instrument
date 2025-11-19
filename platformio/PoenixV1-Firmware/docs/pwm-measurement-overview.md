# LED Router PWM Measurement Overview

This note captures how the Stormcloud LED router drives its TS5A3359 switch matrix with PWM and records the timing cues we rely on when aligning MCP356x conversions with the live router waveform.

## Hardware Context

- The router uses `NRF_PWM3` with the waveform programmed by `led_router_pwm_start`. Channel 0 (IN2) and channel 1 (IN1, inverted form) is set up so we can get LED on times for each colour, and the drain in between, with a simple PWM waveform. Channel assignments preserve the Stormcloud quirk that blue must precede green; the waveform presents green first, then a drain segment, then blue.
- The PWM sequence contains a single waveform entry that loops continuously. `NRF_PWM_EVENT_PWMPERIODEND` fires at the start of every cycle, independent of the sequence length, so we use it to increment the period counter consumed by the busy-waiting sweep helper.
- The MCP356x can only start conversions via the `MCP356X_FASTCMD_START` command delivered over SPI. DRDY remains on `PIN_ADC_IRQ` (GPIO 9) and asserts low when data is ready. The firmware now spins in a tight loop watching that GPIO instead of wiring an Arduino interrupt, so the conversion flow stays deterministic without extra ISR plumbing.
- The PWM sweep helper also polls the IN1/IN2 router pins directly (via `nrf_gpio_pin_read`) to detect when the waveform reaches the green, drain, and blue windows. This removes the dependency on Arduino pin-change interrupts and keeps the timing loop self-contained.

## Waveform Layout

The helper programs a 25% / 50% / 25% duty pattern that routes the photodiodes in a fixed order: green → drain → blue.

```
PWM counter:    0%         25%        50%        75%         100%
IN2 (CH0):      ┌── HIGH ──┐┌─ LOW ──────────────────────────┐┌
IN1 (CH1, inv): ┌ LOW ────────────────────────────┐┌── HIGH ─┐┌
Router state:   |  Green   |    Drain              | Blue     |
```

## Firmware API Expectations

- `light_readings_pwm_sweep_n` only observes the live waveform; production code (or tests) must call
	`led_router_pwm_start` ahead of time and leave the router under PWM control for the duration of the sweep.
- The helper now surfaces distinct Phoenix error codes so callers can triage setup issues quickly:
	- `LIGHT_READINGS_ERR_PWM_DISABLED` when PWM support is disabled in the cached light readings configuration.
	- `LIGHT_READINGS_ERR_PWM_NOT_CONFIGURED` when required metadata (pins, minimum period, timeout) is missing.
	- `LIGHT_READINGS_ERR_PWM_UNSUPPORTED_INSTANCE` when a PWM peripheral other than `NRF_PWM3` is wired in.
	- `LIGHT_READINGS_ERR_PWM_NOT_RUNNING` when the PWM engine is idle at the start of a sweep.
- The period watcher now backs up one cycle before waiting for the next boundary so an event that lands between
	`light_readings_pwm_enable_period_tracking` and the first wait call does not cause off-by-one sampling.

