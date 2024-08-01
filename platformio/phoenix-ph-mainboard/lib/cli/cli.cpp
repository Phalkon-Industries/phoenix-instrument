#include <Arduino.h>
#include "cli.hpp"
#include <stdio.h>
#include <string.h>
#include "logging.hpp"
// #include "Adafruit_TinyUSB.h"

// global variables
char *lastCommand[32] = {0};

// initialize serial_buffer
serialBuffer serial_buffer = {
    .buffer = {0},
    .size = 0,
};

char inputBuffer[50];
int inputIndex = 0;

// Function declarations
static void help();
static void statusReport();
static void start();
static void stop();
static void settings();
static void runTest();
static void readTheLog();

typedef void (*functionPointerType)();

struct CommandStruct
{
    const char *name;
    functionPointerType execute;
    const char *help;
};

const CommandStruct commands[] = {
    {"stop", stop, "Stop the system"},
    {"help", help, "list of all commands"},
    {"status", statusReport, "Get the status report of ECLIPSE-pH"},
    {"start", start, "Start ECLIPSE-pH"},
    {"setting", settings, "Change your settings for pump, optical sensor, etc."},
    {"test", runTest, "Test sensors and board"},
    {"log", readTheLog, "start logging the data"},
    {"", 0, ""} // End of table indicator. MUST BE LAST!!!
};

// FUNCTIONS
static void buffer(const char *string)
{
    memccpy(serial_buffer.buffer, string, 0, 128);
    serial_buffer.size = strlen(serial_buffer.buffer);
}
static void bufferedPrint(const char *string)
{
    buffer(string);
    Serial.write(serial_buffer.buffer, serial_buffer.size);
}

static void help()
{
    for (int i = 0; commands[i].name && commands[i].execute; i++)
    {
        Serial.print(commands[i].name);
        Serial.print(": ");
        Serial.println(commands[i].help);
    }
}

static void statusReport()
{
    Serial.println("Status report for ECLIPSE-pH");
}

static void start()
{
    Serial.println("Starting ECLIPSE-pH...");
}

static void stop()
{

    bufferedPrint("ECLIPSE-pH stopped\n\r");
}

static void settings()
{
    Serial.println("Change your settings");
}

static void runTest()
{
    Serial.println("Testing all the sensors and board...");
}

static void readTheLog()
{
    Serial.println("Logging data...");
}

void executeCommand(const char *cmd)
{
    int i = 0;
    while (commands[i].name && commands[i].execute)
    {
        if (strcmp(commands[i].name, cmd) == 0)
        {
            commands[i].execute();
            memccpy(lastCommand, cmd, 0, 32);
            return;
        }
        i++;
    }
    bufferedPrint("Command not found.\n\r");
    memccpy(lastCommand, "not found", 0, 32);
}

void cliLoop()
{
    if (Serial.available() > 0)
    {
        char receivedChar = Serial.read();
        if (receivedChar == '\n')
        {
            inputBuffer[inputIndex] = '\0';
            if (strcmp(inputBuffer, "exit") == 0)
            {
                Serial.println("Exiting...");
                while (1)
                    ;
            }
            executeCommand(inputBuffer);
            inputIndex = 0;
            Serial.println("Enter command (or 'help' for list, 'exit' to quit): ");
        }
        else if (inputIndex < 49)
        {
            inputBuffer[inputIndex++] = receivedChar;
        }
    }
}