#include "led_router.hpp"

#include <Arduino.h>
#include <string.h>

extern const uint32_t g_ADigitalPinMap[];

namespace {

LedRouterConfig g_router_config  = {0, 0, {false, nullptr}};
LedRouterState  g_current_state  = LedRouterState::LED_ROUTER_STATE_OFF;
bool            g_is_initialized = false;

constexpr uint32_t k_pwm_source_clock_hz         = 16000000u;
constexpr uint32_t k_pwm_microseconds_per_second = 1000000u;
constexpr uint16_t k_pwm_polarity_invert_mask    = 0x8000u;
constexpr uint32_t k_pwm_channel_not_connected   = NRF_PWM_PIN_NOT_CONNECTED;
constexpr uint32_t k_pwm_countertop_limit        = PWM_COUNTERTOP_COUNTERTOP_Msk;

struct PwmClockOption {
  nrf_pwm_clk_t clock_setting;
  uint32_t      frequency_hz;
};

constexpr PwmClockOption k_pwm_clock_options[] = {
    {NRF_PWM_CLK_16MHz, k_pwm_source_clock_hz},        {NRF_PWM_CLK_8MHz, k_pwm_source_clock_hz / 2u},
    {NRF_PWM_CLK_4MHz, k_pwm_source_clock_hz / 4u},    {NRF_PWM_CLK_2MHz, k_pwm_source_clock_hz / 8u},
    {NRF_PWM_CLK_1MHz, k_pwm_source_clock_hz / 16u},   {NRF_PWM_CLK_500kHz, k_pwm_source_clock_hz / 32u},
    {NRF_PWM_CLK_250kHz, k_pwm_source_clock_hz / 64u}, {NRF_PWM_CLK_125kHz, k_pwm_source_clock_hz / 128u},
};

struct LedRouterPwmRuntimeState {
  bool          pwm_configured;
  NRF_PWM_Type* pwm_instance;
  uint16_t      countertop;
  uint16_t      channel0_level;
  uint16_t      channel1_level;
  bool          channel1_is_inverted;
  uint32_t      base_frequency_hz;
};

LedRouterPwmRuntimeState g_pwm_state = {false, nullptr, 0u, 0u, 0u, false, 0u};

nrf_pwm_values_wave_form_t g_pwm_waveforms[] = {
    {0u, 0u, 0u, 0u},
};

bool is_valid_state(LedRouterState state) {
  // Step 1: Map the provided state to the set of supported router modes.
  switch (state) {
    case LedRouterState::LED_ROUTER_STATE_OFF:
    case LedRouterState::LED_ROUTER_STATE_LED1:
    case LedRouterState::LED_ROUTER_STATE_LED2:
    case LedRouterState::LED_ROUTER_STATE_DRAIN:
      return true;
    default:
      return false;
  }
}

void apply_state_to_pins(LedRouterState state) {
  // Avoid redundant writes that would repeatedly toggle the TS5A3359 control pins.
  if (g_current_state == state) {
    return;
  }

  int pin_in1_level = LOW;
  int pin_in2_level = LOW;

  // TS5A3359 truth table: IN1/IN2 -> OFF (0/0), LED1 (1/0), LED2 (0/1), DRAIN (1/1).
  // Step 1: Translate the router state into the two control-line levels.
  switch (state) {
    case LedRouterState::LED_ROUTER_STATE_OFF:
      pin_in1_level = LOW;
      pin_in2_level = LOW;
      break;
    case LedRouterState::LED_ROUTER_STATE_LED1:
      pin_in1_level = HIGH;
      pin_in2_level = LOW;
      break;
    case LedRouterState::LED_ROUTER_STATE_LED2:
      pin_in1_level = LOW;
      pin_in2_level = HIGH;
      break;
    case LedRouterState::LED_ROUTER_STATE_DRAIN:
      pin_in1_level = HIGH;
      pin_in2_level = HIGH;
      break;
    default:
      // This branch is unreachable because callers validate the state.
      break;
  }

  // Step 2: Drive the switch pins to realise the requested routing mode.
  digitalWrite(g_router_config.switch_in1_pin, pin_in1_level);
  digitalWrite(g_router_config.switch_in2_pin, pin_in2_level);
  g_current_state = state;
}

}  // namespace

int led_router_initialize(const LedRouterConfig* config) {
  // Step 1: Validate the configuration pointer and ensure unique pins.
  GUARD_NONNULL(config);

  if (config->switch_in1_pin == config->switch_in2_pin) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  // Step 2: Prepare the hardware pins for digital output.
  pinMode(config->switch_in1_pin, OUTPUT);
  pinMode(config->switch_in2_pin, OUTPUT);

  // Step 3: Cache the configuration and park the router in the drain state.
  g_router_config  = *config;
  g_is_initialized = true;
  apply_state_to_pins(LedRouterState::LED_ROUTER_STATE_DRAIN);

  return LED_ROUTER_OK;
}

