#ifndef TIME_H
#define TIME_H

#include <RTClib.h>

extern RTC_DS3231 rtc;

struct Time
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    char timestamp[128];
};

extern Time time;

/**
 * @brief Initializes the RTC.
 *
 * This function initializes the RTC. It should be called once at the beginning
 * of the program.
 */
bool initTime();

/**
 * @brief Updates the time variable with the current time.
 *
 * This function will update the time variable with the current time.
 * The time variable is a global variable that can be accessed from
 * anywhere in the program.
 *
 * @note This function assumes that the RTC has been initialized. *
 */
void updateTime();

/**
 * @brief Sets the current time and date on the RTC.
 *
 * This function initializes the RTC with the provided time and date.
 * The RTC_DS3231 object must be globally accessible. This function
 * does not handle year conversion and expects a four-digit year input.
 *
 * @param hour The hour to set (0-23).
 * @param minute The minute to set (0-59).
 * @param second The second to set (0-59).
 * @param day The day of the month to set (1-31).
 * @param month The month to set (1-12).
 * @param year The year to set (four digits).
 */
void setTime(uint8_t hour, uint8_t minute, uint8_t second, uint8_t day, uint8_t month, uint8_t year);

/* FIXME: This one seems redundant now. My original intention for this was that you would manually set a time
in the timestruct so that you knew what the time was supposed to be and it wasn't constantly changing.
But it seems the RTC stuff is quick enough that we can set the time and check the time and still get the same answer.
Would be insteresting to see if we are able to test a delay to see if the time is changing correctly. That should be a good test.
 */
/**
 * @brief Sets the current time and date on the RTC for testing purposes.
 *
 * This function is similar to setTime, intended for use in testing scenarios.
 * It sets the RTC with the provided parameters and prints the set time to the
 * Serial monitor. This function assumes that Serial.begin() has been called
 * previously. The year parameter should be four digits.
 *
 * @param hour The hour to set (0-23) for testing.
 * @param minute The minute to set (0-59) for testing.
 * @param second The second to set (0-59) for testing.
 * @param day The day of the month to set (1-31) for testing.
 * @param month The month to set (1-12) for testing.
 * @param year The year to set (four digits) for testing.
 */
void setTimeForTest(uint8_t hour, uint8_t minute, uint8_t second, uint8_t day, uint8_t month, uint8_t year);

/**
 * @brief Logs an error code with a timestamp to the serial monitor.
 *
 * This function retrieves the current date and time from an RTC (Real-Time Clock),
 * formats it into a standard timestamp, and then prints it to the serial monitor.
 * It also logs a predefined error code and its corresponding message. The error
 * message is obtained by converting the error code to a string representation.
 *
 * @note This function assumes the RTC is already initialized and functioning.
 *       The function uses a predefined error code `ERROR_INIT` for demonstration.
 *       The actual error code should be passed as a parameter if the function
 *       is modified for general use.
 *
 * @warning The delay at the end of the function causes a pause in the execution
 *          of the program. This should be adjusted or removed according to the
 *          specific requirements of the application.
 *
 * @todo Modify the function to accept an error code as a parameter for more
 *       versatile error logging.
 */
void timestampErrorCode();

#endif