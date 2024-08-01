#ifndef ADPD4101_H
#define ADPD4101_H

#include <Arduino.h>
#include <Wire.h>

#define ADPD4101_I2C_ADDRESS 0x24

/**
 * @brief Writes data to a specified register on the ADPD4101 using the long address protocol.
 * @param address 15-bit register address.
 * @param data 16-bit data to write to the register.
 */
void adpd4101_write(uint16_t address, uint16_t data);

/**
 * @brief Reads data from a specified register on the ADPD4101 using the long address protocol.
 * @param address 15-bit register address.
 * @return 16-bit data read from the register.
 */
uint16_t adpd4101_read(uint16_t address);

/**
 * @brief Reads data from a specified register on the ADPD4101 using the long address protocol.
 * @param address 15-bit register address.
 * @return 32-bit data read from the register.
 */
uint32_t adpd4101_read_32bit(uint16_t address);

/**
 * @brief Reads multiple bytes of data from a specified register on the ADPD4101 using the long address protocol.
 * @param address 15-bit register address.
 * @param num_reads Number of bytes to read from the register.
 * @return 32-bit data read from the register.
 */
uint32_t adpd4101_read_multiple(uint16_t address, uint8_t num_reads);

#endif