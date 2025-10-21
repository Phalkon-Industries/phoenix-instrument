#include "drift_capture.hpp"

#include "../../adc_hal/adc_hal.hpp"
#include "../channel_map/channel_map.hpp"
#include <Arduino.h>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr PhoenixBenchmarkDriftCaptureDefaults k_default_drift_defaults = {
    .start_time_us = 0u,
    .end_time_us   = 100000u,
    .step_delay_us = 0u,
    .osr           = 4096u,
    .wiper_code    = 0x00u,
};

constexpr uint32_t    k_adc_timeout_us = 1000000u;
constexpr std::size_t k_led_count      = 2u;

constexpr uint8_t k_digipot_channels[] = {0u, 1u};

constexpr const char* k_error_invalid_arguments  = "invalid arguments";
constexpr const char* k_error_invalid_command    = "invalid command";
constexpr const char* k_error_invalid_options    = "invalid options";
constexpr const char* k_error_invalid_value      = "invalid value";
constexpr const char* k_error_unknown_argument   = "unknown argument";
constexpr const char* k_error_hardware_init      = "hardware initialisation failed";
constexpr const char* k_error_wiper_failed       = "wiper configuration failed";
constexpr const char* k_error_osr_failed         = "osr configuration failed";
constexpr const char* k_error_adc_read_failed    = "adc read failed";
constexpr const char* k_error_led_router_failed  = "led router state change failed";
constexpr const char* k_error_restore_osr_failed = "osr restore failed";

PhoenixBenchmarkDriftCaptureDefaults g_defaults = k_default_drift_defaults;
PhoenixBenchmarkDriftCaptureSample   g_samples[k_led_count][k_phoenix_benchmark_drift_capture_max_sample_count];
std::size_t                          g_sample_counts[k_led_count] = {0u, 0u};
uint8_t                              g_warning_mask               = 0u;

using HardwareReadyChecker = bool (*)(void);
using WiperSetter          = bool (*)(uint8_t);
using LedSetter            = int (*)(LedRouterState);
using OsrSetter            = int (*)(mcp356x_osr);
using AdcReader            = bool (*)(AdcHalChannel, int32_t*);
using MicrosProvider       = uint32_t (*)(void);
using DelayProvider        = void (*)(uint32_t);

HardwareReadyChecker g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
LedSetter            g_led_setter             = led_router_set_state;
OsrSetter            g_osr_setter             = mcp356x_set_osr;
MicrosProvider       g_micros_provider        = ::micros;
DelayProvider        g_delay_provider         = ::delayMicroseconds;

bool default_wiper_setter(uint8_t code) {
  for (uint8_t channel : k_digipot_channels) {
    if (ad524x_set_wiper(channel, code) != AD524X_OK) {
      return false;
    }
  }
  return true;
}

bool default_adc_reader(AdcHalChannel channel, int32_t* out_code) {
  return (out_code != nullptr) && (adc_hal_read_single_ended(channel, k_adc_timeout_us, out_code) == ADC_HAL_OK);
}

WiperSetter g_wiper_setter = default_wiper_setter;
AdcReader   g_adc_reader   = default_adc_reader;

uint32_t compute_elapsed_time(uint32_t start, uint32_t end) {
  if (end >= start) {
    return end - start;
  }
  return (0xFFFFFFFFu - start) + end + 1u;
}

AdcHalChannel channel_for_led(PhoenixBenchmarkDriftCaptureLed led) {
  return (led == PhoenixBenchmarkDriftCaptureLed::kLed1) ? AdcHalChannel::ADC_HAL_CHANNEL_4 :
                                                           AdcHalChannel::ADC_HAL_CHANNEL_5;
}

LedRouterState router_state_for_led(PhoenixBenchmarkDriftCaptureLed led) {
  return (led == PhoenixBenchmarkDriftCaptureLed::kLed1) ? LedRouterState::LED_ROUTER_STATE_LED1 :
                                                           LedRouterState::LED_ROUTER_STATE_LED2;
}