int led_router_set_state(LedRouterState state) {
  // Step 1: Enforce initialisation before manipulating the router.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 2: Reject unknown router states so the TS5A3359 only sees valid combinations.
  if (!is_valid_state(state)) {
    return LED_ROUTER_ERR_INVALID_STATE;
  }

  // Step 3: Apply the new state to the hardware.
  apply_state_to_pins(state);
  return LED_ROUTER_OK;
}

int led_router_get_state(LedRouterState* state_out) {
  // Step 1: Validate output storage.
  GUARD_NONNULL(state_out);

  // Step 2: Ensure the router has been initialised.
  GUARD_INITIALIZED(g_is_initialized);

  // Step 3: Report the cached state without touching hardware.
  *state_out = g_current_state;
  return LED_ROUTER_OK;
}

int led_router_shutdown(void) {
  // Step 1: Confirm initialisation before toggling hardware pins.
  GUARD_INITIALIZED(g_is_initialized);

  if (g_router_config.pwm_config.pwm_enabled) {
    // Step 2: Halt PWM playback so the GPIO peripheral regains control of the pins.
    (void) led_router_pwm_stop();
  }

  // Step 3: Drive the router lines low so the switch enters the OFF state.
  digitalWrite(g_router_config.switch_in1_pin, LOW);
  digitalWrite(g_router_config.switch_in2_pin, LOW);

  // Step 4: Reset cached state so subsequent initialisations start cleanly.
  g_is_initialized = false;
  g_current_state  = LedRouterState::LED_ROUTER_STATE_OFF;
  return LED_ROUTER_OK;
}

void led_router_reset_for_test(void) {
  // Step 1: Clear runtime state so tests can simulate cold-start behaviour.
  g_is_initialized                        = false;
  g_router_config.switch_in1_pin          = 0;
  g_router_config.switch_in2_pin          = 0;
  g_router_config.pwm_config.pwm_enabled  = false;
  g_router_config.pwm_config.pwm_instance = nullptr;
  g_current_state                         = LedRouterState::LED_ROUTER_STATE_OFF;
  g_pwm_state.pwm_configured              = false;
  g_pwm_state.pwm_instance                = nullptr;
  g_pwm_state.countertop                  = 0u;
  g_pwm_state.channel0_level              = 0u;
  g_pwm_state.channel1_level              = 0u;
  g_pwm_state.channel1_is_inverted        = false;
  g_pwm_state.base_frequency_hz           = 0u;
  memset(&g_pwm_waveforms[0], 0, sizeof(g_pwm_waveforms));
}

