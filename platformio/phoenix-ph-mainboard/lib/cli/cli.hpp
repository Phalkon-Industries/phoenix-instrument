#ifndef CLI_HPP
#define CLI_HPP

extern char *lastCommand[32];

/**
 * @brief Executes a command from a set of predefined commands.
 *
 * This function iterates through an array of command structures. If a command with a name matching
 * the `cmd` argument is found, it executes the associated function. It also copies the executed command
 * name or a "not found" message to the `lastCommand` buffer.
 *
 * @param cmd A pointer to a null-terminated string that contains the name of the command to execute.
 * @return void This function does not return a value.
 *
 * @note This function assumes that `commands` is an array of structures with at least two members:
 * `name` (a pointer to a string) and `execute` (a pointer to a function), and that `lastCommand` is a buffer
 * with enough space to store the command name. The function `memccpy` is used to copy the command name,
 * and it stops copying when the character 0 is found or after 32 characters have been copied.
 * The `bufferedPrint` function is presumably used for outputting the result.
 */
void executeCommand(const char *cmd);

struct serialBuffer
{
    char buffer[128];
    int size;
};

extern serialBuffer serial_buffer;

/**
 * @brief Reads input from the Serial port and executes commands.
 *
 * This function reads input from the Serial port and waits for a newline character
 * to signal the end of a command. If the command is "exit", the function will enter
 * an infinite loop. Otherwise, the command will be executed using the executeCommand()
 * function. The function will then reset the input buffer and prompt the user for
 * another command.
 *
 * @note This function assumes that Serial communication has been initialized.
 */
void cliLoop(); // FIXME: I think a better name is in order here. This doesn't loop inside, but it is meant to be called in a main loop.
// Also not sure if exit is a good command to have. There isn't a use case where you want to purposefully crash the program like that. Plus it isn't in the
// command table which is a nono, all the usable commands should be in the command table.

#endif // TEST_CLI