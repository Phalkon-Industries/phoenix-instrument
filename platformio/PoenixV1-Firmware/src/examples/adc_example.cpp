#include <Arduino.h>
#include <SPI.h>

#define ADDRESS_BITS 0b01
#define ADDRESS_ACKNOWLEDGE_BITS 0b010
#define CONFIG0_ADDRESS 0x01

#define CONFIG0 0b10110011
#define CONFIG1 0b00001100
#define CONFIG2 0b10001011
#define CONFIG3 0b00000000

#define ADC_DATA_ADDR 0x0
#define ADC_READ 0b01000001

#define CONVERSION_START 0b1010
#define startConversion 0b01101000
#define checkConversion 0b01000000

#define WRITE_CONFIG 0b01000110
#define READ_CONFIG 0b01000111

const int SPI_CS_PIN = 13;  // Chip select pin

void printByteBinary(const char* label, uint8_t byte) {
  // Step 1: Emit the provided label so the bit pattern is identified in logs.
  Serial.print(label);
  // Step 2: Walk each bit from MSB to LSB and print its value.
  for (int i = 7; i >= 0; --i)
    Serial.print((byte >> i) & 1);
}

byte sendFastCommand(uint8_t dataToSend) {
  // Step 1: Assert chip select so the ADC listens to the command.
  digitalWrite(SPI_CS_PIN, LOW);
  // Step 2: Transfer the fast command byte and capture the response simultaneously.
  byte receivedData = SPI.transfer(dataToSend);
  // Step 3: Release chip select to conclude the transaction.
  digitalWrite(SPI_CS_PIN, HIGH);
  // Step 4: Log the sent and received bytes for debugging visibility.
  printByteBinary("Sent: 0b", dataToSend);
  Serial.print(" | ");
  printByteBinary("Received: 0b", receivedData);
  Serial.println();
  return receivedData;
}

uint32_t read_adc() {
  // Step 1: Assert chip select and issue the read command to the ADC.
  digitalWrite(SPI_CS_PIN, LOW);
  SPI.transfer(ADC_READ);

  // Step 2: Read three response bytes, building a 24-bit result.
  uint32_t adcValue = 0;
  for (int i = 0; i < 3; ++i) {
    adcValue <<= 8;
    adcValue |= SPI.transfer(0x00);
  }
  // Step 3: Release chip select so the device returns to idle.
  digitalWrite(SPI_CS_PIN, HIGH);

  // Step 4: Return the assembled sample value to the caller.
  return adcValue;
}

void read_config() {
  // Step 1: Assert chip select and send the read-config opcode.
  digitalWrite(SPI_CS_PIN, LOW);
  SPI.transfer(READ_CONFIG);

  // Step 2: Capture the four register bytes returned by the ADC.
  uint8_t configBytes[4];
  for (int i = 0; i < 4; ++i) {
    configBytes[i] = SPI.transfer(0x00);
  }
  // Step 3: Release chip select to finish the command.
  digitalWrite(SPI_CS_PIN, HIGH);

  // Step 4: Dump the register contents for inspection.
  Serial.print("Config Bytes: ");
  for (int i = 0; i < 4; ++i) {
    printByteBinary("", configBytes[i]);
    Serial.print(" ");
  }
  Serial.println();
}

void write_config() {
  // Step 1: Assert chip select and issue the write-config opcode.
  digitalWrite(SPI_CS_PIN, LOW);
  SPI.transfer(WRITE_CONFIG);

  // Step 2: Send each configuration byte in order expected by the ADC.
  SPI.transfer(CONFIG0);
  SPI.transfer(CONFIG1);
  SPI.transfer(CONFIG2);
  SPI.transfer(CONFIG3);

  // Step 3: Release chip select once the payload has been transmitted.
  digitalWrite(SPI_CS_PIN, HIGH);

  // Step 4: Inform the operator that the configuration was written.
  Serial.println("Wrote config bytes.");
}

void setup() {
  // Step 1: Start the serial port so diagnostic output is visible.
  Serial.begin(115200);
  while (!Serial) {
    ;  // Step 1a: Hold until the USB serial connection becomes available.
  }
  // Step 2: Initialize the SPI bus and configure chip select.
  SPI.begin();
  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);
  // Step 3: Fetch and rewrite the ADC configuration for a known baseline.
  read_config();
  write_config();
  read_config();
  // Step 4: Delay briefly to separate startup logs from the run loop.
  delay(1000);
}

void loop() {
  // Step 1: Trigger a conversion and allow the ADC time to accumulate data.
  sendFastCommand(startConversion);
  delay(1000);
  // Step 2: Poll the conversion status before attempting to read the result.
  sendFastCommand(checkConversion);
  delay(1000);
  // Step 3: Retrieve the sample and print it for monitoring.
  uint32_t result = read_adc();
  Serial.print("ADC Result: ");
  Serial.println(result);
}