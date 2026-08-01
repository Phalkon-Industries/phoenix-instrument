#include "light_readings.hpp"

#include "digipot_hal.hpp"
#include "phoenix_guard.hpp"
#include "power_control.hpp"
#include <Arduino.h>
#include <hal/nrf_gpio.h>
#include <limits.h>
#include <math.h>
#include <nrf.h>
extern const uint32_t g_ADigitalPinMap[];

static LightReadingsConfig g_light_config                   = {};
static bool                g_is_initialized                 = false;
static bool                g_last_sweep_detected_saturation = false;
static bool                g_force_saturation_for_test      = false;

static constexpr uint8_t k_light_readings_blue_channel  = 0u;
static constexpr uint8_t k_light_readings_green_channel = 1u;
// MCP3564 full-scale boundary codes as documented in datasheet DS20006204.
static constexpr int32_t k_light_readings_adc_positive_full_scale_code = 8388607;
static constexpr int32_t k_light_readings_adc_negative_full_scale_code = -8388608;

LightReadingsSweepSample g_light_readings_sweep_storage[LIGHT_READINGS_MAX_SWEEP_COUNT] = {};

// Helper: Check whether an ADC code sits at or beyond the converter saturation limits.
static bool light_readings_is_code_saturated(int32_t code) {
  return (code >= k_light_readings_adc_positive_full_scale_code) ||
         (code <= k_light_readings_adc_negative_full_scale_code);
}

struct LightReadingsRunningStats {
  uint32_t sample_count;
  double   mean;
  double   m2;
  int32_t  min_value;
  int32_t  max_value;
  double   sum_x;
  double   sum_y;
  double   sum_xy;
  double   sum_xx;
};

// Helper: Reset running-statistics bookkeeping before accumulating samples.
static void light_readings_running_stats_reset(LightReadingsRunningStats* stats) {
  stats->sample_count = 0u;
  stats->mean         = 0.0;
  stats->m2           = 0.0;
  stats->min_value    = INT32_MAX;
  stats->max_value    = INT32_MIN;
  stats->sum_x        = 0.0;
  stats->sum_y        = 0.0;
  stats->sum_xy       = 0.0;
  stats->sum_xx       = 0.0;
}

// Helper: Incorporate an additional sample into a running-statistics accumulator.
static void light_readings_running_stats_update(LightReadingsRunningStats* stats, uint32_t sample_index,
                                                int32_t value) {
  ++stats->sample_count;

  // Step 1: Update the running mean and second moment using Welford's algorithm.
  const double double_value = static_cast<double>(value);
  const double delta        = double_value - stats->mean;
  stats->mean += delta / static_cast<double>(stats->sample_count);
  const double delta2 = double_value - stats->mean;
  stats->m2 += delta * delta2;

  // Step 2: Accumulate regression-friendly sums for drift calculations.
  const double double_index = static_cast<double>(sample_index);
  stats->sum_x += double_index;
  stats->sum_y += double_value;
  stats->sum_xy += double_index * double_value;
  stats->sum_xx += double_index * double_index;

  // Step 3: Track the extremal values observed across the samples.
  if (value < stats->min_value) {
    stats->min_value = value;
  }
  if (value > stats->max_value) {
    stats->max_value = value;
  }
}

struct LightReadingsPwmState {
  bool              sweep_active;
  NRF_PWM_Type*     pwm_instance;
  IRQn_Type         pwm_irq;
  uint8_t           in1_pin;
  uint8_t           in2_pin;
  uint32_t          in1_nrf_pin;
  uint32_t          in2_nrf_pin;
  uint32_t          period_timeout_us;
  volatile uint32_t period_counter;
};

static LightReadingsPwmState       g_pwm_state                  = {};
static LightReadingsPwmDiagnostics g_pwm_diagnostics            = {};
static bool                        g_force_pwm_timeout_for_test = false;

