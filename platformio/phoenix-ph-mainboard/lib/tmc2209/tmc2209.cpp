#include <Arduino.h>
#include "tmc2209.hpp"

/* Helper Functions */

static void tmc2209_crc(uint8_t *datagram, uint8_t datagramLength)
{
    int i, j;
    uint8_t *crc = datagram + (datagramLength - 1); // CRC located in last byte of message
    uint8_t currentByte;
    *crc = 0;
    for (i = 0; i < (datagramLength - 1); i++)
    {
        // Execute for all bytes of a message
        currentByte = datagram[i];
        // Retrieve a byte to be sent from Array
        for (j = 0; j < 8; j++)
        {
            if ((*crc >> 7) ^ (currentByte & 0x01))
            // update CRC based result of XOR operation
            {
                *crc = (*crc << 1) ^ 0x07;
            }
            else
            {
                *crc = (*crc << 1);
            }
            currentByte = currentByte >> 1;
        } // for CRC bit
    }     // for message byte
}

/* Interface Functions */

void tmc2209_write(TMC2209_t pump, uint16_t address, uint16_t data)
{
}

uint32_t tmc2209_read(TMC2209_t pump, uint16_t address)
{
    // Construct datagram
    uint8_t datagram[4] = {0};
    // first byte is 10100 0000
    datagram[0] = 0b00000101;
    // second byte is device address
    datagram[1] = pump.device_address;
    // third byte is register address
    datagram[2] = address;
    // fourth byte is crc
    tmc2209_crc(datagram, 4);

    // send datagram on pump serial
    pump.serial->write(datagram, 4);
    // read response
    uint8_t response_datagram[8];
    size_t err = pump.serial->readBytes(response_datagram, 8);
    // response data is in bytes 3-6
    uint32_t response = 0;
    for (int i = 3; i < 7; i++)
    {
        response = response << 8;
        response |= response_datagram[i];
    }

    if (err != 8)
    {
        Serial.println("Error reading response from TMC2209");
        return -1;
    }
    return response;
}

void tmc2209_init(TMC2209_t pump)
{
    // set up driver
    tmc2209_write(pump, 0x00, R00); // GCONF
}
