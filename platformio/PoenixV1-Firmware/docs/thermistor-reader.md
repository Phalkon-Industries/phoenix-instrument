# Thermistor Reader — Ratiometric Measurement

The `thermistor_reader` module measures up to five NTC thermistors on the Tornado
board using a single shared 10 kΩ / 10 kΩ reference divider on ADC channel 7.
All thermistor dividers and the reference divider share the same 3.3 V rail and
the same 10 kΩ pull-up resistor, so the ratiometric technique cancels rail drift
without needing a precision voltage reference.

---

## Hardware Topology

```
3.3 V ─┬─ 10k ─┬─ ch7 (MCP3564)      3.3 V ─┬─ 10k ─┬─ chN (MCP3564)
               │                                    │
              10k                                 R_therm(N)
               │                                    │
              GND                                  GND

    Reference divider                     Thermistor divider (N = 2,3,4,5,6)
    code_ref ∝ V_ref                     code_sens ∝ V_sens
```

| Channel | Thermistor          | Model          |
| ------- | ------------------- | -------------- |
| ch2     | Gain stage          | Beta (3380 K)  |
| ch3     | LED drive stage     | Beta (3380 K)  |
| ch4     | Blue LED            | Steinhart-Hart |
| ch5     | Green LED           | Steinhart-Hart |
| ch6     | Sample (first read) | Steinhart-Hart |
| ch7     | Reference divider   | —              |

The thermistor rail is pulsed on only during measurement windows via
`PIN_THERMISTOR_ON` (pin 11) to avoid self-heating and wasted quiescent current.
A configurable settle delay (`settle_time_us`, default 2000 µs) allows the
rail and dividers to stabilise before sampling begins.

---

## Ratiometric Resistance Calculation

The ADC codes are proportional to the voltages at each divider:

$$V_{\text{ref}} = 3.3V \cdot \frac{R_{\text{ref}}}{R_{\text{pullup}} + R_{\text{ref}}}
= 3.3V \cdot \frac{10k}{10k + 10k} = \frac{3.3V}{2}$$

$$V_{\text{sens}}(N) = 3.3V \cdot \frac{R_{\text{therm}}(N)}{R_{\text{pullup}} + R_{\text{therm}}(N)}$$

Taking the ratio of the two ADC codes cancels the 3.3 V rail:

$$\frac{\text{code}_{\text{sens}}}{\text{code}_{\text{ref}}}
= \frac{V_{\text{sens}}}{V_{\text{ref}}}
= \frac{R_{\text{therm}}}{R_{\text{pullup}} + R_{\text{therm}}}
\cdot \frac{R_{\text{pullup}} + R_{\text{ref}}}{R_{\text{ref}}}$$

Define the **divider factor** (constant for a given board):

$$D_f = \frac{R_{\text{pullup}} + R_{\text{ref}}}{R_{\text{ref}}}
= \frac{10k + 10k}{10k} = 2.0$$

Solving for $R_{\text{therm}}$:

$$R_{\text{therm}} =
\frac{\frac{\text{code}_{\text{sens}}}{\text{code}_{\text{ref}}} \cdot R_{\text{pullup}}}
{D_f - \frac{\text{code}_{\text{sens}}}{\text{code}_{\text{ref}}}}$$

This is implemented in `thermistor_reader_compute_resistance()`:

```cpp
// thermistor_reader.cpp, Step 2
const float reference_ratio = (float)sensor_code / (float)reference_code;
const float divider_factor  = (pullup_ohms + reference_ohms) / reference_ohms;
const float denominator     = divider_factor - reference_ratio;
*resistance_out             = (reference_ratio * pullup_ohms) / denominator;
```

If the rail drifts — say from 3.30 V to 3.15 V — both `code_ref` and
`code_sens` scale down by the same factor, so `reference_ratio` stays constant.
The derived resistance (and therefore temperature) is immune to rail drift.

---

## Temperature Conversion Models

### Beta Equation (gain stage, LED drive stage)

$$\frac{1}{T} = \frac{1}{T_0} + \frac{1}{\beta} \cdot \ln\left(\frac{R}{R_{25}}\right)$$

Where $T_0 = 298.15\text{ K}$ (25 °C), $\beta = 3380\text{ K}$, and $R_{25} = 10\text{ k}\Omega$.

### Steinhart-Hart (sample, blue LED, green LED)

A piecewise 4-coefficient Steinhart-Hart model with four ratio segments:

$$\frac{1}{T} = A + B \ln(r) + C [\ln(r)]^2 + D [\ln(r)]^3$$

where $r = R / R_{25}$. The segment is selected by comparing $r$ against
`ratio_min_inclusive` thresholds:

| $r$ range | A             | B             | C              | D              |
| --------- | ------------- | ------------- | -------------- | -------------- |
| ≥ 3.274   | 3.3538646e-03 | 2.5654090e-04 | 1.9243889e-06  | 1.0969244e-07  |
| ≥ 0.36036 | 3.3540154e-03 | 2.5627725e-04 | 2.0829210e-06  | 7.3003206e-08  |
| ≥ 0.06831 | 3.3539264e-03 | 2.5609446e-04 | 1.9621987e-06  | 4.6045930e-08  |
| ≥ 0.0     | 3.3368620e-03 | 2.4057263e-04 | -2.6687093e-06 | -4.0719355e-07 |

Coefficients are from the thermistor manufacturer's datasheet for the 10 kΩ NTC
used in the water-side sensors.

---

## Measurement Sequence

### Single Thermistor (`thermistor_reader_measure_celsius`)

1. Energise the thermistor rail (`PIN_THERMISTOR_ON` → HIGH).
2. Wait `settle_time_us` for the rail and dividers to stabilise.
3. Sample the reference channel (ch7).
4. Sample the target sensor channel.
5. Compute resistance from the two codes (see §Ratiometric Resistance).
6. Convert resistance → temperature using the sensor's configured model.
7. Depower the rail (`PIN_THERMISTOR_ON` → LOW).

The rail is always depowered before returning, even on error — this is why
the code uses chained `if (result == OK)` checks instead of `GUARD` macros.

### Full Sweep (`thermistor_reader_measure_all`)

1. Energise the rail once.
2. Sample the reference channel (ch7) **once** — all sensor codes are
   converted against this single reference sample.
3. Sample sensors in order: ch6 → ch4 → ch5 → ch2 → ch3.
   The sample thermistor (ch6) is read first to limit its self-heating time.
4. Depower the rail.

Each sensor is converted independently; a failure on one channel marks only
that channel as invalid (`result.valid[N] = false`) without aborting the sweep.

---

## Configuration Reference

| Parameter                   | Tornado Value | Description                                       |
| --------------------------- | ------------- | ------------------------------------------------- |
| `reference_channel`         | ch7           | 10k/10k rail-proxy divider                        |
| `pullup_resistance_ohms`    | 10,000        | Shared pull-up for all dividers                   |
| `reference_resistance_ohms` | 10,000        | Lower-leg resistor in the reference divider       |
| `rail_enable_pin`           | 11            | GPIO toggled HIGH during measurement windows      |
| `settle_time_us`            | 2,000         | Delay after enabling the rail before ADC sampling |
| `adc_timeout_us`            | 100,000       | Maximum time to wait for each ADC conversion      |
| `beta_constant`             | 3380.0        | Shared Beta for gain-stage and LED-drive sensors  |