bool resolve_osr_value(uint32_t value, mcp356x_osr* out_osr) {
  mcp356x_osr resolved = mcp356x_osr::osr_4096;
  switch (value) {
    case 32u:
      resolved = mcp356x_osr::osr_32;
      break;
    case 64u:
      resolved = mcp356x_osr::osr_64;
      break;
    case 128u:
      resolved = mcp356x_osr::osr_128;
      break;
    case 256u:
      resolved = mcp356x_osr::osr_256;
      break;
    case 512u:
      resolved = mcp356x_osr::osr_512;
      break;
    case 1024u:
      resolved = mcp356x_osr::osr_1024;
      break;
    case 2048u:
      resolved = mcp356x_osr::osr_2048;
      break;
    case 4096u:
      resolved = mcp356x_osr::osr_4096;
      break;
    case 8192u:
      resolved = mcp356x_osr::osr_8192;
      break;
    case 16384u:
      resolved = mcp356x_osr::osr_16384;
      break;
    case 20480u:
      resolved = mcp356x_osr::osr_20480;
      break;
    case 24576u:
      resolved = mcp356x_osr::osr_24576;
      break;
    case 40960u:
      resolved = mcp356x_osr::osr_40960;
      break;
    case 49152u:
      resolved = mcp356x_osr::osr_49152;
      break;
    case 81920u:
      resolved = mcp356x_osr::osr_81920;
      break;
    case 98304u:
      resolved = mcp356x_osr::osr_98304;
      break;
    default:
      return false;
  }
  if (out_osr != nullptr) {
    *out_osr = resolved;
  }
  return true;
}

void reset_buffers(void) {
  for (std::size_t led = 0u; led < k_led_count; ++led) {
    g_sample_counts[led] = 0u;
  }
  g_warning_mask = 0u;
}

