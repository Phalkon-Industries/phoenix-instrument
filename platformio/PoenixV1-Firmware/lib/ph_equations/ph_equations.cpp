#include "ph_equations.hpp"

#include <math.h>
#include <stddef.h>

// Step 1: Provide stub implementations that validate inputs and return domain errors.
// These will be replaced in Phase 1 implementation steps.

int ph_equations_calc_absorbance(double reference_intensity, double sample_intensity, double* out_absorbance) {
  // Step 1: Validate arguments to protect against invalid math operations.
  GUARD_NONNULL(out_absorbance);
  if (reference_intensity <= 0.0 || sample_intensity <= 0.0) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }
  // Step 2: Compute the intensity ratio and validate the domain.
  const double ratio = reference_intensity / sample_intensity;
  if (ratio <= 0.0) {
    // Step 2a: log10(ratio) is undefined for ratio <= 0.
    return PH_EQUATIONS_ERR_DOMAIN;
  }
  // Step 3: Calculate absorbance using base-10 logarithm.
  *out_absorbance = log10(ratio);
  return PH_EQUATIONS_OK;
}

int ph_equations_calc_r_ratio(double absorbance_green, double absorbance_blue, double* out_r_ratio) {
  // Step 1: Validate pointer and denominator.
  GUARD_NONNULL(out_r_ratio);
  if (absorbance_blue <= 0.0) {
    // Step 1a: Prevent division by zero and sign-invalid ratios.
    return PH_EQUATIONS_ERR_DOMAIN;
  }
  // Step 2: Compute R = A_578 / A_434 (green over blue).
  *out_r_ratio = absorbance_green / absorbance_blue;
  return PH_EQUATIONS_OK;
}

int ph_equations_compute_ph(double r_ratio, double temperature_c, double salinity_psu, double* out_ph) {
  // Step 1: Validate pointer and physical guard rails.
  GUARD_NONNULL(out_ph);
  if (r_ratio <= 0.0 || temperature_c <= -273.15 || salinity_psu < 0.0) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }

  // ===================== Coefficient Computation (Byrne 2017 form) =====================
  // Step 2: Convert to Kelvin for temperature-dependent terms.
  const double kelvin = temperature_c + 273.15;

  // Step 3: Compute e1, e3/e2, and pK1/e2 from temperature and salinity.
  // Note: These forms mirror published relationships; impurity correction is not applied here.
  const double e1         = (-7.762e-3) + (4.5174e-5) * kelvin;
  const double e3_over_e2 = (-2.0813e-2) + (2.60262e-4) * kelvin + (1.0436e-4) * (salinity_psu - 35.0);

  // Polynomial and inverse-temperature terms for pK1/e2.
  const double s           = salinity_psu;
  const double s05         = pow(s, 0.5);
  const double s15         = pow(s, 1.5);
  const double s20         = s * s;
  const double s25         = pow(s, 2.5);
  const double invT        = 1.0 / kelvin;
  const double pk1_over_e2 = (5.561224 - 0.547716 * s05 + 0.123791 * s - 0.0280156 * s15 + 0.00344940 * s20 -
                              0.000167297 * s25 + 52.640726 * s05 * invT + 815.984591 * invT);

  // ===================== pH Computation =====================
  // Step 4: Build the log argument and validate the domain.
  const double numerator   = (r_ratio - e1);
  const double denominator = (1.0 - r_ratio * e3_over_e2);
  if (denominator == 0.0) {
    return PH_EQUATIONS_ERR_DOMAIN;
  }
  const double log_argument = numerator / denominator;
  if (log_argument <= 0.0) {
    // Step 4a: Invalid argument to log10.
    return PH_EQUATIONS_ERR_DOMAIN;
  }

  // Step 5: Compute pH.
  *out_ph = pk1_over_e2 + log10(log_argument);
  return PH_EQUATIONS_OK;
}

int ph_equations_compute_coefficients(double temperature_c, double salinity_psu, double* out_e1, double* out_e3_over_e2,
                                      double* out_pk1_over_e2) {
  // Step 1: Validate pointers and physical guard rails.
  GUARD_NONNULL(out_e1);
  GUARD_NONNULL(out_e3_over_e2);
  GUARD_NONNULL(out_pk1_over_e2);
  if (temperature_c <= -273.15 || salinity_psu < 0.0) {
    return PH_EQUATIONS_ERR_INVALID_ARG;
  }

  // Step 2: Convert to Kelvin for temperature-dependent terms.
  const double kelvin = temperature_c + 273.15;

  // Step 3: Compute coefficients.
  *out_e1         = (-7.762e-3) + (4.5174e-5) * kelvin;
  *out_e3_over_e2 = (-2.0813e-2) + (2.60262e-4) * kelvin + (1.0436e-4) * (salinity_psu - 35.0);

  const double s    = salinity_psu;
  const double s05  = pow(s, 0.5);
  const double s15  = pow(s, 1.5);
  const double s20  = s * s;
  const double s25  = pow(s, 2.5);
  const double invT = 1.0 / kelvin;
  *out_pk1_over_e2  = (5.561224 - 0.547716 * s05 + 0.123791 * s - 0.0280156 * s15 + 0.00344940 * s20 -
                      0.000167297 * s25 + 52.640726 * s05 * invT + 815.984591 * invT);

  return PH_EQUATIONS_OK;
}
