#include "unity_config.h"

#include <unity.h>

#if !defined(UNITY_WEAK_ATTRIBUTE) && !defined(UNITY_WEAK_PRAGMA)
#if defined(__GNUC__) || defined(__ghs__) /* __GNUC__ includes clang */
#if !(defined(__WIN32__) && defined(__clang__)) && !defined(__TMS470__)
#define UNITY_WEAK_ATTRIBUTE __attribute__((weak))
#endif
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef UNITY_WEAK_ATTRIBUTE
UNITY_WEAK_ATTRIBUTE void setUp(void) {
}
UNITY_WEAK_ATTRIBUTE void tearDown(void) {
}
UNITY_WEAK_ATTRIBUTE void suiteSetUp(void) {
}
UNITY_WEAK_ATTRIBUTE int suiteTearDown(int num_failures) {
  return num_failures;
}
#elif defined(UNITY_WEAK_PRAGMA)
#pragma weak setUp
void setUp(void) {
}
#pragma weak tearDown
void tearDown(void) {
}
#pragma weak suiteSetUp
void suiteSetUp(void) {
}
#pragma weak suiteTearDown
int suiteTearDown(int num_failures) {
  return num_failures;
}
#endif

#ifdef __cplusplus
}
#endif /* extern "C" */

#include <Arduino.h>
void unityOutputStart(unsigned long baudrate) {
  // Step 1: Initialize the serial port so Unity test logs can stream to the host.
  Serial.begin(baudrate);
  // Step 2: Wait for the USB serial link to enumerate before proceeding.
  while (!Serial)
    ;
}
void unityOutputChar(unsigned int character) {
  // Step 1: Forward each character emitted by Unity to the serial port.
  Serial.write(character);
}
void unityOutputFlush(void) {
  // Step 1: Ensure buffered serial data is delivered to the host immediately.
  Serial.flush();
}
void unityOutputComplete(void) {
}

void unity_platform_setup_serial(unsigned long baudrate, unsigned long service_delay_ms) {
  // Step 1: Set up the serial port and clear any stale data.
  Serial.begin(baudrate);
  Serial.flush();
  // Step 2: Wait for the host connection before starting the Unity session.
  while (!Serial)
    ;
  // Step 3: Begin the Unity test harness and allow the host to prepare.
  UNITY_BEGIN();
  delay(service_delay_ms);
}