const char* skip_whitespace(const char* text) {
  if (text == nullptr) {
    return nullptr;
  }
  while ((*text != '\0') && std::isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  return text;
}

bool parse_unsigned_value(const char* cursor, int base, uint32_t* value_out, const char** end_out) {
  if ((cursor == nullptr) || (value_out == nullptr)) {
    return false;
  }

  errno                            = 0;
  char*               parse_end    = nullptr;
  const unsigned long parsed_value = std::strtoul(cursor, &parse_end, base);
  if ((parse_end == cursor) || (errno != 0) || (parsed_value > 0xFFFFFFFFUL)) {
    return false;
  }

  if (end_out != nullptr) {
    *end_out = parse_end;
  }
  *value_out = static_cast<uint32_t>(parsed_value);
  return true;
}

void emit_line(const PhoenixBenchmarkDriftCaptureOutputCallbacks& callbacks, const char* line) {
  if ((callbacks.print_line != nullptr) && (line != nullptr)) {
    callbacks.print_line(line);
  }
}

struct CaptureOutcome {
  bool        success;
  const char* message;
  bool        overflow;
  bool        saturation;
};

CaptureOutcome capture_led(PhoenixBenchmarkDriftCaptureLed led, const PhoenixBenchmarkDriftCaptureOptions& options) {
  const std::size_t led_index = static_cast<std::size_t>(led);

  // Step 1: Configure the LED router so the ADC samples the desired optical path.
  if (g_led_setter == nullptr) {
    return {false, k_error_led_router_failed, false, false};
  }

  const int router_result = g_led_setter(router_state_for_led(led));
  if (router_result != LED_ROUTER_OK) {
    return {false, k_error_led_router_failed, false, false};
  }

  const uint32_t activation_micros = g_micros_provider();

  // Step 2: Honour the requested start delay before taking the first sample.
  while (true) {
    const uint32_t now     = g_micros_provider();
    const uint32_t elapsed = compute_elapsed_time(activation_micros, now);
    if (elapsed >= options.start_time_us) {
      break;
    }
    if (g_delay_provider != nullptr) {
      const uint32_t remaining = options.start_time_us - elapsed;
      const uint32_t delay_step =
          (options.step_delay_us > 0u) ? ((remaining < options.step_delay_us) ? remaining : options.step_delay_us) : 1u;
      g_delay_provider(delay_step);
    }
  }

  bool overflow   = false;
  bool saturation = false;

  // Step 3: Capture samples until the window closes or the buffer fills.
  while (true) {
    if (g_sample_counts[led_index] >= k_phoenix_benchmark_drift_capture_max_sample_count) {
      overflow = true;
      break;
    }

    const std::size_t sample_index      = g_sample_counts[led_index];
    const uint32_t    pre_sample_micros = g_micros_provider();

    int32_t code = 0;
    if ((g_adc_reader == nullptr) || !g_adc_reader(channel_for_led(led), &code)) {
      if (sample_index == 0u) {
        return {false, k_error_adc_read_failed, overflow, saturation};
      }
      break;
    }

    uint32_t sample_micros = pre_sample_micros;
    if (options.step_delay_us == 0u) {
      sample_micros = g_micros_provider();
    }

    uint32_t elapsed = compute_elapsed_time(activation_micros, sample_micros);
    if (options.step_delay_us > 0u) {
      const uint64_t scheduled_elapsed =
          static_cast<uint64_t>(options.start_time_us) + (static_cast<uint64_t>(sample_index) * options.step_delay_us);
      elapsed = static_cast<uint32_t>(scheduled_elapsed);
    }

    PhoenixBenchmarkDriftCaptureSample& sample = g_samples[led_index][sample_index];
    sample.elapsed_microseconds                = elapsed;
    sample.adc_code                            = code;
    ++g_sample_counts[led_index];

    if (phoenix_benchmark_is_adc_code_saturated(code)) {
      saturation = true;
    }

    if (elapsed >= options.end_time_us) {
      break;
    }

    if ((options.step_delay_us > 0u) && (g_delay_provider != nullptr)) {
      g_delay_provider(options.step_delay_us);
    }
  }

  return {true, nullptr, overflow, saturation};
}

PhoenixBenchmarkDriftCaptureExecutionStatus run_drift_capture(
    const PhoenixBenchmarkDriftCaptureOptions&         input_options,
    const PhoenixBenchmarkDriftCaptureOutputCallbacks& callbacks) {
  PhoenixBenchmarkDriftCaptureExecutionStatus status = {
      .success            = false,
      .has_warnings       = false,
      .warning_mask       = 0u,
      .message            = nullptr,
      .applied_start_us   = 0u,
      .applied_end_us     = 0u,
      .applied_step_us    = 0u,
      .applied_osr        = 0u,
      .applied_wiper_code = 0u,
      .led1_samples       = 0u,
      .led2_samples       = 0u,
  };

  auto emit_failure_line = [&](const char* message) {
    if (message == k_error_led_router_failed) {
      emit_line(callbacks, "# drift_capture,error=led_router_failed");
    }
    else if (message == k_error_adc_read_failed) {
      emit_line(callbacks, "# drift_capture,error=adc_read_failed");
    }
    else {
      emit_line(callbacks, "# drift_capture,error=execution_failed");
    }
  };

  PhoenixBenchmarkDriftCaptureOptions options = input_options;
  options.apply_defaults(g_defaults);

  // Step 1: Record the effective configuration so metadata reflects applied defaults.
  status.applied_start_us   = options.start_time_us;
  status.applied_end_us     = options.end_time_us;
  status.applied_step_us    = options.step_delay_us;
  status.applied_osr        = options.osr;
  status.applied_wiper_code = options.wiper_code;

  // Step 2: Validate caller input before touching any hardware state.
  const char* validation_message = nullptr;
  if (!options.validate(&validation_message)) {
    emit_line(callbacks, "# drift_capture,error=invalid_options");
    status.message = (validation_message != nullptr) ? validation_message : k_error_invalid_options;
    return status;
  }

  // Step 3: Reset capture buffers so previous runs cannot leak into this report.
  reset_buffers();

  // Step 4: Ensure the upstream hardware is powered and initialised.
  if ((g_hardware_ready_checker == nullptr) || !g_hardware_ready_checker()) {
    emit_line(callbacks, "# drift_capture,error=hardware_initialisation_failed");
    status.message = k_error_hardware_init;
    return status;
  }

  // Step 5: Apply the requested digipot wiper code before enabling the LEDs.
  if ((g_wiper_setter == nullptr) || !g_wiper_setter(options.wiper_code)) {
    emit_line(callbacks, "# drift_capture,error=wiper_configuration_failed");
    status.message = k_error_wiper_failed;
    return status;
  }

  mcp356x_osr requested_osr = mcp356x_osr::osr_4096;
  bool        attempted_osr = false;
  if (!resolve_osr_value(options.osr, &requested_osr)) {
    emit_line(callbacks, "# drift_capture,error=invalid_osr");
    status.message = k_error_invalid_options;
    return status;
  }

  // Step 6: Program the ADC oversampling ratio so both traces share the requested precision.
  if (g_osr_setter != nullptr) {
    if (g_osr_setter(requested_osr) != MCP356X_OK) {
      emit_line(callbacks, "# drift_capture,error=osr_configuration_failed");
      status.message = k_error_osr_failed;
      return status;
    }
    attempted_osr = true;
  }

  bool overflow_detected   = false;
  bool saturation_detected = false;
  bool run_failed          = false;

  auto set_failure = [&](const char* message) {
    if (status.message == nullptr) {
      status.message = message;
    }
    run_failed = true;
  };

  // Step 7: Capture the LED1 trace before switching the router back to drain.
  const CaptureOutcome led1_outcome = capture_led(PhoenixBenchmarkDriftCaptureLed::kLed1, options);
  overflow_detected |= led1_outcome.overflow;
  saturation_detected |= led1_outcome.saturation;
  if (!led1_outcome.success) {
    emit_failure_line(led1_outcome.message);
    set_failure(led1_outcome.message);
  }

  if (g_led_setter != nullptr) {
    (void) g_led_setter(LedRouterState::LED_ROUTER_STATE_DRAIN);
  }

  CaptureOutcome led2_outcome = {};
  if (!run_failed) {
    // Step 8: Repeat the capture for LED2 only if the first trace succeeded.
    led2_outcome = capture_led(PhoenixBenchmarkDriftCaptureLed::kLed2, options);
    overflow_detected |= led2_outcome.overflow;
    saturation_detected |= led2_outcome.saturation;
    if (!led2_outcome.success) {
      emit_failure_line(led2_outcome.message);
      set_failure(led2_outcome.message);
    }
    if (g_led_setter != nullptr) {
      (void) g_led_setter(LedRouterState::LED_ROUTER_STATE_DRAIN);
    }
  }

  status.led1_samples = g_sample_counts[0u];
  status.led2_samples = g_sample_counts[1u];

  if (overflow_detected) {
    g_warning_mask |= k_phoenix_benchmark_drift_capture_warning_buffer_overflow;
  }
  if (saturation_detected) {
    g_warning_mask |= k_phoenix_benchmark_drift_capture_warning_saturation;
  }

  if (g_led_setter != nullptr) {
    (void) g_led_setter(LedRouterState::LED_ROUTER_STATE_DRAIN);
  }

  // Step 9: Restore the ADC configuration when the test adjusted the OSR.
  if (attempted_osr && (g_osr_setter != nullptr)) {
    mcp356x_osr restore_value = mcp356x_osr::osr_4096;
    if (!resolve_osr_value(g_defaults.osr, &restore_value) || (g_osr_setter(restore_value) != MCP356X_OK)) {
      g_warning_mask |= k_phoenix_benchmark_drift_capture_warning_restore_failed;
      if (run_failed && (status.message == nullptr)) {
        status.message = k_error_restore_osr_failed;
      }
    }
  }

  status.warning_mask = g_warning_mask;
  status.has_warnings = (g_warning_mask != 0u);

  if (!run_failed) {
    // Step 10: Emit metadata and tabulated results so the host can persist the capture.
    status.success = true;

    char buffer[160] = {};
    std::snprintf(buffer, sizeof(buffer),
                  "# drift_capture,metadata,start_us=%lu,end_us=%lu,step_delay_us=%lu,osr=%lu,wiper_code=0x%02X",
                  static_cast<unsigned long>(status.applied_start_us),
                  static_cast<unsigned long>(status.applied_end_us), static_cast<unsigned long>(status.applied_step_us),
                  static_cast<unsigned long>(status.applied_osr), static_cast<unsigned int>(status.applied_wiper_code));
    emit_line(callbacks, buffer);

    std::snprintf(buffer, sizeof(buffer),
                  "# drift_capture,results,led1_samples=%lu,led2_samples=%lu,warning_mask=0x%02X",
                  static_cast<unsigned long>(status.led1_samples), static_cast<unsigned long>(status.led2_samples),
                  static_cast<unsigned int>(status.warning_mask));
    emit_line(callbacks, buffer);

    emit_line(callbacks, "Elapsed_LED1_us\tCode_LED1\tElapsed_LED2_us\tCode_LED2");
    const std::size_t max_rows =
        (status.led1_samples > status.led2_samples) ? status.led1_samples : status.led2_samples;
    for (std::size_t index = 0u; index < max_rows; ++index) {
      const bool  have_led1 = index < status.led1_samples;
      const bool  have_led2 = index < status.led2_samples;
      char        line[160] = {};
      std::size_t used      = 0u;

      if (have_led1) {
        used = static_cast<std::size_t>(std::snprintf(
            line, sizeof(line), "%lu\t%ld", static_cast<unsigned long>(g_samples[0u][index].elapsed_microseconds),
            static_cast<long>(g_samples[0u][index].adc_code)));
      }
      else {
        used = static_cast<std::size_t>(std::snprintf(line, sizeof(line), "nan\tnan"));
      }

      if (used < sizeof(line)) {
        line[used++] = '\t';
        line[used]   = '\0';
      }

      if (have_led2) {
        (void) std::snprintf(line + used, sizeof(line) - used, "%lu\t%ld",
                             static_cast<unsigned long>(g_samples[1u][index].elapsed_microseconds),
                             static_cast<long>(g_samples[1u][index].adc_code));
      }
      else {
        (void) std::snprintf(line + used, sizeof(line) - used, "nan\tnan");
      }

      emit_line(callbacks, line);
    }
  }

  return status;
}
PhoenixBenchmarkDriftCaptureParseResult parse_json_command(const char*                         line,
                                                           PhoenixBenchmarkDriftCaptureOptions options) {
  if (line == nullptr) {
    return {false, options, k_error_invalid_arguments};
  }

  const char* cursor = skip_whitespace(line);
  if ((cursor == nullptr) || (*cursor != '{')) {
    return {false, options, k_error_invalid_command};
  }
  ++cursor;

  auto failure = [&](const char* message) -> PhoenixBenchmarkDriftCaptureParseResult {
    return {false, options, message};
  };

  bool saw_command = false;

  while (true) {
    cursor = skip_whitespace(cursor);
    if (cursor == nullptr) {
      return failure(k_error_invalid_command);
    }

    if (*cursor == '}') {
      ++cursor;
      break;
    }

    if (*cursor != '"') {
      return failure(k_error_invalid_command);
    }
    ++cursor;

    const char* key_start = cursor;
    while ((*cursor != '\0') && (*cursor != '"')) {
      ++cursor;
    }
    if (*cursor != '"') {
      return failure(k_error_invalid_command);
    }
    const std::size_t key_length = static_cast<std::size_t>(cursor - key_start);
    if ((key_length == 0u) || (key_length >= 32u)) {
      return failure(k_error_invalid_command);
    }

    char key_buffer[32] = {};
    std::memcpy(key_buffer, key_start, key_length);
    key_buffer[key_length] = '\0';

    ++cursor;
    cursor = skip_whitespace(cursor);
    if (*cursor != ':') {
      return failure(k_error_invalid_command);
    }
    ++cursor;
    cursor = skip_whitespace(cursor);

    if (std::strcmp(key_buffer, "command") == 0) {
      if (*cursor != '"') {
        return failure(k_error_invalid_command);
      }
      ++cursor;
      const char* value_start = cursor;
      while ((*cursor != '\0') && (*cursor != '"')) {
        ++cursor;
      }
      if (*cursor != '"') {
        return failure(k_error_invalid_command);
      }
      const std::size_t value_length = static_cast<std::size_t>(cursor - value_start);
      if ((value_length == 0u) || (value_length != std::strlen("drift_capture")) ||
          (std::strncmp(value_start, "drift_capture", value_length) != 0)) {
        return failure(k_error_invalid_command);
      }
      if (saw_command) {
        return failure(k_error_invalid_command);
      }
      saw_command = true;
      ++cursor;
    }
    else if ((std::strcmp(key_buffer, "parameters") == 0) || (std::strcmp(key_buffer, "options") == 0)) {
      if (*cursor != '{') {
        return failure(k_error_invalid_command);
      }
      ++cursor;
      while (true) {
        cursor = skip_whitespace(cursor);
        if (cursor == nullptr) {
          return failure(k_error_invalid_command);
        }
        if (*cursor == '}') {
          ++cursor;
          break;
        }
        if (*cursor != '"') {
          return failure(k_error_invalid_command);
        }
        ++cursor;
        const char* param_key_start = cursor;
        while ((*cursor != '\0') && (*cursor != '"')) {
          ++cursor;
        }
        if (*cursor != '"') {
          return failure(k_error_invalid_command);
        }
        const std::size_t param_key_length = static_cast<std::size_t>(cursor - param_key_start);
        if ((param_key_length == 0u) || (param_key_length >= 32u)) {
          return failure(k_error_invalid_command);
        }

        char param_key_buffer[32] = {};
        std::memcpy(param_key_buffer, param_key_start, param_key_length);
        param_key_buffer[param_key_length] = '\0';

        ++cursor;
        cursor = skip_whitespace(cursor);
        if (*cursor != ':') {
          return failure(k_error_invalid_command);
        }
        ++cursor;
        cursor = skip_whitespace(cursor);

        uint32_t parsed_value = 0u;
        if (!parse_unsigned_value(cursor, (std::strcmp(param_key_buffer, "wiper_code") == 0) ? 0 : 10, &parsed_value,
                                  &cursor)) {
          return failure(k_error_invalid_value);
        }

        if (std::strcmp(param_key_buffer, "start_time_us") == 0) {
          options.start_time_us      = parsed_value;
          options.has_start_override = true;
        }
        else if (std::strcmp(param_key_buffer, "end_time_us") == 0) {
          options.end_time_us      = parsed_value;
          options.has_end_override = true;
        }
        else if (std::strcmp(param_key_buffer, "step_delay_us") == 0) {
          options.step_delay_us     = parsed_value;
          options.has_step_override = true;
        }
        else if (std::strcmp(param_key_buffer, "osr") == 0) {
          options.osr              = parsed_value;
          options.has_osr_override = true;
        }
        else if (std::strcmp(param_key_buffer, "wiper_code") == 0) {
          if (parsed_value > 0xFFu) {
            return failure(k_error_invalid_value);
          }
          options.wiper_code         = static_cast<uint8_t>(parsed_value & 0xFFu);
          options.has_wiper_override = true;
        }
        else {
          return failure(k_error_unknown_argument);
        }

        cursor = skip_whitespace(cursor);
        if (*cursor == ',') {
          ++cursor;
          continue;
        }
        if (*cursor == '}') {
          ++cursor;
          break;
        }
        return failure(k_error_invalid_command);
      }
    }
    else {
      return failure(k_error_unknown_argument);
    }

    cursor = skip_whitespace(cursor);
    if (*cursor == ',') {
      ++cursor;
      continue;
    }
    if (*cursor == '}') {
      ++cursor;
      break;
    }
  }

  cursor = skip_whitespace(cursor);
  if ((cursor != nullptr) && (*cursor != '\0')) {
    return failure(k_error_invalid_command);
  }

  if (!saw_command) {
    return failure(k_error_invalid_command);
  }

  const char* validation_message = nullptr;
  if (!options.validate(&validation_message)) {
    return failure((validation_message != nullptr) ? validation_message : k_error_invalid_options);
  }

  return {true, options, nullptr};
}

}  // namespace

