#include <Arduino.h>
#include "unity.h"
#include "logging.hpp"
#include "Adafruit_TinyUSB.h"
#include "time.hpp"
#include "file-readwrite.hpp"

void setUp(void)
{
    initTime();
    initSD();
    // Set up any resources required by the tests
}

void tearDown(void)
{
    // Clean up any resources used by the tests
}

void test_error_to_string()
{
    TEST_ASSERT_EQUAL_STRING("No error", errorToMessageString(ERROR_NA));
    TEST_ASSERT_EQUAL_STRING("Initialization error", errorToMessageString(ERROR_INIT));
    TEST_ASSERT_EQUAL_STRING("Timeout error", errorToMessageString(ERROR_TIMEOUT));
    TEST_ASSERT_EQUAL_STRING("Disconnect error", errorToMessageString(ERROR_DISCONNECT));
    TEST_ASSERT_EQUAL_STRING("Overflow error", errorToMessageString(ERROR_OVERFLOW));
    TEST_ASSERT_EQUAL_STRING("Underflow error", errorToMessageString(ERROR_UNDERFLOW));
    TEST_ASSERT_EQUAL_STRING("Invalid argument error", errorToMessageString(ERROR_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("Custom error", errorToMessageString(ERROR_CUSTOM));

    ErrorCode unknownError = (ErrorCode)999;
    TEST_ASSERT_EQUAL_STRING("Unknown Error", errorToMessageString(unknownError)); // Testing an out-of-range error code
}

void test_logMsg()
{
    const char *testMsg = "Test Message_logMsg";
    logMsg(testMsg);
    File logFile = openFile(FILE_NAME);
    bool messageFound = false;
    String loggedMsg;
    while (logFile.available())
    {
        loggedMsg = logFile.readStringUntil('\n');
        loggedMsg.trim();
        if (loggedMsg == testMsg)
        {
            messageFound = true;
            break;
        }
    }
    logFile.close();

    if (!messageFound)
    {
        TEST_FAIL_MESSAGE("Test message not found in log!");
    }
    else
    {
        TEST_PASS();
    }
}

void test_logError()
{
    delay(100);
    clearLog();
    ErrorCode testError = ERROR_INIT;
    logError(testError);
    File logFile = openFile(FILE_NAME);

    String loggedError = logFile.readStringUntil('\n');
    logFile.close();

    loggedError.trim();

    String expectedError = "ERROR_INIT";

    TEST_ASSERT_EQUAL_STRING(expectedError.c_str(), loggedError.c_str());
}

void test_logErrorMsg()
{
    delay(100);
    clearLog();
    ErrorCode testError = ERROR_INIT;
    const char *testMsg = "logErrorMsg Message";
    logErrorMsg(testError, testMsg);

    File logFile = openFile(FILE_NAME);

    String loggedError = logFile.readStringUntil('\n');
    logFile.close();

    loggedError.trim();

    String expectedError = String(testError) + ": " + String(testMsg);

    TEST_ASSERT_EQUAL_STRING(expectedError.c_str(), loggedError.c_str());
}

void test_readLog()
{
    const char *testMsg = "Test Message_readLog";
    logMsg(testMsg);

    String logContents = readLog();

    TEST_ASSERT_TRUE(logContents.indexOf(testMsg) >= 0);
    // FIXME: All this test does is see if the tstMsg is in the log file string. It doesn't actually test if the log file is correct, or anything else.
    // You need to come up with a better test to make sure that this function is working as documented.
}

void test_readLargeLog()
{
    const char *testMsg = "Test Message: This is a test to see if it can handle large log files";
    for (int i = 0; i < 100; i++)
    {
        logMsg(testMsg);
    }
    logMsg(testMsg);
    const char *testMsg2 = "This is the final test to see if it can do large files";
    logMsg(testMsg2);

    String logContents = readLog();

    TEST_ASSERT_TRUE(logContents.indexOf(testMsg2) >= 0);
}

void test_deleteLog()
{
    deleteLog(FILE_NAME);
    if (SD.exists(FILE_NAME))
    {
        TEST_FAIL_MESSAGE("Log file still exists after deletion!");
    }
    else
    {
        TEST_PASS();
    }
}

void test_clearLog()
{
    const char *testMsg = "clearlog Message";
    logMsg(testMsg);

    if (fileIsEmpty(FILE_NAME))
    {
        TEST_FAIL_MESSAGE("Log is already empty before clearing!");
        return;
    }

    clearLog();

    if (!fileIsEmpty(FILE_NAME))
    {
        TEST_FAIL_MESSAGE("Log file is not empty after clearing!");
    }
    else
    {
        TEST_PASS(); // Use an appropriate macro or function to indicate the test passed
    }
}

void test_fileIsEmpty()
{
    const char *nonEmptyFileName = "test_non_empty.txt";
    createFile(nonEmptyFileName, "Hello world");
    bool result = fileIsEmpty(nonEmptyFileName);
    TEST_ASSERT_FALSE_MESSAGE(result, "Non-empty file reported as empty!");
    SD.remove(nonEmptyFileName);

    const char *emptyFileName = "12345678.txt";
    createFile(emptyFileName);
    result = fileIsEmpty(emptyFileName);
    TEST_ASSERT_TRUE_MESSAGE(result, "Empty file reported as non-empty!");
    SD.remove(emptyFileName);

    const char *nonExistentFileName = "non_existent.txt";
    result = fileIsEmpty(nonExistentFileName);
    TEST_ASSERT_FALSE_MESSAGE(result, "Non-existent file reported as empty!");
    SD.remove(nonExistentFileName);
}

void test_createFileWithContent() {
    const char* fileName = "test_with_content.txt";
    const char* content = "Hello, World!";

     if (SD.exists(fileName)) {
        SD.remove(fileName);
    }
    
    createFile(fileName, content); // Create a file with content

    File testFile = SD.open(fileName, FILE_READ);
    if (SD.exists(fileName))
    {
        TEST_ASSERT_TRUE(testFile);
    }

    String fileContent = testFile.readStringUntil('\n');
    testFile.close();

    fileContent.trim();

    TEST_ASSERT_EQUAL_STRING(content, fileContent.c_str());

    SD.remove(fileName); // Clean up
}


void setup()
{
    UNITY_BEGIN();
    delay(2000); // service delay

    RUN_TEST(test_error_to_string);
    RUN_TEST(test_logError);
    // RUN_TEST(test_logErrorMsg);
    RUN_TEST(test_readLog);
    RUN_TEST(test_readLargeLog);
    RUN_TEST(test_logMsg);
    RUN_TEST(test_deleteLog);
    RUN_TEST(test_clearLog);
    RUN_TEST(test_fileIsEmpty);
    RUN_TEST(test_createFileWithContent);
    //   FIXME: No test for getlastlog
    //

    UNITY_END();
}

void loop()
{
    // Do nothing
}