// Helper: Calculate the least-squares slope for the accumulated samples.
static double light_readings_calculate_slope(const LightReadingsRunningStats& running_stats) {
  // Step 1: Reject slope calculations that lack enough samples for a regression.
  if (running_stats.sample_count < 2u) {
    return 0.0;
  }

  // Step 2: Evaluate the denominator and guard against near-singularities.
  const double sample_count = static_cast<double>(running_stats.sample_count);
  const double denominator  = (sample_count * running_stats.sum_xx) - (running_stats.sum_x * running_stats.sum_x);
  if (fabs(denominator) < 1e-12) {
    return 0.0;
  }

  // Step 3: Compute and return the least-squares slope across the sample indices.
  const double numerator = (sample_count * running_stats.sum_xy) - (running_stats.sum_x * running_stats.sum_y);
  return numerator / denominator;
}

// Helper: Translate running-statistics state into the caller-visible summary structure.
static void light_readings_populate_summary(const LightReadingsRunningStats& running_stats,
                                            LightReadingsStatisticSummary*   summary_out) {
  // Step 1: Populate scalar counts and mean-based metrics.
  summary_out->sample_count = running_stats.sample_count;
  summary_out->has_samples  = running_stats.sample_count > 0u;
  summary_out->mean         = summary_out->has_samples ? running_stats.mean : 0.0;
  summary_out->min_value    = summary_out->has_samples ? running_stats.min_value : 0;
  summary_out->max_value    = summary_out->has_samples ? running_stats.max_value : 0;
  summary_out->drift_slope  = summary_out->has_samples ? light_readings_calculate_slope(running_stats) : 0.0;

  // Step 2: Derive the standard deviation when the sample set supports it.
  if (running_stats.sample_count > 1u) {
    summary_out->standard_deviation = sqrt(running_stats.m2 / static_cast<double>(running_stats.sample_count - 1u));
  }
  else {
    summary_out->standard_deviation = 0.0;
  }
}

static constexpr IRQn_Type k_light_readings_invalid_pwm_irq = static_cast<IRQn_Type>(-1);

static int light_readings_wait_for_next_period(uint32_t current_period, uint32_t timeout_us, uint32_t* next_period_out);

// Helper: Sample the router state directly from the PWM-driven GPIO levels.
static LedRouterState light_readings_sample_router_state(void) {
  const uint32_t in1_level = nrf_gpio_pin_read(g_pwm_state.in1_nrf_pin);
  const uint32_t in2_level = nrf_gpio_pin_read(g_pwm_state.in2_nrf_pin);

  if ((in1_level != 0u) && (in2_level != 0u)) {
    return LedRouterState::LED_ROUTER_STATE_DRAIN;
  }
  if ((in1_level != 0u) && (in2_level == 0u)) {
    return LedRouterState::LED_ROUTER_STATE_GREEN;
  }
  if ((in1_level == 0u) && (in2_level != 0u)) {
    return LedRouterState::LED_ROUTER_STATE_BLUE;
  }

  return LedRouterState::LED_ROUTER_STATE_OFF;
}

// Helper: Wait for the PWM waveform to enter a specific router state within the current period.
static int light_readings_wait_for_router_state(uint32_t period_index, LedRouterState target_state,
                                                uint32_t timeout_us) {
  const uint32_t start_us = micros();

  while (true) {
    if (light_readings_sample_router_state() == target_state) {
      return LIGHT_READINGS_OK;
    }

    if (g_pwm_state.period_counter > period_index) {
      return LIGHT_READINGS_ERR_MISSED_CYCLE;
    }

    if ((timeout_us > 0u) && ((micros() - start_us) >= timeout_us)) {
      return LIGHT_READINGS_ERR_TIMEOUT;
    }
  }
}