int led_router_pwm_configure(uint32_t minimum_period_us) {
  // Step 1: Confirm the router and PWM back-end are ready for configuration.
  GUARD_INITIALIZED(g_is_initialized);

  if (!g_router_config.pwm_config.pwm_enabled) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  NRF_PWM_Type* const pwm_instance = g_router_config.pwm_config.pwm_instance;
  if (pwm_instance == nullptr) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  if (minimum_period_us == 0u) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  // Step 2: Select the smallest prescaler that satisfies the minimum period constraint.
  uint16_t      countertop        = 0u;
  uint32_t      base_frequency_hz = 0u;
  nrf_pwm_clk_t clock_selection   = NRF_PWM_CLK_1MHz;

  for (const PwmClockOption& option : k_pwm_clock_options) {
    uint64_t required_ticks =
        ((static_cast<uint64_t>(option.frequency_hz) * minimum_period_us) + (k_pwm_microseconds_per_second - 1u)) /
        k_pwm_microseconds_per_second;
    if (required_ticks == 0u) {
      required_ticks = 1u;
    }

    if (required_ticks <= k_pwm_countertop_limit) {
      countertop        = static_cast<uint16_t>(required_ticks);
      base_frequency_hz = option.frequency_hz;
      clock_selection   = option.clock_setting;
      break;
    }
  }

  if (base_frequency_hz == 0u) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  // Step 3: Stop the PWM instance before programming new waveform registers.
  pwm_instance->TASKS_STOP     = 1u;
  pwm_instance->EVENTS_STOPPED = 0u;
  pwm_instance->ENABLE         = (PWM_ENABLE_ENABLE_Disabled << PWM_ENABLE_ENABLE_Pos);

  // Step 4: Bind the TS5A3359 control pins to the PWM channels.
  const uint32_t pwm_pins[NRF_PWM_CHANNEL_COUNT] = {
      g_ADigitalPinMap[g_router_config.switch_in1_pin],
      g_ADigitalPinMap[g_router_config.switch_in2_pin],
      k_pwm_channel_not_connected,
      k_pwm_channel_not_connected,
  };

  for (size_t index = 0u; index < NRF_PWM_CHANNEL_COUNT; ++index) {
    pwm_instance->PSEL.OUT[index] = pwm_pins[index];
  }

  // Step 5: Program the PWM core for edge-aligned playback at the requested frequency.
  pwm_instance->PRESCALER  = clock_selection;
  pwm_instance->MODE       = PWM_MODE_UPDOWN_Up;
  pwm_instance->COUNTERTOP = countertop;
  pwm_instance->DECODER =
      (PWM_DECODER_LOAD_WaveForm << PWM_DECODER_LOAD_Pos) | (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
  pwm_instance->LOOP   = 0u;
  pwm_instance->SHORTS = PWM_SHORTS_LOOPSDONE_SEQSTART0_Msk;

  // Step 6: Build the inverted-duty waveform so IN1 is 50% and IN2 is 75% high.
  const uint16_t channel0_level = static_cast<uint16_t>(countertop / 2u);
  const uint16_t channel1_level = static_cast<uint16_t>(countertop / 4u);

  g_pwm_waveforms[0].channel_0   = channel0_level;
  g_pwm_waveforms[0].channel_1   = static_cast<uint16_t>(channel1_level | k_pwm_polarity_invert_mask);
  g_pwm_waveforms[0].channel_2   = 0u;
  g_pwm_waveforms[0].counter_top = countertop;

  pwm_instance->SEQ[0].PTR      = reinterpret_cast<uint32_t>(&g_pwm_waveforms[0]);
  pwm_instance->SEQ[0].CNT      = NRF_PWM_VALUES_LENGTH(g_pwm_waveforms);
  pwm_instance->SEQ[0].REFRESH  = 0u;
  pwm_instance->SEQ[0].ENDDELAY = 0u;

  pwm_instance->SEQ[1].PTR = 0u;
  pwm_instance->SEQ[1].CNT = 0u;

  // Step 7: Enable playback so hardware toggles the switch lines autonomously.
  pwm_instance->ENABLE            = (PWM_ENABLE_ENABLE_Enabled << PWM_ENABLE_ENABLE_Pos);
  pwm_instance->TASKS_SEQSTART[0] = 1u;

  g_pwm_state.pwm_configured       = true;
  g_pwm_state.pwm_instance         = pwm_instance;
  g_pwm_state.countertop           = countertop;
  g_pwm_state.channel0_level       = channel0_level;
  g_pwm_state.channel1_level       = channel1_level;
  g_pwm_state.channel1_is_inverted = true;
  g_pwm_state.base_frequency_hz    = base_frequency_hz;

  return LED_ROUTER_OK;
}

int led_router_pwm_stop(void) {
  // Step 1: Ensure the driver and PWM back-end are valid before stopping playback.
  GUARD_INITIALIZED(g_is_initialized);

  if (!g_router_config.pwm_config.pwm_enabled) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  NRF_PWM_Type* const pwm_instance = g_router_config.pwm_config.pwm_instance;
  if (pwm_instance == nullptr) {
    return LED_ROUTER_ERR_INVALID_ARG;
  }

  if (!g_pwm_state.pwm_configured) {
    return LED_ROUTER_OK;
  }

  // Step 2: Request the PWM peripheral to halt and release its shortcuts.
  pwm_instance->SHORTS     = 0u;
  pwm_instance->TASKS_STOP = 1u;
  pwm_instance->ENABLE     = (PWM_ENABLE_ENABLE_Disabled << PWM_ENABLE_ENABLE_Pos);

  g_pwm_state.pwm_configured       = false;
  g_pwm_state.pwm_instance         = pwm_instance;
  g_pwm_state.countertop           = 0u;
  g_pwm_state.channel0_level       = 0u;
  g_pwm_state.channel1_level       = 0u;
  g_pwm_state.channel1_is_inverted = false;
  g_pwm_state.base_frequency_hz    = 0u;
  memset(&g_pwm_waveforms[0], 0, sizeof(g_pwm_waveforms));

  return LED_ROUTER_OK;
}

void led_router_get_pwm_test_snapshot(LedRouterPwmTestSnapshot* snapshot_out) {
  if (snapshot_out == nullptr) {
    return;
  }

  memset(snapshot_out, 0, sizeof(*snapshot_out));

  snapshot_out->pwm_configured       = g_pwm_state.pwm_configured;
  snapshot_out->countertop           = g_pwm_state.countertop;
  snapshot_out->channel0_level       = g_pwm_state.channel0_level;
  snapshot_out->channel1_level       = g_pwm_state.channel1_level;
  snapshot_out->channel1_is_inverted = g_pwm_state.channel1_is_inverted;
  snapshot_out->base_frequency_hz    = g_pwm_state.base_frequency_hz;
}
