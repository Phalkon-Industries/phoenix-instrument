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
  Serial.print(label);
  for (int i = 7; i >= 0; --i)
    Serial.print((byte >> i) & 1);
}

byte sendFastCommand(uint8_t dataToSend) {
  digitalWrite(SPI_CS_PIN, LOW);  // Select device
  byte receivedData = SPI.transfer(dataToSend);
  digitalWrite(SPI_CS_PIN, HIGH);  // Deselect device
  printByteBinary("Sent: 0b", dataToSend);
  Serial.print(" | ");
  printByteBinary("Received: 0b", receivedData);
  Serial.println();
  return receivedData;
}

uint32_t read_adc() {
  digitalWrite(SPI_CS_PIN, LOW);  // Select device
  SPI.transfer(ADC_READ);         // Send ADC read command

  uint32_t adcValue = 0;
  for (int i = 0; i < 3; ++i) {
    adcValue <<= 8;
    adcValue |= SPI.transfer(0x00);  // Read 3 bytes (24 bits)
  }
  digitalWrite(SPI_CS_PIN, HIGH);  // Deselect device

  return adcValue;
}

void read_config() {
  digitalWrite(SPI_CS_PIN, LOW);  // Select device
  SPI.transfer(READ_CONFIG);      // Send read config command

  uint8_t configBytes[4];
  for (int i = 0; i < 4; ++i) {
    configBytes[i] = SPI.transfer(0x00);  // Read 4 bytes
  }
  digitalWrite(SPI_CS_PIN, HIGH);  // Deselect device

  Serial.print("Config Bytes: ");
  for (int i = 0; i < 4; ++i) {
    printByteBinary("", configBytes[i]);
    Serial.print(" ");
  }
  Serial.println();
}

void write_config() {
  digitalWrite(SPI_CS_PIN, LOW);  // Select device
  SPI.transfer(WRITE_CONFIG);     // Send write config command

  SPI.transfer(CONFIG0);  // Write CONFIG0
  SPI.transfer(CONFIG1);  // Write CONFIG1
  SPI.transfer(CONFIG2);  // Write CONFIG2
  SPI.transfer(CONFIG3);  // Write CONFIG3

  digitalWrite(SPI_CS_PIN, HIGH);  // Deselect device

  Serial.println("Wrote config bytes.");
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    ;  // Wait for Serial to be ready
  }
  SPI.begin();
  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);  // Deselect device
  read_config();
  write_config();
  read_config();
  delay(1000);
}

void loop() {
  sendFastCommand(startConversion);
  delay(1000);
  sendFastCommand(checkConversion);
  delay(1000);
  uint32_t result = read_adc();
  Serial.print("ADC Result: ");
  Serial.println(result);
}