// Helper: Arm the PWM period-end interrupt so sweeps can detect cycle boundaries.
static void light_readings_pwm_enable_period_tracking(void) {
  // Step 1: Skip configuration when no PWM instance has been staged.
  if (g_pwm_state.pwm_instance == nullptr) {
    return;
  }

  // Step 2: Clear lingering events and enable the period-end interrupt on the active PWM instance.
  g_pwm_state.pwm_instance->EVENTS_PWMPERIODEND = 0u;
  g_pwm_state.pwm_instance->EVENTS_STOPPED      = 0u;
  g_pwm_state.pwm_instance->INTENCLR            = 0xFFFFFFFFu;
  g_pwm_state.pwm_instance->INTENSET            = PWM_INTENSET_PWMPERIODEND_Msk;

  // Step 3: Arm the NVIC entry so the IRQ handler records each PWM period boundary.
  if (g_pwm_state.pwm_irq != k_light_readings_invalid_pwm_irq) {
    NVIC_ClearPendingIRQ(g_pwm_state.pwm_irq);
    NVIC_SetPriority(g_pwm_state.pwm_irq, 5u);
    NVIC_EnableIRQ(g_pwm_state.pwm_irq);
  }
}

// Helper: Mask PWM period tracking to leave the hardware in a quiet state.
static void light_readings_pwm_disable_period_tracking(void) {
  // Step 1: Ignore disable requests when no PWM instance is latched.
  if (g_pwm_state.pwm_instance == nullptr) {
    return;
  }

  // Step 2: Mask the period-end interrupt and release the NVIC slot if it was enabled.
  g_pwm_state.pwm_instance->INTENCLR = PWM_INTENCLR_PWMPERIODEND_Msk;

  if (g_pwm_state.pwm_irq != k_light_readings_invalid_pwm_irq) {
    NVIC_DisableIRQ(g_pwm_state.pwm_irq);
  }
}

// Helper: Reset PWM observation state so future sweeps start from a blank slate.
static void light_readings_pwm_clear_state(void) {
  // Step 1: Drop the active flag and period tracking hooks.
  g_pwm_state.sweep_active = false;
  light_readings_pwm_disable_period_tracking();

  // Step 2: Clear cached PWM metadata so future sweeps start from a blank slate.
  g_pwm_state.pwm_instance      = nullptr;
  g_pwm_state.pwm_irq           = k_light_readings_invalid_pwm_irq;
  g_pwm_state.in1_pin           = 0u;
  g_pwm_state.in2_pin           = 0u;
  g_pwm_state.in1_nrf_pin       = 0u;
  g_pwm_state.in2_nrf_pin       = 0u;
  g_pwm_state.period_timeout_us = 0u;
  g_pwm_state.period_counter    = 0u;
}

// Helper: Prepare PWM observation bookkeeping so sweeps can poll the waveform in the foreground.
static int light_readings_pwm_prepare_observation(NRF_PWM_Type* pwm_instance) {
  // Step 1: Validate the PWM instance reference before caching any state.
  if (pwm_instance == nullptr) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Cache the active PWM resources and pin mappings for foreground polling.
  g_pwm_state.sweep_active      = true;
  g_pwm_state.pwm_instance      = pwm_instance;
  g_pwm_state.pwm_irq           = (pwm_instance == NRF_PWM3) ? PWM3_IRQn : k_light_readings_invalid_pwm_irq;
  g_pwm_state.in1_pin           = static_cast<uint8_t>(g_light_config.pwm_config.router_in1_pin);
  g_pwm_state.in2_pin           = static_cast<uint8_t>(g_light_config.pwm_config.router_in2_pin);
  g_pwm_state.in1_nrf_pin       = g_ADigitalPinMap[g_pwm_state.in1_pin];
  g_pwm_state.in2_nrf_pin       = g_ADigitalPinMap[g_pwm_state.in2_pin];
  g_pwm_state.period_timeout_us = g_light_config.pwm_config.period_timeout_us;
  g_pwm_state.period_counter    = 0u;

  return LIGHT_READINGS_OK;
}

// Helper: Wait for the next PWM period, enforcing a timeout so sweeps cannot hang indefinitely.
static int light_readings_wait_for_next_period(uint32_t current_period, uint32_t timeout_us,
                                               uint32_t* next_period_out) {
  // Step 1: Bookmark the start time so the polling loop can enforce the timeout budget.
  const uint32_t start_us = micros();

  while (true) {
    // Step 2: Publish the next period index once the PWM IRQ increments its counter.
    const uint32_t observed_period = g_pwm_state.period_counter;
    if (observed_period > current_period) {
      if (next_period_out != NULL) {
        *next_period_out = observed_period;
      }
      return LIGHT_READINGS_OK;
    }

    // Step 3: Abort the wait when the caller-specified timeout window expires.
    if ((timeout_us > 0u) && ((micros() - start_us) >= timeout_us)) {
      return LIGHT_READINGS_ERR_TIMEOUT;
    }

    // Step 4: Continue spinning so the next ISR-observed period is captured immediately.
  }
}

