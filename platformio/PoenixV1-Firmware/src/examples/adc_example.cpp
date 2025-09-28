#include <Arduino.h>
#include <SPI.h>
#include "main.hpp" // For PIN definitions if desired (PIN_ADC_CS, etc.)

/*
  Minimal MCP3564 (MCP356x family) polling example
  ------------------------------------------------
  Purpose: Provide the smallest workable sketch to verify SPI communication
           and obtain raw conversion data by polling (no interrupts, no PID).

  Wiring assumptions:
    SPI_CS_PIN  -> MCP3564 CS
    ADC_INT_PIN -> MCP3564 IRQ/DRDY (active low) (optional; code also timeouts)
    SCK/MOSI/MISO -> standard SPI

  Flow:
    1. Hardware reset sequence (FAST_CMD RESET)
    2. Basic configuration writes (CONFIG0..MUX) for one single-ended channel SE_0
    3. Start single conversion (FAST_CMD CONV) each loop iteration
    4. Poll IRQ pin (or timeout) until data ready
    5. Read 32-bit frame (status + 24-bit data + sign extend bits per existing mask logic)
    6. Print raw code and converted signed value

  Notes:
    - Adjust SPI speed if initial comms fail (e.g. try 1 MHz first).
    - OSR / gain / reference choices are simplified; tweak CONFIG registers for your design.
    - No power management, no error handling beyond basic timeout.
*/

// ---------------- Pin Selection (override here if not using main.hpp) ----------------
#ifndef SPI_CS_PIN
#define SPI_CS_PIN 13
#endif
#ifndef ADC_INT_PIN
#define ADC_INT_PIN 41
#endif

// ---------------- Basic Register / Command Definitions ----------------
static const uint32_t SPI_SPEED = 1000000UL; // Start conservative (1 MHz); raise after it works

// MCP356x base address (device address bits A2..A0 = 000) shift pattern per datasheet
static const uint8_t ADDR          = 0b01000000; // Write, default dev addr (A2..A0=000)

// Fast command opcodes (OR with ADDR)
static const uint8_t FAST_CMD      = 0b00000000; // Base for fast command space
static const uint8_t CONV          = 0b00101000; // Start conversion
static const uint8_t STBY          = 0b00101100; // Standby (unused here)
static const uint8_t RESET_CMD     = 0b00111000; // Reset device

// Register addresses (write inc mode usage)
static const uint8_t INC_WRITE     = 0b00000010; // Incremental write, add to ADDR + first register base
static const uint8_t CONFIG0_ADDR  = 0b00000100; // CONFIG0 register address field
static const uint8_t ADC_REG       = 0b00000000; // ADC data register read code
static const uint8_t READ          = 0b00000001; // Read command bit pattern

// Simple configuration values (these are examples; tailor to your design)
// CONFIG0: Vref selection, CLK source etc. Here: Internal clock select bit (bit5) left 1 for internal.
static const uint8_t CONFIG0 = 0b11010010; // Example from prior code (gain, ref, etc.)
// CONFIG1: OSR setting placed at bits [6:2]; using OSR=128 as earlier
static const uint8_t OSR_128 = 0x02; // from earlier enum mapping
static const uint8_t CONFIG1 = (OSR_128 << 2);
// CONFIG2: Gain etc. (reuse prior value)
static const uint8_t CONFIG2 = 0b10001011;
// CONFIG3: Conversion mode etc.
static const uint8_t CONFIG3 = 0b11110000;
// IRQ: Interrupt config (reuse prior value to enable DRDY)
static const uint8_t IRQ_REG = 0b01110110;
// MUX: Select channel SE_0 (bits per prior code 0b01111000 baseline) -> keep original
static const uint8_t MUX = 0b01111000; // Single-ended Channel 0 selected

// ---------------- Helper: Tiny delay (replace prior ns delays) ----------------
static inline void tinyDelay() { delayMicroseconds(1); }