PhoenixBenchmarkDriftCaptureExecutionStatus phoenix_benchmark_drift_capture_run(
    const PhoenixBenchmarkDriftCaptureOptions&         input_options,
    const PhoenixBenchmarkDriftCaptureOutputCallbacks& callbacks) {
  return run_drift_capture(input_options, callbacks);
}

void PhoenixBenchmarkDriftCaptureOptions::apply_defaults(const PhoenixBenchmarkDriftCaptureDefaults& defaults) {
  if (!has_start_override) {
    start_time_us = defaults.start_time_us;
  }
  if (!has_end_override) {
    end_time_us = defaults.end_time_us;
  }
  if (!has_step_override) {
    step_delay_us = defaults.step_delay_us;
  }
  if (!has_osr_override) {
    osr = defaults.osr;
  }
  if (!has_wiper_override) {
    wiper_code = defaults.wiper_code;
  }
}

bool PhoenixBenchmarkDriftCaptureOptions::validate(const char** error_message) const {
  const char* message = nullptr;
  if (end_time_us < start_time_us) {
    message = "end_time_us must be >= start_time_us";
  }
  else if ((step_delay_us > 0u)) {
    const uint64_t span         = static_cast<uint64_t>(end_time_us) - static_cast<uint64_t>(start_time_us);
    const uint64_t step         = static_cast<uint64_t>(step_delay_us);
    const uint64_t sample_count = (span / step) + 1u;
    if (sample_count > k_phoenix_benchmark_drift_capture_max_sample_count) {
      message = "capture window exceeds buffer";
    }
  }

  if ((message == nullptr) && !resolve_osr_value(osr, nullptr)) {
    message = "osr unsupported";
  }

  if ((message != nullptr) && (error_message != nullptr)) {
    *error_message = message;
  }
  return message == nullptr;
}

