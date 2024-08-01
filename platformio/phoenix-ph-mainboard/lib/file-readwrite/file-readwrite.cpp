#include <Arduino.h>
#include <SD.h>
#include "file-readwrite.hpp"

void initSD()
{
    if (!SD.begin(13))
    {
        Serial.println("SD card initialization failed!");
        while (1)
            ;
    }
    Serial.println("SD card initialized.");
}

File openFile(const char* fileName)
{
    File logFile = SD.open(fileName, FILE_READ);
    if (!logFile)
    {
        Serial.println("Could not open file for reading!");
    }
    return logFile;
}