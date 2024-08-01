/**
 * @file adpd4101example.cpp
 * @author Jonathan Pfeifer
 * @brief Example code for the ADPD4101 which turns on the two LEDs and matching PDs and print to serial.
 * @version 0.1
 * @date 2023-12-19
 *
 * @copyright Copyright (c) 2023
 *
 */
#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "adpd4101.hpp"
#include "adpd410x.h"

// Define a structure to hold register address and value
typedef struct
{
    unsigned int address;
    unsigned int value;
} RegisterValue;

// Initialize an array of structures with your register values and comments

RegisterValue registerValues[] = {
    {0x0F, 0x8000}, // SYS_CTL : software reset
    {0x0F, 0x0006}, // SYS_CTL : use and enable 32kHZ oscillator
                    // {0x00, 0x0048}, // FIFO_STATUS : unsure, supposed to be Read only.
                    // {0x01, 0x000F}, // INT_STATUS_DATA : clearing timeslot interrupts
    {0x0D, 0x4E20}, // TS_FREQ : 50Hz frequency with 1MHz clock
    {0x0E, 0x0000}, // TS_FREQH : see above
    {0x10, 0x0070}, // OPMODE : enable timeslots, no operation

    // Optical path 1
    {0x102, 0x0001}, // INPUTS_A : IN1 to channel 1, IN2 disconnected
    {0x105, 0x0020}, // LED_POW12_A : LED 2 DRIVE on B, LED1 at 96ma
    {0x101, 0x40DA}, // TS_PATH_A : TIA, BPF, integrator, and ADC; Precondition 8us (both defaults)
    {0x100, 0x0000}, // TS_CTRL A : CH2 disable
    {0x103, 0x5082}, // CATHODE_A : Precondition PDs to TIA_VREF; VC2 active is TIA_VREF+215mV;TIA_VREF + 215 mV
    {0x104, 0x2BD1}, // CH2,CH1_TRIM_INT 100kohm; No pulsing; TIA_VREF = 1.265V; TIA_GAIN_CH2 50kohm, TIA_GAIN_CH1 100kohm
    {0x107, 0x0101}, // COUNTS_A : 0 repeats
    {0x109, 0x0220}, // LED_PULSE_A : 2us LED pulse with a 32us offset
    {0x10A, 0x0003}, // INTEG_SETUP_A : 3us AFE width (default value)
    {0x10B, 0x03FC}, // INTEG_OS_A : ~63.390us integrator offset (og comment claims 32?)
    {0x10D, 0x00AA}, // PATTERN_A : 4 pulse chop with alternate positive negative
    {0x10F, 0x8000}, // ADC_OFF2_A : 2048 digital offset for channel 2 (not sure of purpose)
    {0x110, 0x0003}, // DATA_FORMAT_A : 3 byte wide data

    // path2
    {0x120, 0x0000}, // CH2 Disable
    {0x121, 0x40DA}, // Standard TS Path
    {0x122, 0x0030}, // IN3 connected to PD2B(PCB has PD2A and PD2B Switched)
    {0x12D, 0x00AA}, // PATTERN_A : 4pulse chop with alternative Postive negativ
    {0x130, 0x0003}, // 3 Byte Data/
    {0x125, 0x2000}, // Drive LED2A at 96mA
    {0x126, 0x0000}, // LED3,4 Off
    {0x127, 0x0101}, // only single acquisition
    {0x123, 0x5082},
    {0x124, 0x2BD1},
    {0x12b, 0x03fc},
    {0x129, 0x0220}
    /* more stuff for later use
    00b, 6b2
    022, 0001 //GPIO Enabled input for GPIO0
    026, 0000 //GPIO0 for External synch
    */
};
void setup()
{
    Serial.begin(115200);
    Wire.begin();
    delay(2000);
    while (!Serial)
    {
    }; // wait for serial monitor to openS
    Serial.println("ADPD4101 Setting Default State...");
    int numRegisterValues = sizeof(registerValues) / sizeof(registerValues[0]);
    for (int i = 0; i < numRegisterValues; i++)
    {
        // Call your function to write to the register
        adpd4101_write(registerValues[i].address, registerValues[i].value);
    }
    adpd4101_write(0x10, 0x0101); // OP_MODE (start A and B)
}

void loop()
{
    // read adpd4101 measurement and print to serial
    /*FIFO_DATA:
     */
    uint32_t data_A = adpd4101_read_32bit(ADPD410X_REG_SIGNAL1_L(0));
    uint32_t data_B = adpd4101_read_32bit(ADPD410X_REG_SIGNAL1_L(1));
    Serial.print("A Data: ");
    Serial.print(data_A);
    Serial.print("\tB Data ");
    Serial.println(data_B);
    delay(100);
}