// ---------------- Low-level SPI helpers ----------------
static void adcWriteConfigBlock() {
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_CS_PIN, LOW);
  tinyDelay();
  // Incremental write starting at CONFIG0
  SPI.transfer(ADDR | CONFIG0_ADDR | INC_WRITE);
  SPI.transfer(CONFIG0);
  SPI.transfer(CONFIG1);
  SPI.transfer(CONFIG2);
  SPI.transfer(CONFIG3);
  SPI.transfer(IRQ_REG);
  SPI.transfer(MUX);
  digitalWrite(SPI_CS_PIN, HIGH);
  SPI.endTransaction();
}

static void adcFastCommand(uint8_t cmd) {
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_CS_PIN, LOW);
  tinyDelay();
  SPI.transfer(ADDR | cmd | FAST_CMD);
  digitalWrite(SPI_CS_PIN, HIGH);
  SPI.endTransaction();
}

static bool adcReadRaw(uint32_t *raw32) {
  if(!raw32) return false;
  SPI.beginTransaction(SPISettings(SPI_SPEED, MSBFIRST, SPI_MODE0));
  digitalWrite(SPI_CS_PIN, LOW);
  tinyDelay();
  SPI.transfer(ADDR | ADC_REG | READ);
  uint32_t v = 0;
  v |= (uint32_t)SPI.transfer(0) << 24; // STATUS + upper bits
  v |= (uint32_t)SPI.transfer(0) << 16;
  v |= (uint32_t)SPI.transfer(0) << 8;
  v |= (uint32_t)SPI.transfer(0);
  digitalWrite(SPI_CS_PIN, HIGH);
  SPI.endTransaction();
  *raw32 = v;
  return true;
}

// Extract 25-bit (or 24-bit) signed data from raw frame.
// Prior code masked 0x01FFFFFF and shifted <<7 then /0x3FFFFFFF, implying sign bit at bit 24 of a 25-bit value.
static int32_t adcConvertSigned(uint32_t raw) {
  int32_t data25 = (int32_t)(raw & 0x01FFFFFF); // 25 bits
  // Sign-extend 25-bit value to 32 bits
  if(data25 & (1 << 24)) { // sign bit
    data25 |= ~0x01FFFFFF;
  }
  return data25; // Raw signed code
}

// ---------------- Setup ----------------
void setup() {
  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);

  pinMode(ADC_INT_PIN, INPUT_PULLUP); // DRDY typically active low

  Serial.begin(115200);
  while(!Serial && millis() < 1500) { /* wait */ }
  Serial.println();
  Serial.println(F("MCP356x Minimal Polling Example"));

  SPI.begin();
  delay(5);

  // Reset device
  adcFastCommand(RESET_CMD);
  delay(5);

  // Write basic configuration
  adcWriteConfigBlock();
  delay(2);
}

// ---------------- Loop: single-shot conversion poll ----------------
void loop() {
  // Start a single conversion
  adcFastCommand(CONV);

  // Poll DRDY (IRQ pin low) with timeout (2 ms)
  const uint32_t tStart = micros();
  while (digitalRead(PIN_ADC_IRQ) != LOW) {
    if ((micros() - tStart) > 2000) { // timeout
      Serial.println(F("Timeout waiting for DRDY"));
      break;
    }
  }

  uint32_t rawFrame = 0;
  if (adcReadRaw(&rawFrame)) {
    // Upper nibble (bits 31..28) contains channel/tag per earlier code
    uint8_t tag = (rawFrame >> 28) & 0x0F;
    int32_t signedCode = adcConvertSigned(rawFrame);

    // Convert to floating voltage ratio if desired: signedCode / (double)0x01000000 ...
    // Reuse earlier scaling: value * 3.3 * 0.93 * Current_Sense_Gain etc. kept out for simplicity.

    Serial.print(F("Tag=")); Serial.print(tag);
    Serial.print(F(" Raw=0x")); Serial.print(rawFrame, HEX);
    Serial.print(F(" Signed=")); Serial.print(signedCode);
    Serial.println();
  } else {
    Serial.println(F("SPI read failed"));
  }

  delay(250); // Slow down output for readability
}