// Helper: Capture a single ADC conversion through the IRQ read path and update optional diagnostics.
static int light_readings_capture_channel_irq(AdcHalChannel channel, int32_t* code_out, uint32_t* counter_out,
                                              bool* saw_saturation) {
  // Step 1: Reject calls that omit the destination storage for the captured sample.
  if (code_out == NULL) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Surface forced timeout behaviour used by hardware tests.
  if (g_force_pwm_timeout_for_test) {
    g_pwm_diagnostics.missed_drdy_count += 1u;
    return LIGHT_READINGS_ERR_TIMEOUT;
  }

  // Step 3: Launch the interrupt-driven ADC conversion and guard for timeout results.
  const int return_code = adc_hal_read_channel_irq(channel, g_light_config.adc_timeout_us, code_out);
  if (return_code == ADC_HAL_ERR_TIMEOUT) {
    g_pwm_diagnostics.missed_drdy_count += 1u;
    return LIGHT_READINGS_ERR_TIMEOUT;
  }

  // Step 4: Bubble up backend-specific failures unchanged so callers can triage hardware faults.
  if (return_code != ADC_HAL_OK) {
    return return_code;
  }

  // Step 5: Track saturation metadata for the overall sweep result when requested.
  if (saw_saturation != NULL) {
    *saw_saturation |= light_readings_is_code_saturated(*code_out);
  }

  // Step 6: Update diagnostics so tests can assert conversion sequencing.
  if (counter_out != NULL) {
    *counter_out += 1u;
  }

  return LIGHT_READINGS_OK;
}

static int light_readings_capture_pwm_cycle(uint32_t period_index, LightReadingsSweepSample* sample,
                                            bool* saw_saturation) {
  // Step 1: Wait for the green illumination window and capture the direct reading.
  GUARD(light_readings_wait_for_router_state(period_index, LedRouterState::LED_ROUTER_STATE_GREEN,
                                             g_pwm_state.period_timeout_us));

  // Step 2: Honour the configured dwell period before sampling the illuminated photodiode.
  if (g_light_config.green_channel.dwell_us > 0u) {
    delayMicroseconds(g_light_config.green_channel.dwell_us);
  }

  // Step 3: Capture the green-channel conversion and propagate saturation metadata.
  GUARD(light_readings_capture_channel_irq(g_light_config.green_channel.adc_channel, &sample->green_code,
                                           &g_pwm_diagnostics.green_conversion_count, saw_saturation));

  // Step 4: Enter the drain window and sample both photodiodes for the dark reference.
  GUARD(light_readings_wait_for_router_state(period_index, LedRouterState::LED_ROUTER_STATE_DRAIN,
                                             g_pwm_state.period_timeout_us));

  // Step 5: Capture drain-state conversions for the green and blue channels.
  GUARD(light_readings_capture_channel_irq(g_light_config.green_channel.adc_channel, &sample->drain_green_code, NULL,
                                           saw_saturation));
  GUARD(light_readings_capture_channel_irq(g_light_config.blue_channel.adc_channel, &sample->drain_blue_code, NULL,
                                           saw_saturation));
  g_pwm_diagnostics.drain_read_count += 1u;

  // Step 6: Wait for the blue illumination window and capture the direct reading.
  GUARD(light_readings_wait_for_router_state(period_index, LedRouterState::LED_ROUTER_STATE_BLUE,
                                             g_pwm_state.period_timeout_us));

  // Step 7: Honour the blue-channel dwell before sampling the illuminated photodiode.
  if (g_light_config.blue_channel.dwell_us > 0u) {
    delayMicroseconds(g_light_config.blue_channel.dwell_us);
  }

  // Step 8: Capture the blue-channel conversion and propagate saturation metadata.
  GUARD(light_readings_capture_channel_irq(g_light_config.blue_channel.adc_channel, &sample->blue_code,
                                           &g_pwm_diagnostics.blue_conversion_count, saw_saturation));

  return LIGHT_READINGS_OK;
}

