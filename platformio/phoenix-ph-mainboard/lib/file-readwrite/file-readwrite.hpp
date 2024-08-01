#ifndef FILE_READWRITE_H
#define FILE_READWRITE_H

/**
 * @brief Initializes the SD card.
 *
 * Attempts to initialize the SD card using pin 13 as the CS (Chip Select) line. If the initialization fails,
 * it prints an error message to the Serial monitor and enters an infinite loop, effectively halting the program.
 * If initialization is successful, it prints a confirmation message.
 *
 * @note This function uses the SD library's `begin` method to initialize the SD card, which returns false if 
 * the initialization fails. The infinite loop (`while (1);`) after a failure is a brute force way to halt 
 * further execution of the program. To use this function, the Serial must be started with `Serial.begin()` 
 * prior to calling `initSD()`.
 *
 * @return void This function does not return a value.
 */
void initSD();

/**
 * @brief Opens a file with the specified file name.
 *
 * This function attempts to open a file from the storage device (like an SD card)
 * using the provided file name. It is designed to handle the opening of the file
 * for reading, writing, or both, depending on how it's implemented.
 *
 * @param fileName A pointer to a character array (C-string) that contains
 *                 the name of the file to be opened. The string should include
 *                 the file extension and, if applicable, the path.
 *
 * @note This function does not specify the mode (read, write, append, etc.) in which
 *       the file is opened. The mode should be determined within the function
 *       implementation or passed as an additional parameter if needed.
 *
 * @warning Ensure that the storage device is properly initialized and available
 *          before calling this function. The function might not check for the
 *          existence of the file and could return a null or invalid file object
 *          if the file does not exist or the storage device is not ready.
 */
File openFile(const char* fileName);

#endif