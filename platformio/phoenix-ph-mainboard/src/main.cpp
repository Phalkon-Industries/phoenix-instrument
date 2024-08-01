#include <Arduino.h>
#include "Adafruit_TinyUSB.h"
#include "cli.hpp"
#include "logging.hpp"
#include "file-readwrite.hpp"

void setup()
{
  Serial.begin(115200);
  while (!Serial)
    ;
  delay(5000);
  Serial.println("Enter error (or 'help' for list, 'exit' to quit): ");
  initTime();
  initSD();
}

void loop()
{

}