int light_readings_initialize(const LightReadingsConfig* config) {
  // Step 1: Validate the configuration pointer before touching shared state.
  GUARD_NONNULL(config);

  // Step 2: Park the router in the configured drain state so readings start from a known baseline.
  GUARD(led_router_set_state(config->drain_state));

  // Step 3: Program the digi-pot so colour-specific LED drive strengths start from a calibrated baseline.
  GUARD(digipot_blue_set_wiper(config->blue_channel.wiper_code));
  GUARD(digipot_green_set_wiper(config->green_channel.wiper_code));

  // Step 4: Cache the configuration so subsequent operations can reuse it without copying.
  g_light_config   = *config;
  g_is_initialized = true;
  return LIGHT_READINGS_OK;
}

int light_readings_sweep(LightReadingsSweepSample* sweep_out) {
  // Step 1: Validate output storage.
  GUARD_NONNULL(sweep_out);

  // Step 2: Ensure the helper has been initialised before manipulating hardware.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2.5: Verify the 5V rail and LM7705 generator are asserted before routing LEDs.
  if (!power_control_led_power_is_ready()) {
    return LIGHT_READINGS_ERR_POWER_NOT_READY;
  }

  bool drain_blue_saturated        = false;
  bool drain_green_saturated       = false;
  bool blue_saturated              = false;
  bool green_saturated             = false;
  g_last_sweep_detected_saturation = false;

  // Step 3: Enter the drain state so both photodiodes share the drain path for the first measurements.
  GUARD(led_router_set_state(g_light_config.drain_state));

  // Step 4: Sample the green photodiode without altering the routing state.
  GUARD(adc_hal_read_single_ended(g_light_config.green_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->drain_green_code));

  // Step 5: Sample the blue photodiode while the router remains in the drain configuration.
  GUARD(adc_hal_read_single_ended(g_light_config.blue_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->drain_blue_code));
  // Step 6: Route to the blue photodiode, honour its dwell period, and capture a direct reading.
  GUARD(led_router_set_state(g_light_config.blue_channel.router_state));
  if (g_light_config.blue_channel.dwell_us > 0u) {
    delayMicroseconds(g_light_config.blue_channel.dwell_us);
  }
  GUARD(adc_hal_read_single_ended(g_light_config.blue_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->blue_code));
  // Step 7: Route to the green photodiode, honour its dwell period, and capture a direct reading.
  GUARD(led_router_set_state(g_light_config.green_channel.router_state));
  if (g_light_config.green_channel.dwell_us > 0u) {
    delayMicroseconds(g_light_config.green_channel.dwell_us);
  }
  GUARD(adc_hal_read_single_ended(g_light_config.green_channel.adc_channel, g_light_config.adc_timeout_us,
                                  &sweep_out->green_code));

  // Step 8: Park the router in the configured drain state so callers see a consistent idle configuration.
  GUARD(led_router_set_state(g_light_config.drain_state));

  // Step 9: Check saturation metadata so callers can detect clipped readings.
  drain_blue_saturated  = light_readings_is_code_saturated(sweep_out->drain_blue_code);
  drain_green_saturated = light_readings_is_code_saturated(sweep_out->drain_green_code);
  blue_saturated        = light_readings_is_code_saturated(sweep_out->blue_code);
  green_saturated       = light_readings_is_code_saturated(sweep_out->green_code);
  // Force saturation for test if applicable
  if (g_force_saturation_for_test) {
    sweep_out->drain_blue_code  = k_light_readings_adc_positive_full_scale_code;
    sweep_out->drain_green_code = k_light_readings_adc_negative_full_scale_code;
    sweep_out->blue_code        = k_light_readings_adc_positive_full_scale_code;
    sweep_out->green_code       = k_light_readings_adc_negative_full_scale_code;
    drain_blue_saturated        = true;
    drain_green_saturated       = true;
    blue_saturated              = true;
    green_saturated             = true;
  }
  g_last_sweep_detected_saturation = drain_blue_saturated || drain_green_saturated || blue_saturated || green_saturated;

  return LIGHT_READINGS_OK;
}

