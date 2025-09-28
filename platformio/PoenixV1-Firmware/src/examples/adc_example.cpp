#include <Arduino.h>
#include <SPI.h>

#define ADDRESS_BITS = 0b01
#define ADDRESS_ACKNOWLEDGE_BITS = 0b010

#define CONVERSION_START = 0b1010

const int SPI_CS_PIN = 13;  // Chip select pin

byte sendFastCommand(uint8_t dataToSend) {
  digitalWrite(SPI_CS_PIN, LOW);  // Select device
  byte receivedData = SPI.transfer(dataToSend);
  digitalWrite(SPI_CS_PIN, HIGH);  // Deselect device
  Serial.print("Sent: 0b");
  for (int i = 7; i >= 0; --i)
    Serial.print((dataToSend >> i) & 1);
  Serial.print(" | Received: 0b");
  for (int i = 7; i >= 0; --i)
    Serial.print((receivedData >> i) & 1);
  Serial.println();
  return receivedData;
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  pinMode(SPI_CS_PIN, OUTPUT);
  digitalWrite(SPI_CS_PIN, HIGH);  // Deselect device
}

void loop() {
  byte startConversion = 0b01101000;
  byte checkConversion = 0b01000000;
  sendFastCommand(startConversion);
  delay(5000);
  sendFastCommand(checkConversion);
  delay(1000);
}