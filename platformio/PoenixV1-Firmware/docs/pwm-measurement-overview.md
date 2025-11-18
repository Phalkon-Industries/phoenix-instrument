# LED Router PWM Measurement Overview

This note captures how the Stormcloud LED router drives its TS5A3359 switch matrix with PWM and records the timing cues we rely on when triggering MCP356x conversions from interrupts.

## Hardware Context

- The router uses `NRF_PWM3` with the waveform programmed by `led_router_pwm_start`. Channel 0 (IN2) and channel 1 (IN1, inverted form) is set up so we can get led on for each, and the drain inbetween with a simple pwm waveform. Channel assignments preserve the Stormcloud quirk that green must precede blue; the waveform opens with a drain segment in the middle.
- The PWM sequence contains a single waveform entry that loops continuously. `NRF_PWM_EVENT_PWMPERIODEND` fires at the start of every cycle, independent of the sequence length, so we treat it as the cycle reference for interrupt-driven sampling.
- The MCP356x can only start conversions via the `MCP356X_FASTCMD_START` command delivered over SPI. DRDY remains on `PIN_ADC_IRQ` (GPIO 9) and asserts low when data is ready if using IRQ method.

## Waveform Layout

The helper programs a 25% / 50% / 25% duty pattern that routes the photodiodes in a fixed order: drain → green → blue.

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
	- `LIGHT_READINGS_ERR_PWM_INTERRUPTS_UNAVAILABLE` when the configured router pins cannot raise Arduino change
		interrupts.
- The period watcher now backs up one cycle before waiting for the next boundary so an event that lands between
	`light_readings_pwm_enable_period_tracking` and the first wait call does not cause off-by-one sampling.

