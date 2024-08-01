
#include <RTClib.h>
#include <SD.h>
#include "time.hpp"
#include "logging.hpp"

RTC_DS3231 rtc;

Time time{
    .second = 0,
    .minute = 0,
    .hour = 0,
    .day = 0,
    .month = 0,
    .year = 0};

static void updateTimeStruct()
{
    time.second = rtc.now().second();
    time.minute = rtc.now().minute();
    time.hour = rtc.now().hour();
    time.day = rtc.now().day();
    time.month = rtc.now().month();
    time.year = rtc.now().year();
    // Create timestamp string
    sprintf(time.timestamp, "%02d/%02d/%04d %02d:%02d:%02d",
            time.month, time.day, time.year,
            time.hour, time.minute, time.second);
}

bool initTime()
{
    if (!rtc.begin())
    {
        return false;
    }
    updateTimeStruct();
    return true;
}

void updateTime()
{
    updateTimeStruct();
}


void setTime(uint8_t hour, uint8_t minute, uint8_t second, uint8_t day, uint8_t month, uint8_t year) {
  rtc.adjust(DateTime(year, month, day, hour, minute, second));
  updateTimeStruct();
}

void setTimeForTest(uint8_t hour, uint8_t minute, uint8_t second, uint8_t day, uint8_t month, uint8_t year) 
{
    DateTime now(year, month, day, hour, minute, second);

    rtc.adjust(now);

    // For testing purposes
    DateTime currentTime = rtc.now();
    Serial.print("Test Time set to: ");
    Serial.print(currentTime.year(), DEC);
    Serial.print('/');
    Serial.print(currentTime.month(), DEC);
    Serial.print('/');
    Serial.print(currentTime.day(), DEC);
    Serial.print(" ");
    Serial.print(currentTime.hour(), DEC);
    Serial.print(':');
    Serial.print(currentTime.minute(), DEC);
    Serial.print(':');
    Serial.print(currentTime.second(), DEC);
    Serial.println();
}

void timestampErrorCode()
{
    DateTime now = rtc.now();

    char timestamp[128];
    char logBuffer[256];

    sprintf(timestamp, "%d/%d/%d %d:%d:%d",
            now.year(), now.month(), now.day(),
            now.hour(), now.minute(), now.second());
    Serial.println(timestamp);

    ErrorCode error = ERROR_INIT;
    const char *errorMessage = errorToMessageString(error);
    // Output: Initialization error

    // Format the log message
    sprintf(logBuffer, "Error: %s", errorMessage);
    Serial.println(logBuffer);
    // Now print the formatted log message

    delay(1000); // Log every 1 second. Adjust as needed.
}