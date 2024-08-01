#include <Arduino.h>
#include "unity.h"
#include "cli.hpp"
#include "Adafruit_TinyUSB.h"

// Test cases
void testExecuteCommand_ParseValidCommand()
{
    delay(1000);
    const char *commands[] = {
        "stop",
        "help",
        "status",
        "start",
        "setting",
        "test",
        "log",
        nullptr};

    for (int i = 0; commands[i] != nullptr; ++i)
    {
        delay(1000);
        executeCommand(commands[i]);
        TEST_ASSERT_EQUAL_STRING(commands[i], lastCommand);
    }
}

void testExecuteCommand_ParseInvalidCommand()
{
    char input[] = "invalid";
    executeCommand(input);
    TEST_ASSERT_EQUAL_STRING("not found", lastCommand);
}

void testExecuteCommand_ExecuteStopCommand()
{

    delay(1000);
    char input[] = "stop";
    executeCommand(input);
    // Stop is going to run and put into Serial Buffer
    TEST_ASSERT_EQUAL_STRING("ECLIPSE-pH stopped\n\r", serial_buffer.buffer);
}

void setUp()
{
    // Set up any resources required by the tests
}

void tearDown()
{
    // Clean up any resources used by the tests
}

void setup()
{

    Serial.begin(115200);
    Serial.flush();
    while (!Serial)
        ;
    UNITY_BEGIN();
    delay(2000); // service delay

    RUN_TEST(testExecuteCommand_ParseValidCommand);
    RUN_TEST(testExecuteCommand_ParseInvalidCommand);
    RUN_TEST(testExecuteCommand_ExecuteStopCommand);

    UNITY_END();
}

void loop()
{
}
