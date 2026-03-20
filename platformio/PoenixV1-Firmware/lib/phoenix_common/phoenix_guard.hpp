#ifndef PHOENIX_COMMON_PHOENIX_GUARD_HPP
#define PHOENIX_COMMON_PHOENIX_GUARD_HPP

// Shared Phoenix Instrument return codes unify guard behaviour across modules.
#define PHX_OK 0
#define PHX_ERR_INVALID_ARG -1
#define PHX_ERR_NOT_INITIALIZED -2
#define PHX_ERR_TIMEOUT -3
#define PHX_ERR_UNSUPPORTED -4
#define PHX_ERR_NOT_IMPLEMENTED -5
#define PHX_ERR_HARDWARE_FAILURE -6
#define PHX_ERR_COMMUNICATION -7

// Driver-specific error codes should remain at or below this offset to preserve uniqueness.
#define PHX_ERR_MODULE_BASE -100

// Returns when @p expression evaluates to a non-zero error code so callers see the
// first failure encountered.
#define GUARD(expression)                   \
  do {                                      \
    const int guard_result_ = (expression); \
    if (guard_result_ != PHX_OK) {          \
      return guard_result_;                 \
    }                                       \
  } while (0)

// Returns when @p expression evaluates to a non-zero error code, emitting a labelled
// error via @p emit_fn before returning.
//
// @p emit_fn must be callable as emit_fn(label, error_code).
#define GUARD_EMIT(emit_fn, label, expression) \
  do {                                         \
    const int guard_result_ = (expression);    \
    if (guard_result_ != PHX_OK) {             \
      (emit_fn)((label), guard_result_);       \
      return guard_result_;                    \
    }                                          \
  } while (0)

// Ensures pointer arguments are not NULL before the function dereferences them.
#define GUARD_NONNULL(pointer)    \
  do {                            \
    if ((pointer) == NULL) {      \
      return PHX_ERR_INVALID_ARG; \
    }                             \
  } while (0)

// Verifies modules complete their initialise routine before dependent calls.
#define GUARD_INITIALIZED(flag)       \
  do {                                \
    if (!(flag)) {                    \
      return PHX_ERR_NOT_INITIALIZED; \
    }                                 \
  } while (0)

#endif  // PHOENIX_COMMON_PHOENIX_GUARD_HPP
