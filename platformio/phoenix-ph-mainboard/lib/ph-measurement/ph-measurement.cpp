#include <math.h>
#include "ph-measurement.hpp"
double e1 = 0;
double e3_e2 = 0;
double pk1e2 = 0;
double find_ph(double r_ratio, double temp, double salinity)
{
    if (temp <= -273.15 || salinity < 0 || r_ratio <= 0)
    {
        return -1.0;
    }
    double kelvin = temp + 273.15;
    e1 = -0.007762 + 4.5174 * 0.00001 * kelvin;
    e3_e2 = -0.020813 + 2.60262 * (0.0001 * kelvin) + 1.0436 * (0.0001 * (salinity - 35));
    pk1e2 = (5.561224 - 0.547716 * pow(salinity, 0.5) + 0.123791 * salinity - 0.0280156 * pow(salinity, 1.5) + 0.00344940 * pow(salinity, 2) - 0.000167297 * pow(salinity, 2.5) + 52.640726 * pow(salinity, 0.5) * pow(kelvin, -1) + 815.984591 * pow(kelvin, -1));
    double log_argument = (r_ratio - e1) / (1 - r_ratio * e3_e2);
    if (log_argument < 0)
    {
        return -1.0;
    }
    double ph_value = pk1e2 + log10(log_argument);
    return ph_value;
}