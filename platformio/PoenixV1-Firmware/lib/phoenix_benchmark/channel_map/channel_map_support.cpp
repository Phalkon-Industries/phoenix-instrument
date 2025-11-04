#include "channel_map_support.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

PhoenixBenchmarkChannel phoenix_benchmark_channel_map_determine_dominant_channel(
    const PhoenixBenchmarkStateAccumulator& drain_accumulator,
    const PhoenixBenchmarkStateAccumulator& state_accumulator, double minimum_difference) {
  // Step 1: Ensure each accumulator collected samples so statistical comparisons are valid.
  if (!drain_accumulator.channel_a_codes.has_samples() || !drain_accumulator.channel_b_codes.has_samples() ||
      !state_accumulator.channel_a_codes.has_samples() || !state_accumulator.channel_b_codes.has_samples()) {
    return PhoenixBenchmarkChannel::kUnknown;
  }

  // Step 2: Capture mean codes for each channel in the drain and state accumulators.
  const double drain_mean_a = drain_accumulator.channel_a_codes.mean;
  const double drain_mean_b = drain_accumulator.channel_b_codes.mean;
  const double state_mean_a = state_accumulator.channel_a_codes.mean;
  const double state_mean_b = state_accumulator.channel_b_codes.mean;

  // Step 3: Compute absolute differences to quantify how far the state drifted from the drain baseline.
  const double delta_a = std::fabs(state_mean_a - drain_mean_a);
  const double delta_b = std::fabs(state_mean_b - drain_mean_b);

  // Step 4: Reject both channels when neither exceeded the minimum difference threshold.
  if ((delta_a < minimum_difference) && (delta_b < minimum_difference)) {
    return PhoenixBenchmarkChannel::kUnknown;
  }

  // Step 5: Favor channel A when it exceeds channel B by at least the minimum delta.
  if ((delta_a > delta_b) && (delta_a >= minimum_difference)) {
    return PhoenixBenchmarkChannel::kChannelA;
  }

  // Step 6: Favor channel B when it exceeds channel A by at least the minimum delta.
  if ((delta_b > delta_a) && (delta_b >= minimum_difference)) {
    return PhoenixBenchmarkChannel::kChannelB;
  }

  // Step 7: Declare the result unknown when deltas are tied within the threshold window.
  return PhoenixBenchmarkChannel::kUnknown;
}

bool phoenix_benchmark_channel_map_format_alignment_label(PhoenixBenchmarkChannel expected,
                                                          PhoenixBenchmarkChannel observed, char* buffer,
                                                          std::size_t buffer_length) {
  // Step 1: Validate the caller supplied a writable buffer before writing any tokens.
  if ((buffer == nullptr) || (buffer_length == 0u)) {
    return false;
  }

  // Step 2: Map each channel enum to its printable token for string formatting.
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

  // Step 3: Emit a placeholder when the expected channel is unknown.
  if (expected == PhoenixBenchmarkChannel::kUnknown) {
    if (buffer_length < 3u) {
      return false;
    }
    buffer[0u] = '-';
    buffer[1u] = '-';
    buffer[2u] = '\0';
    return true;
  }

  // Step 4: Fetch the expected and observed channel tokens for comparison output.
  const char* expected_token = channel_token(expected);
  const char* observed_token = channel_token(observed);

  // Step 5: Format the alignment status string depending on the observed outcome.
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

  // Step 6: Validate the snprintf result so the caller only receives complete strings.
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
  // Step 1: Confirm the caller provided input text and an output structure.
  if ((json_line == nullptr) || (out_request == nullptr)) {
    return false;
  }

  // Step 2: Locate the command token to ensure the payload targets channel_map.
  const char* command_token = std::strstr(json_line, "\"command\"");
  if (command_token == nullptr) {
    return false;
  }

  // Step 3: Move to the command value and verify it matches the channel map command.
  const char* command_value = std::strchr(command_token, ':');
  if (command_value == nullptr) {
    return false;
  }
  ++command_value;

  const char* channel_map_token = std::strstr(command_value, "\"channel_map\"");
  if (channel_map_token == nullptr) {
    return false;
  }

  // Step 4: Extract the sweep count which is required for every request.
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

  // Step 5: Initialize the request with the mandatory sweep count.
  PhoenixBenchmarkChannelMapRequest request = {};
  request.sweep_count                       = static_cast<uint32_t>(sweeps_long);

  // Step 6: Reject dwell overrides now that the channel map command relies on device defaults.
  if (std::strstr(json_line, "\"dwell_us\"") != nullptr) {
    return false;
  }

  // Step 7: Reject wiper overrides to enforce the simplified command surface.
  if (std::strstr(json_line, "\"wiper_code\"") != nullptr) {
    return false;
  }

  // Step 8: Publish the parsed request back to the caller.
  *out_request = request;
  return true;
}