int light_readings_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  // Step 1: Validate the output collection pointer.
  GUARD_NONNULL(results_out);

  // Step 2: Require initialisation before running a sweep sequence.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2.5: Verify the 5V rail and LM7705 generator are asserted before routing LEDs.
  if (!power_control_led_power_is_ready()) {
    return LIGHT_READINGS_ERR_POWER_NOT_READY;
  }

  // Step 3: Ensure callers provide backing storage when requesting one or more sweeps.
  if ((results_out->sweeps == NULL) && (sweep_count > 0u)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 4: Guard against callers requesting more sweeps than the fixed-capacity buffer can hold.
  if (sweep_count > LIGHT_READINGS_MAX_SWEEP_COUNT) {
    return LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED;
  }

  // Step 5: Reset sweep count index.
  results_out->sweep_count = 0u;

  bool saw_saturation = false;

  // Step 6: Execute each sweep and store the results until all iterations complete or a dependency fails.
  for (uint32_t index = 0u; index < sweep_count; ++index) {
    GUARD(light_readings_sweep(&results_out->sweeps[index]));

    results_out->sweep_count = index + 1u;
    saw_saturation |= g_last_sweep_detected_saturation;
  }

  g_last_sweep_detected_saturation = saw_saturation;

  return LIGHT_READINGS_OK;
}

void light_readings_force_saturation_for_test(bool enabled) {
  g_force_saturation_for_test = enabled;
}

bool light_readings_last_sweep_detected_saturation(void) {
  return g_last_sweep_detected_saturation;
}

int light_readings_modify_settings(const LightReadingsRuntimeSettings* settings) {
  // Step 1: Validate the overrides structure before touching hardware state.
  GUARD_NONNULL(settings);

  // Step 2: Ensure the helper has been initialised so cached configuration is valid.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 3: Apply dwell overrides when requested, rejecting out-of-range values.
  if (settings->apply_dwell_override) {
    if (settings->dwell_us > 5000000u) {
      return LIGHT_READINGS_ERR_INVALID_ARG;
    }
    g_light_config.blue_channel.dwell_us  = settings->dwell_us;
    g_light_config.green_channel.dwell_us = settings->dwell_us;
  }

  // Step 4: Update the digipot wiper configuration when an override is provided.
  if (settings->apply_wiper_override) {
    GUARD(digipot_blue_set_wiper(settings->wiper_code));
    GUARD(digipot_green_set_wiper(settings->wiper_code));
    g_light_config.blue_channel.wiper_code  = settings->wiper_code;
    g_light_config.green_channel.wiper_code = settings->wiper_code;
  }

  return LIGHT_READINGS_OK;
}

