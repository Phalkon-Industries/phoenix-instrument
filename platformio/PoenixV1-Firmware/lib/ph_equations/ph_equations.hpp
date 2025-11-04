#ifndef PH_EQUATIONS_HPP
#define PH_EQUATIONS_HPP

#include "phoenix_guard.hpp"

/**
 * \file ph_equations.hpp
 * \brief Spectrophotometric pH equations utilities.
 *
 * This header exposes C-callable helpers to compute absorbance, the dual-wavelength R-ratio,
 * and spectrophotometric pH using temperature- and salinity-dependent coefficients.
 *
 * Notes:
 * - Impurity correction is currently unimplemented and will be introduced in a later phase.
 * - Ratio definition follows common practice for m-cresol purple: R = A_578 / A_434 (green/blue).
 */

#ifdef __cplusplus
extern "C" {
#endif

// Error codes for pH equations helpers.
// Return early on invalid inputs; do not write to out parameters on error.
// Values chosen to align with project conventions (OK >= 0; errors < 0).

enum PhEquationsError {
  PH_EQUATIONS_OK              = PHX_OK,
  PH_EQUATIONS_ERR_INVALID_ARG = PHX_ERR_INVALID_ARG,
  PH_EQUATIONS_ERR_DOMAIN      = PHX_ERR_MODULE_BASE - 1,
};

/**
 * Compute absorbance for a wavelength from reference and sample intensities.
 *
 * A = log10(reference_intensity / sample_intensity).
 *
 * Parameters:
 * - reference_intensity: positive reference intensity (dark-corrected if applicable).
 * - sample_intensity: positive sample intensity (dark-corrected if applicable).
 * - out_absorbance: non-null pointer to receive the absorbance value.
 *
 * Returns:
 * - PH_EQUATIONS_OK on success and writes to out_absorbance.
 * - PH_EQUATIONS_ERR_INVALID_ARG for null pointer or non-positive inputs.
 */
int ph_equations_calc_absorbance(double reference_intensity, double sample_intensity, double* out_absorbance);

/**
 * Compute the dual-wavelength R-ratio.
 *
 * Definition used here: R = A_578 / A_434 (green over blue).
 *
 * Parameters:
 * - absorbance_green: absorbance at 578 nm (A_578).
 * - absorbance_blue: absorbance at 434 nm (A_434).
 * - out_r_ratio: non-null pointer to receive the ratio.
 *
 * Returns:
 * - PH_EQUATIONS_OK on success and writes to out_r_ratio.
 * - PH_EQUATIONS_ERR_INVALID_ARG if out_r_ratio is null.
 * - PH_EQUATIONS_ERR_DOMAIN if absorbance_blue <= 0 (division by zero or sign invalid).
 */
int ph_equations_calc_r_ratio(double absorbance_green, double absorbance_blue, double* out_r_ratio);

/**
 * Compute pH given R-ratio, temperature, and salinity using the Byrne 2017 formulation.
 *
 * Parameters:
 * - r_ratio: positive dual-wavelength ratio (R).
 * - temperature_c: temperature in Celsius; must be greater than absolute zero (-273.15 C).
 * - salinity_psu: practical salinity units; must be non-negative.
 * - out_ph: non-null pointer to receive the computed pH on success.
 *
 * Returns:
 * - PH_EQUATIONS_OK on success and writes to out_ph.
 * - PH_EQUATIONS_ERR_INVALID_ARG for null out pointer or invalid physical inputs.
 * - PH_EQUATIONS_ERR_DOMAIN if intermediate terms are outside valid domain (e.g., log argument <= 0).
 */
int ph_equations_compute_ph(double r_ratio, double temperature_c, double salinity_psu, double* out_ph);

/**
 * Compute intermediate coefficients used by the pH equation (Byrne 2017 form).
 *
 * Coefficients:
 * - e1 = a0 + a1*T
 * - e3/e2 = b0 + b1*T + b2*(S - 35)
 * - pK1/e2 = polynomial in S and inverse temperature
 *
 * Parameters:
 * - temperature_c: temperature in Celsius; must be greater than absolute zero (-273.15 C).
 * - salinity_psu: practical salinity units; must be non-negative.
 * - out_e1: non-null pointer to receive e1.
 * - out_e3_over_e2: non-null pointer to receive e3/e2.
 * - out_pk1_over_e2: non-null pointer to receive pK1/e2.
 *
 * Returns:
 * - PH_EQUATIONS_OK on success and writes all outputs.
 * - PH_EQUATIONS_ERR_INVALID_ARG for null outputs or invalid physical inputs.
 */
int ph_equations_compute_coefficients(double temperature_c, double salinity_psu, double* out_e1, double* out_e3_over_e2,
                                      double* out_pk1_over_e2);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PH_EQUATIONS_HPP
