#include "adpd4101.hpp"

void adpd4101_write(uint16_t address, uint16_t data)
{
    Wire.beginTransmission(ADPD4101_I2C_ADDRESS);

    // Send the MSB of the address with the L/S bit set to 1
    Wire.write((uint8_t)((address >> 8) | 0x80));
    // Send the LSB of the address
    Wire.write((uint8_t)(address & 0x00FF));

    // Send the data MSB and LSB
    Wire.write((uint8_t)(data >> 8));
    Wire.write((uint8_t)(data & 0x00FF));

    Wire.endTransmission();
}

uint16_t adpd4101_read(uint16_t address)
{
    return adpd4101_read_multiple(address, 2);
}

uint32_t adpd4101_read_32bit(uint16_t address)
{
    return adpd4101_read_multiple(address, 4);
}

uint32_t adpd4101_read_multiple(uint16_t address, uint8_t num_reads)
{
    Wire.beginTransmission(ADPD4101_I2C_ADDRESS);

    // Send the MSB of the address with the L/S bit set to 1
    Wire.write((uint8_t)((address >> 7) | 0x80));
    // Send the LSB of the address
    Wire.write((uint8_t)(address & 0x7F));

    Wire.endTransmission(false); // Repeated start

    Wire.requestFrom(ADPD4101_I2C_ADDRESS, num_reads);
    uint32_t data = (Wire.read() << 8) | Wire.read();

    return data;
}