int light_readings_compute_sweep_stats(const LightReadingsSweepCollection* sweep_collection,
                                       LightReadingsSweepStats*            stats_out) {
  // Step 1: Validate arguments before touching output storage.
  GUARD_NONNULL(sweep_collection);
  GUARD_NONNULL(stats_out);

  if ((sweep_collection->sweeps == NULL) && (sweep_collection->sweep_count > 0u)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 2: Clear the output structure so unpopulated fields carry deterministic values.
  *stats_out             = {};
  stats_out->sweep_count = sweep_collection->sweep_count;

  // Step 3: Initialise running statistics accumulators for each sweep channel field.
  LightReadingsRunningStats drain_blue_stats;
  LightReadingsRunningStats drain_green_stats;
  LightReadingsRunningStats blue_stats;
  LightReadingsRunningStats green_stats;
  light_readings_running_stats_reset(&drain_blue_stats);
  light_readings_running_stats_reset(&drain_green_stats);
  light_readings_running_stats_reset(&blue_stats);
  light_readings_running_stats_reset(&green_stats);

  // Step 4: Fold each sweep sample into the corresponding running statistics accumulator.
  for (uint32_t index = 0u; index < sweep_collection->sweep_count; ++index) {
    const LightReadingsSweepSample& sweep = sweep_collection->sweeps[index];
    light_readings_running_stats_update(&drain_blue_stats, index, sweep.drain_blue_code);
    light_readings_running_stats_update(&drain_green_stats, index, sweep.drain_green_code);
    light_readings_running_stats_update(&blue_stats, index, sweep.blue_code);
    light_readings_running_stats_update(&green_stats, index, sweep.green_code);
  }

  // Step 5: Populate caller-visible summaries for each channel.
  light_readings_populate_summary(drain_blue_stats, &stats_out->drain_blue);
  light_readings_populate_summary(drain_green_stats, &stats_out->drain_green);
  light_readings_populate_summary(blue_stats, &stats_out->blue);
  light_readings_populate_summary(green_stats, &stats_out->green);

  return LIGHT_READINGS_OK;
}

int light_readings_shutdown(void) {
  // Step 1: Reject shutdown requests issued before initialisation.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2: Leave the router in the configured drain state so other modules see a consistent baseline.
  const int return_code = led_router_set_state(g_light_config.drain_state);

  // Step 3: Clear cached state so future tests or runs start from a blank slate.
  g_light_config                   = {};
  g_is_initialized                 = false;
  g_last_sweep_detected_saturation = false;
  g_force_saturation_for_test      = false;
  light_readings_pwm_reset_for_test();

  GUARD(return_code);  // Need to wait to guard to enforce that config and initialize clear first

  return LIGHT_READINGS_OK;
}

void light_readings_reset_for_test(void) {
  // Step 1: Reset cached state so repeated test cases see a cold-started module.
  g_light_config                   = {};
  g_is_initialized                 = false;
  g_last_sweep_detected_saturation = false;
  g_force_saturation_for_test      = false;
  light_readings_pwm_reset_for_test();
}

void light_readings_get_config_for_test(LightReadingsConfig* config_out) {
  // Step 1: Provide tests with a snapshot of the cached configuration when requested.
  if (config_out == nullptr) {
    return;
  }
  *config_out = g_light_config;
}

extern "C" void PWM3_IRQHandler(void) {
  // Step 1: Ignore the interrupt when no new period event is pending.
  if (NRF_PWM3->EVENTS_PWMPERIODEND == 0u) {
    return;
  }

  // Step 2: Clear the event and increment the period counter when a sweep is active.
  NRF_PWM3->EVENTS_PWMPERIODEND = 0u;
  if (g_pwm_state.sweep_active && (g_pwm_state.pwm_instance == NRF_PWM3)) {
    g_pwm_state.period_counter += 1u;
  }
}

int light_readings_pwm_sweep_n(uint32_t sweep_count, LightReadingsSweepCollection* results_out) {
  // Step 1: Validate the output collection pointer.
  GUARD_NONNULL(results_out);

  // Step 2: Ensure the helper has been initialised before manipulating hardware.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2.5: Verify the 5V rail and LM7705 generator are asserted before routing LEDs.
  if (!power_control_led_power_is_ready()) {
    return LIGHT_READINGS_ERR_POWER_NOT_READY;
  }

  // Step 3: Require callers to supply backing storage when sweeps are requested.
  if ((results_out->sweeps == NULL) && (sweep_count > 0u)) {
    return LIGHT_READINGS_ERR_INVALID_ARG;
  }

  // Step 4: Guard against callers requesting more sweeps than the fixed-capacity buffer can hold.
  if (sweep_count > LIGHT_READINGS_MAX_SWEEP_COUNT) {
    return LIGHT_READINGS_ERR_SWEEP_CAPACITY_EXCEEDED;
  }

  // Step 5: Reject PWM sweeps when the configuration has hardware timing disabled.
  if (!g_light_config.pwm_config.pwm_enabled) {
    return LIGHT_READINGS_ERR_PWM_DISABLED;
  }

  // Step 6: Reset the output collection and short-circuit zero-length requests.
  results_out->sweep_count = 0u;
  if (sweep_count == 0u) {
    return LIGHT_READINGS_OK;
  }

  // Step 7: Validate the PWM configuration fields before arming hardware.
  if ((g_light_config.pwm_config.pwm_instance == nullptr) || (g_light_config.pwm_config.minimum_period_us == 0u) ||
      (g_light_config.pwm_config.period_timeout_us == 0u) || (g_light_config.pwm_config.router_in1_pin < 0) ||
      (g_light_config.pwm_config.router_in2_pin < 0)) {
    return LIGHT_READINGS_ERR_PWM_NOT_CONFIGURED;
  }

  // Step 8: Enforce the supported PWM instance for hardware.
  if (g_light_config.pwm_config.pwm_instance != NRF_PWM3) {
    return LIGHT_READINGS_ERR_PWM_UNSUPPORTED_INSTANCE;
  }

  // Step 9: Verify the PWM instance is currently active so sweeps can synchronise with the waveform.
  NRF_PWM_Type* const pwm_instance = g_light_config.pwm_config.pwm_instance;
  const uint32_t      enable_state = pwm_instance->ENABLE;
  if ((enable_state & PWM_ENABLE_ENABLE_Msk) != (PWM_ENABLE_ENABLE_Enabled << PWM_ENABLE_ENABLE_Pos)) {
    return LIGHT_READINGS_ERR_PWM_NOT_RUNNING;
  }

  // Step 10: Prepare PWM observation bookkeeping so the ISR can publish period boundaries.
  int return_code = light_readings_pwm_prepare_observation(pwm_instance);
  if (return_code != LIGHT_READINGS_OK) {
    return return_code;
  }

  // Step 11: Enable period tracking now that the PWM metadata has been latched.
  light_readings_pwm_enable_period_tracking();

  // Step 12: Wait for the next completed period so sampling starts on a clean boundary.
  uint32_t current_period = g_pwm_state.period_counter;
  if (current_period > 0u) {
    // Reset count if interrupt occured in between setup and here
    current_period = 0u;
  }

  return_code = light_readings_wait_for_next_period(current_period, g_pwm_state.period_timeout_us, &current_period);
  if (return_code != LIGHT_READINGS_OK) {
    light_readings_pwm_clear_state();
    return return_code;
  }

  bool saw_saturation = false;

  // Step 13: Capture the requested PWM-synchronised sweeps.
  for (uint32_t index = 0u; index < sweep_count; ++index) {
    LightReadingsSweepSample& sample = results_out->sweeps[index];
    return_code                      = light_readings_capture_pwm_cycle(current_period, &sample, &saw_saturation);
    if (return_code != LIGHT_READINGS_OK) {
      break;
    }

    results_out->sweep_count              = index + 1u;
    g_pwm_diagnostics.sample_period_count = results_out->sweep_count;

    // Wait for next period edge
    if ((index + 1u) < sweep_count) {
      return_code = light_readings_wait_for_next_period(current_period, g_pwm_state.period_timeout_us, &current_period);
      if (return_code != LIGHT_READINGS_OK) {
        break;
      }
    }
  }

  g_last_sweep_detected_saturation = saw_saturation;
  light_readings_pwm_clear_state();

  if (return_code != LIGHT_READINGS_OK) {
    return return_code;
  }

  return LIGHT_READINGS_OK;
}

void light_readings_pwm_reset_for_test(void) {
  light_readings_pwm_clear_state();
  g_pwm_diagnostics            = {};
  g_force_pwm_timeout_for_test = false;
}

void light_readings_pwm_get_diagnostics(LightReadingsPwmDiagnostics* diagnostics_out) {
  if (diagnostics_out == nullptr) {
    return;
  }

  *diagnostics_out = g_pwm_diagnostics;
}

void light_readings_pwm_force_timeout_for_test(bool enabled) {
  g_force_pwm_timeout_for_test = enabled;
}
