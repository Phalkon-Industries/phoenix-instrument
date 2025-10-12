#include "channel_map_support.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

PhoenixBenchmarkChannel phoenix_benchmark_channel_map_determine_dominant_channel(
    const PhoenixBenchmarkStateAccumulator& drain_accumulator,
    const PhoenixBenchmarkStateAccumulator& state_accumulator, double minimum_difference) {
  if (!drain_accumulator.channel_a_codes.has_samples() || !drain_accumulator.channel_b_codes.has_samples() ||
      !state_accumulator.channel_a_codes.has_samples() || !state_accumulator.channel_b_codes.has_samples()) {
    return PhoenixBenchmarkChannel::kUnknown;
  }

  const double drain_mean_a = drain_accumulator.channel_a_codes.mean;
  const double drain_mean_b = drain_accumulator.channel_b_codes.mean;
  const double state_mean_a = state_accumulator.channel_a_codes.mean;
  const double state_mean_b = state_accumulator.channel_b_codes.mean;

  const double delta_a = std::fabs(state_mean_a - drain_mean_a);
  const double delta_b = std::fabs(state_mean_b - drain_mean_b);

  if ((delta_a < minimum_difference) && (delta_b < minimum_difference)) {
    return PhoenixBenchmarkChannel::kUnknown;
  }

  if ((delta_a > delta_b) && (delta_a >= minimum_difference)) {
    return PhoenixBenchmarkChannel::kChannelA;
  }

  if ((delta_b > delta_a) && (delta_b >= minimum_difference)) {
    return PhoenixBenchmarkChannel::kChannelB;
  }

  return PhoenixBenchmarkChannel::kUnknown;
}

bool phoenix_benchmark_channel_map_format_alignment_label(PhoenixBenchmarkChannel expected,
                                                          PhoenixBenchmarkChannel observed, char* buffer,
                                                          std::size_t buffer_length) {
  if ((buffer == nullptr) || (buffer_length == 0u)) {
    return false;
  }

  auto channel_token = [](PhoenixBenchmarkChannel channel) -> const char* {
    switch (channel) {
      case PhoenixBenchmarkChannel::kChannelA:
        return "A";
      case PhoenixBenchmarkChannel::kChannelB:
        return "B";
      case PhoenixBenchmarkChannel::kUnknown:
      default:
        return "??";
    }
  };

  if (expected == PhoenixBenchmarkChannel::kUnknown) {
    if (buffer_length < 3u) {
      return false;
    }
    buffer[0u] = '-';
    buffer[1u] = '-';
    buffer[2u] = '\0';
    return true;
  }

  const char* expected_token = channel_token(expected);
  const char* observed_token = channel_token(observed);

  int written = 0;
  if (observed == expected) {
    written = std::snprintf(buffer, buffer_length, "%s=OK", expected_token);
  }
  else if (observed == PhoenixBenchmarkChannel::kUnknown) {
    written = std::snprintf(buffer, buffer_length,
                            "?"
                            "?!=%s",
                            expected_token);
  }
  else {
    written = std::snprintf(buffer, buffer_length, "%s!=%s", observed_token, expected_token);
  }

  if (written <= 0) {
    return false;
  }
  if (static_cast<std::size_t>(written) >= buffer_length) {
    return false;
  }
  return true;
}

bool phoenix_benchmark_channel_map_parse_command(const char*                        json_line,
                                                 PhoenixBenchmarkChannelMapRequest* out_request) {
  if ((json_line == nullptr) || (out_request == nullptr)) {
    return false;
  }

  const char* command_token = std::strstr(json_line, "\"command\"");
  if (command_token == nullptr) {
    return false;
  }

  const char* command_value = std::strchr(command_token, ':');
  if (command_value == nullptr) {
    return false;
  }
  ++command_value;

  const char* channel_map_token = std::strstr(command_value, "\"channel_map\"");
  if (channel_map_token == nullptr) {
    return false;
  }

  const char* sweeps_token = std::strstr(json_line, "\"sweeps\"");
  if (sweeps_token == nullptr) {
    return false;
  }

  const char* sweeps_value = std::strchr(sweeps_token, ':');
  if (sweeps_value == nullptr) {
    return false;
  }
  ++sweeps_value;

  char*      sweeps_end  = nullptr;
  const long sweeps_long = std::strtol(sweeps_value, &sweeps_end, 10);
  if ((sweeps_end == sweeps_value) || (sweeps_long <= 0L)) {
    return false;
  }

  PhoenixBenchmarkChannelMapRequest request = {};
  request.sweep_count                       = static_cast<uint32_t>(sweeps_long);

  const char* dwell_token = std::strstr(json_line, "\"dwell_us\"");
  if (dwell_token != nullptr) {
    const char* dwell_value = std::strchr(dwell_token, ':');
    if (dwell_value == nullptr) {
      return false;
    }
    ++dwell_value;

    char*      dwell_end  = nullptr;
    const long dwell_long = std::strtol(dwell_value, &dwell_end, 10);
    if ((dwell_end == dwell_value) || (dwell_long < 0L)) {
      return false;
    }

    request.dwell_us           = static_cast<uint32_t>(dwell_long);
    request.has_dwell_override = true;
  }

  const char* wiper_token = std::strstr(json_line, "\"wiper_code\"");
  if (wiper_token != nullptr) {
    const char* wiper_value = std::strchr(wiper_token, ':');
    if (wiper_value == nullptr) {
      return false;
    }
    ++wiper_value;

    char*      wiper_end  = nullptr;
    const long wiper_long = std::strtol(wiper_value, &wiper_end, 0);
    if ((wiper_end == wiper_value) || (wiper_long < 0L) || (wiper_long > 0xFFL)) {
      return false;
    }

    request.wiper_code         = static_cast<uint8_t>(wiper_long & 0xFFL);
    request.has_wiper_override = true;
  }

  *out_request = request;
  return true;
}
