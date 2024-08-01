#ifndef PH_MEASUREMENT_HPP
#define PH_MEASUREMENT_HPP
/**
 * @brief Calculates pH value based on the r_ratio, temperature, and salinity.
 *
 * This function uses the provided r_ratio, temperature, and salinity to calculate
 * and return the pH value based on the equations from
 *
 * @param R_ratio The r_ratio from the light sensor.
 * @param temp The in-situ water temperature in degrees Celsius.
 * @param salinity The current salinity in PSU (Practical Salinity Units).
 *
 * @return The calculated pH value. Will return -1.0 if pH inputs are invalid.
 */
double find_ph(double r_ratio, double temp, double salinity);

extern double e1;
extern double e3_e2;
extern double pk1e2;

#endif // PH_MEASUREMENT_HPP