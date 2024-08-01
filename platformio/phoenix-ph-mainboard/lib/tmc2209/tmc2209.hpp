#ifndef TMC2209_H
#define TMC2209_H

#include <Arduino.h>

/* GCONF */
#define I_SCALE_ANALOG 0   // 0
#define INTERNAL_RSENSE 0  // 1
#define EN_SPREADCYCLE 0   // 2
#define SHAFT 0            // 3
#define INDEX_OTPW 0       // 4
#define INDEX_STEP 0       // 5
#define PDN_DISABLE 1      // 6
#define MSTEP_REG_SELECT 1 // 7
#define MULTISTEP_FILT 1   // 8
#define TEST_MODE 0        // 9
#define R00                                                                                  \
    ((I_SCALE_ANALOG << 0) | (INTERNAL_RSENSE << 1) | (EN_SPREADCYCLE << 2) | (SHAFT << 3) | \
     (INDEX_OTPW << 4) | (INDEX_STEP << 5) | (PDN_DISABLE << 6) | (MSTEP_REG_SELECT << 7) |  \
     (MULTISTEP_FILT << 8) | (TEST_MODE << 9)) // GCONF

struct TMC2209_t
{

    Uart *serial;
    uint8_t device_address;
};

/**
 * Writes the specified data to the specified register address of the TMC2209 driver.
 * This function sends the register address and data to the TMC2209 driver via UART.
 * The `pump` parameter specifies which TMC2209 driver to write to, and the `address`
 * parameter specifies which register to write to. The `data` parameter specifies the
 * value to write to the register.
 *
 * @note This function assumes that the UART interface has already been initialized.
 *
 * @param pump The TMC2209_t object representing the TMC2209 driver to write to.
 * @param address The address of the register to write to.
 * @param data The data to write to the register.
 *
 * @return void
 */
void tmc2209_write(TMC2209_t pump, uint16_t address, uint16_t data);

/**
 * Reads the current value of the specified register address of the TMC2209 driver.
 * This function sends the register address to the TMC2209 driver via UART, and reads
 * the current value of the register. The `pump` parameter specifies which TMC2209
 * driver to read from, and the `address` parameter specifies which register to read.
 *
 * @note This function assumes that the UART interface has already been initialized.
 *
 * @param pump The TMC2209_t object representing the TMC2209 driver to read from.
 * @param address The address of the register to read from.
 *
 * @return The current value of the specified register address.
 */
uint32_t tmc2209_read(TMC2209_t pump, uint16_t address);

void tmc2209_init(TMC2209_t pump);

#endif