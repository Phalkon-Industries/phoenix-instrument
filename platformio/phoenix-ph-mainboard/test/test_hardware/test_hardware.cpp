#include <Arduino.h>
#include <unity.h>
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#include <SD.h>
#include <SPI.h>
#include <RTClib.h>
#include <TMCStepper.h>
#include <stdio.h>
#include <string.h>
#include <TMCStepper.h>
#include "adpd4101.hpp"

#define SAMPLE_PUMP_DRIVER_ADDRESS 0b00 // TMC2209 Driver address according to MS1 and MS2
#define INDICATOR_PUMP_DRIVER_ADDRESS 0b01
#define R_SENSE 0.11f
#define LTC2984_CS_PIN 11

SFE_MAX1704X fuelGauge;
Sd2Card card;
const int chipSelect = 13;
SdVolume volume;
RTC_DS3231 rtc;
TMC2209Stepper SamplePump(&Serial1, R_SENSE, SAMPLE_PUMP_DRIVER_ADDRESS);       // Create TMC driver
TMC2209Stepper IndicatorPump(&Serial1, R_SENSE, INDICATOR_PUMP_DRIVER_ADDRESS); // Create TMC driver

SPISettings ltc2984SPI(1000000, MSBFIRST, SPI_MODE0); // Adjust as needed

void setUp()
{
  Wire.begin();
  fuelGauge.enableDebugging();
  fuelGauge.begin();
  fuelGauge.quickStart();
  SD.begin();
  rtc.begin();
  Serial1.begin(115200);
  SamplePump.begin();
  IndicatorPump.begin();

  SPI.begin();
  pinMode(LTC2984_CS_PIN, OUTPUT);
  digitalWrite(LTC2984_CS_PIN, HIGH);
}
void test_adpd4101()
{
  uint8_t reg = 0x000D;
  adpd4101_write(reg, 0xDEAD);
  uint16_t val = adpd4101_read(reg);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(0xFFFF, val, "ADPD4101 not connected or not responding.");
  TEST_ASSERT_EQUAL_MESSAGE(0xDEAD, val, "ADPD4101 not functioning correctly.");
}
void test_temperature()
{
  float temperature = rtc.getTemperature();
  TEST_ASSERT_GREATER_OR_EQUAL(-40, temperature);
  TEST_ASSERT_LESS_OR_EQUAL(85, temperature);
}

void test_fuel_gauge(void)
{
  float voltage = fuelGauge.getVoltage();
  TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1, 4.0, voltage, "Fuel Guage MAX17043 not connected.");
}

void test_microSD(void)
{
  bool initSuccess = card.init(SPI_HALF_SPEED, chipSelect);
  TEST_ASSERT_TRUE_MESSAGE(initSuccess, "SD Card failed. Check wiring and ensure a card is present.");

  bool volumeInitSuccess = volume.init(&card);
  TEST_ASSERT_TRUE_MESSAGE(volumeInitSuccess, "microSD card volume initialization failed. Card might not be formatted.");

  switch (volume.fatType())
  {
  case 12:
    Serial.println("FAT12");
    break;
  case 16:
    Serial.println("FAT16");
    break;
  case 32:
    Serial.println("FAT32");
    break;
  default:
    TEST_FAIL_MESSAGE("Unknown microSD card format.");
    break;
  }
}

void test_initTime(void)
{
  const uint8_t DS3231_ADDRESS = 0x68;
  Wire.beginTransmission(DS3231_ADDRESS);
  byte error = Wire.endTransmission();
  TEST_ASSERT_EQUAL_MESSAGE(0, error, "DS3231 not found.");
}

void test_ltc2984()
{

  // Write to LTC2984 config register
  uint32_t data = 0x00000002;
  digitalWrite(LTC2984_CS_PIN, LOW);
  SPI.beginTransaction(ltc2984SPI);
  SPI.transfer(0x02);       // Write instruction
  SPI.transfer16(0x0F0);    // Address
  SPI.transfer(data >> 24); // Data MSB
  SPI.transfer((data >> 16) & 0xFF);
  SPI.transfer((data >> 8) & 0xFF);
  SPI.transfer(data & 0xFF); // Data LSB
  SPI.endTransaction();
  digitalWrite(LTC2984_CS_PIN, HIGH);

  // read from LTC2984 config register
  uint32_t rdata = 0;
  digitalWrite(LTC2984_CS_PIN, LOW);
  SPI.beginTransaction(ltc2984SPI);
  SPI.transfer(0x03);                // Read instruction
  SPI.transfer16(0x0F0);             // Address
  rdata |= SPI.transfer(0xFF) << 24; // Data MSB
  rdata |= SPI.transfer(0xFF) << 16;
  rdata |= SPI.transfer(0xFF) << 8;
  rdata |= SPI.transfer(0xFF); // Data LSB
  SPI.endTransaction();
  digitalWrite(LTC2984_CS_PIN, HIGH);

  TEST_ASSERT_EQUAL_MESSAGE(data, rdata, "LTC2984 config register not set correctly.");
}
void setup()
{
  UNITY_BEGIN();
  delay(2000);

  RUN_TEST(test_fuel_gauge);
  RUN_TEST(test_microSD);
  RUN_TEST(test_temperature);
  RUN_TEST(test_initTime);
  RUN_TEST(test_adpd4101);
  RUN_TEST(test_ltc2984);
  RUN_TEST(test_microSD);
  UNITY_END();
}

void loop()
{
}