void phoenix_benchmark_drift_capture_initialise(const PhoenixBenchmarkDriftCaptureDefaults& defaults) {
  g_defaults = defaults;
}

void phoenix_benchmark_drift_capture_reset_state(void) {
  g_defaults               = k_default_drift_defaults;
  g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
  g_wiper_setter           = default_wiper_setter;
  g_led_setter             = led_router_set_state;
  g_osr_setter             = mcp356x_set_osr;
  g_adc_reader             = default_adc_reader;
  g_micros_provider        = ::micros;
  g_delay_provider         = ::delayMicroseconds;
  reset_buffers();
}

PhoenixBenchmarkDriftCaptureParseResult phoenix_benchmark_drift_capture_parse_command(const char* line) {
  PhoenixBenchmarkDriftCaptureOptions options = {};
  options.apply_defaults(g_defaults);

  if (line == nullptr) {
    return {false, options, k_error_invalid_arguments};
  }

  const char* trimmed = skip_whitespace(line);
  if ((trimmed == nullptr) || (*trimmed == '\0')) {
    return {false, options, k_error_invalid_command};
  }

  if (*trimmed == '{') {
    return parse_json_command(trimmed, options);
  }

  char        buffer[192] = {};
  std::size_t length      = 0u;
  while ((trimmed[length] != '\0') && (length < (sizeof(buffer) - 1u))) {
    buffer[length] = trimmed[length];
    ++length;
  }
  buffer[length] = '\0';

  char* token = std::strtok(buffer, " \t\r\n");
  if ((token == nullptr) || (std::strcmp(token, "drift_capture") != 0)) {
    return {false, options, k_error_invalid_command};
  }

  token = std::strtok(nullptr, " \t\r\n");
  if (token != nullptr) {
    return {false, options, k_error_invalid_command};
  }

  return {true, options, nullptr};
}

