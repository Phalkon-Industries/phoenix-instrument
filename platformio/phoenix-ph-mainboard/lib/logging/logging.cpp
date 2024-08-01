#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <SD.h>
#include "logging.hpp"
#include "time.hpp"
#include "Adafruit_TinyUSB.h"

extern RTC_DS3231 rtc;

char lastLogMessage[32] = {0};
char logEntry[256];

typedef void (*functionPointerType)();

struct ErrorStruct
{
    const char *errorName;
    ErrorCode errorCode;
    const char *errorMessage;
};

const ErrorStruct errors[] = {
    {"ERROR_NA", ERROR_NA, "No error"},
    {"ERROR_INIT", ERROR_INIT, "Initialization error"},
    {"ERROR_TIMEOUT", ERROR_TIMEOUT, "Timeout error"},
    {"ERROR_DISCONNECT", ERROR_DISCONNECT, "Disconnect error"},
    {"ERROR_OVERFLOW", ERROR_OVERFLOW, "Overflow error"},
    {"ERROR_UNDERFLOW", ERROR_UNDERFLOW, "Underflow error"},
    {"ERROR_INVALID_ARG", ERROR_INVALID_ARG, "Invalid argument error"},
    {"ERROR_CUSTOM", ERROR_CUSTOM, "Custom error"},
    {nullptr, (ErrorCode)0, nullptr} // End of an array indicator
};

const char *errorToNameString(ErrorCode error)
{
    for (int i = 0; errors[i].errorName != nullptr; ++i)
    {
        if (errors[i].errorCode == error)
        {
            return errors[i].errorName;
        }
    }
    return "Unknown Error";
}

const char *errorToMessageString(ErrorCode error)
{
    for (int i = 0; errors[i].errorName != nullptr; ++i)
    {
        if (errors[i].errorCode == error)
        {
            return errors[i].errorMessage;
        }
    }
    return "Unknown Error";
}

void writeMsg(const char *filePath, const char *msg) {
    File file = SD.open(filePath, FILE_WRITE);
    if (file) {
        file.println(msg);
        file.close();
    } else {
        Serial.println("Error writing message to file");
    }
}

void writeToLog(const char *msg) {
    writeMsg(FILE_NAME, msg);
}

void logMsg(const char *msg)
{
    Serial.println("Enter your message: ");
    writeToLog(msg);
    Serial.println("Message logged");
}; // Log any string message

void logError(ErrorCode error)
{
    Serial.println("Enter your error code: ");
    const char *errorStr = errorToNameString(error);

    snprintf(logEntry, sizeof(logEntry), "%s", errorStr);

    writeToLog(logEntry);
    Serial.println("Error logged");
}; // Log an error code (no message)

void logErrorMsg(ErrorCode error, const char *msg)
{
    Serial.println("Enter your error code: ");
    const char *errorStr = errorToNameString(error);
    Serial.println("Enter your message: ");

    snprintf(logEntry, sizeof(logEntry), "Error: %s - Message: %s", errorStr, msg);

    writeToLog(logEntry);

    Serial.println("Error and message logged");
}; // Log an error code with a string message attached

String readLog()
{
    String logContents;
    File logFile = SD.open(FILE_NAME, FILE_READ);
    if (logFile)
    {
        while (logFile.available())
        {
            logContents += (char)logFile.read();
        }
        logFile.close();
    }
    else
    {
        logContents = "Error opening log file";
    }
    return logContents;
} // Print the log from the SD card to the terminal

char *getLastLog()
{
    //
    return lastLogMessage;
}

void deleteLog(const char* fileName)
{
    if (SD.exists(fileName)) {
        if (!SD.remove(fileName)) {
            Serial.println("Error deleting log file");
            return;
        }
    }
}

void clearLog()
{
    deleteLog(FILE_NAME);
    File logFile = SD.open(FILE_NAME, FILE_WRITE);
    if (logFile)
    {
        logFile.close();
        Serial.println("Log file cleared");
    }
    else
    {
        Serial.println("Error creating log file");
    }
};

bool fileIsEmpty(const char* fileName)
{
    File logFile = SD.open(fileName, FILE_READ);
    if (!logFile) {
        return false; // Cannot open the file, so cannot determine if it's empty
    }

    uint32_t size = logFile.size();
    logFile.close();
    return size == 0;
}

void createFile(const char* fileName, const char* content)
{
    if (!SD.exists(fileName))
    {
        File logFile = SD.open(fileName, FILE_WRITE);
        if (logFile)
        {
            if (content != NULL && strlen(content) > 0) {
                logFile.println(content);
            }
            logFile.close();
        }
        else
        {
            Serial.println("Error creating file");
        }
    }
    else
    {
        Serial.println("File already exists");
    }
}