#ifndef LOGGING_H
#define LOGGING_H

#include <Arduino.h>
#include <Wire.h>
#include <SD.h>
#include "time.hpp"

#define FILE_NAME "log2.txt"

extern char logEntry[256];

enum ErrorCode
{
    ERROR_NA = 0,
    ERROR_INIT,
    ERROR_TIMEOUT,
    ERROR_DISCONNECT,
    ERROR_OVERFLOW,
    ERROR_UNDERFLOW,
    ERROR_INVALID_ARG,
    ERROR_CUSTOM
};

// FIXME: Does this need to be a public function? Not sure I see a use for it outside of the logging library.
/**
 * @brief Converts an error code to its corresponding name string.
 *
 * This function takes an error code of type ErrorCode and returns
 * a string representing the name of the error.
 *
 * @param error The error code to be converted to a name string.
 * @return A string representing the name of the error.
 */
const char *errorToNameString(ErrorCode error);

// FIXME: Does this need to be a public function? Not sure I see a use for it outside of the logging library.
/**
 * @brief Converts an error code to its corresponding message string.
 *
 * This function takes an error code of type ErrorCode and returns
 * a string representing a human-readable message describing the error.
 *
 * @param error The error code to be converted to a message string.
 * @return A string representing the error message.
 */
const char *errorToMessageString(ErrorCode error);

/**
 * @brief Logs a string message to the SD card.
 *
 * This function takes a `const char*` message as an argument and logs it to the
 * SD card. The function is useful for logging general messages or information
 * that do not correspond to a specific error code.
 *
 * @param msg The message to log.
 *
 * @note This function assumes that the SD card has been initialized and is
 *       ready for writing.
 */
void logMsg(const char *msg); // Log any string message onto the SD card

/**
 * @brief Logs an error code without a message to the SD card.
 *
 * This function takes an `ErrorCode` as an argument and logs it to the SD card
 * without any additional message. The function is useful for logging errors
 * that do not require any additional context or explanation.
 *
 * @param error The error code to log.
 *
 * @note This function assumes that the SD card has been initialized and is
 *       ready for writing.
 */
void logError(ErrorCode error); // Log an error code (no message) onto SD Card

/**
 * @brief Logs an error code with a string message attached to the SD card.
 *
 * This function takes an `ErrorCode` and a `const char*` message for additional information as arguments
 * and logs them to the SD card. This message is useful for logging errors that require additional context or explanation.
 *
 * @param error The error code to log.
 * @param msg The message to attach to the error code.
 *
 * @note This function assumes that the SD card has been initialized and is
 *       ready for writing.
 */
void logErrorMsg(ErrorCode error, const char *msg); // Log an error code with a string message attached to SD card

/**
 * @brief Reads the entire contents of the log file and returns it as a String.
 * 
 * Attempts to open the log file defined by FILE_NAME in read mode. If successful, 
 * it reads the file until there are no more characters to read, appending each character 
 * to the `logContents` String. If the file cannot be opened, it sets `logContents` to an 
 * error message.
 *
 * @return String A String object containing the contents of the log file or an error message.
 */
String readLog();

/**
 * @brief Returns the latest log message from the log file on the SD card.
 *
 * This function returns a pointer to a buffer that contains the latest log message from the log file on the SD card.
 * The function is useful for reading the latest log message.
 *
 * @note This function assumes that the SD card has been initialized and is
 *       ready for reading.
 *
 * @return char*
 */
char *getLastLog();

/**
 * @brief Deletes a log file from the SD card.
 *
 * This function checks if the specified file exists on the SD card. If it exists,
 * the function attempts to delete it. If the file cannot be deleted, an error
 * message is printed to the serial monitor.
 *
 * @param fileName The name of the file to be deleted. It should include
 *                 any necessary path if the file is not in the root directory.
 *                 The name should be a null-terminated character array (C-string).
 *
 * @note This function assumes that the SD card is already initialized and
 *       ready for file operations.
 *
 * @warning Make sure that the file name provided is correct, as the deletion
 *          operation cannot be undone.
 */
void deleteLog(const char* fileName);

/**
 * @brief Clears the contents of the log file on the SD card.
 *
 * This function clears the contents of the log file on the SD card and starts
 * it over from the beginning. The function is useful for resetting the log file
 * and starting fresh.
 *
 * @note This function assumes that the SD card has been initialized and is
 *       ready for writing.
 */
void clearLog();

/**
 * @brief Checks if the specified file is empty.
 * 
 * Opens the file given by `fileName` and checks its size. The function 
 * returns `true` if the file is empty (i.e., size is 0). If the file does 
 * not exist or cannot be opened, the function returns `false`.
 *
 * @param fileName The path to the file to check.
 * @return bool Returns `true` if the file is empty or `false` otherwise.
 */
bool fileIsEmpty(const char* fileName);

void createFile(const char* fileName, const char* content = NULL);

#endif