const PhoenixBenchmarkDriftCaptureSample* phoenix_benchmark_drift_capture_led_samples(
    PhoenixBenchmarkDriftCaptureLed led, std::size_t* count_out) {
  const std::size_t index = static_cast<std::size_t>(led);
  if (count_out != nullptr) {
    *count_out = g_sample_counts[index];
  }
  return g_samples[index];
}

void phoenix_benchmark_drift_capture_set_hardware_ready_checker_for_test(bool (*checker)(void)) {
  g_hardware_ready_checker = (checker != nullptr) ? checker : phoenix_benchmark_channel_map_ensure_hardware_ready;
}

void phoenix_benchmark_drift_capture_set_wiper_setter_for_test(bool (*setter)(uint8_t wiper_code)) {
  g_wiper_setter = (setter != nullptr) ? setter : default_wiper_setter;
}

void phoenix_benchmark_drift_capture_set_led_setter_for_test(int (*setter)(LedRouterState state)) {
  g_led_setter = (setter != nullptr) ? setter : led_router_set_state;
}

void phoenix_benchmark_drift_capture_set_osr_setter_for_test(int (*setter)(mcp356x_osr value)) {
  g_osr_setter = (setter != nullptr) ? setter : mcp356x_set_osr;
}

void phoenix_benchmark_drift_capture_set_adc_reader_for_test(bool (*reader)(AdcHalChannel channel, int32_t* code_out)) {
  g_adc_reader = (reader != nullptr) ? reader : default_adc_reader;
}

void phoenix_benchmark_drift_capture_set_micros_provider_for_test(uint32_t (*provider)(void)) {
  g_micros_provider = (provider != nullptr) ? provider : ::micros;
}

void phoenix_benchmark_drift_capture_set_delay_provider_for_test(void (*provider)(uint32_t)) {
  g_delay_provider = (provider != nullptr) ? provider : ::delayMicroseconds;
}

void phoenix_benchmark_drift_capture_clear_test_hooks(void) {
  g_hardware_ready_checker = phoenix_benchmark_channel_map_ensure_hardware_ready;
  g_wiper_setter           = default_wiper_setter;
  g_led_setter             = led_router_set_state;
  g_osr_setter             = mcp356x_set_osr;
  g_adc_reader             = default_adc_reader;
  g_micros_provider        = ::micros;
  g_delay_provider         = ::delayMicroseconds;
}
