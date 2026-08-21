/**
 * Minimal P4 bringup application — first-flash deliverable for #183.
 * Lives outside src/ (fenced) in bringup/p4_bringup.cpp.
 *
 * Full firmware adaptation to P4's ESP-Hosted WiFi path is tracked in #188.
 * This minimal bringup does not build the full firmware — it serves as the
 * first-flash image to establish the toolchain and gate platform readiness.
 *
 * Uses weak symbols to avoid linker collision when .dummy/sketch.cpp.o is
 * compiled as part of the custom_sdkconfig library pass. The weak attributes
 * allow the linker to coexist with .dummy's copy without error; which copy
 * reaches the final image depends on link order and object archive contents.
 * Empirically (verified by nm/addr2line on the shipped firmware.bin), the
 * bringup source's setup()/loop() reach the final image.
 */

#include <Arduino.h>

// GPIO 3 is LED_BUILTIN on FireBeetle 2 ESP32-P4 (DFR1172)
#define BRINGUP_LED 3

// Weak symbols to coexist with .dummy/sketch.cpp.o in the library pass
__attribute__((weak))
void setup() {
  pinMode(BRINGUP_LED, OUTPUT);

  // Three blinks to signal boot
  for (int i = 0; i < 3; i++) {
    digitalWrite(BRINGUP_LED, HIGH);
    delay(200);
    digitalWrite(BRINGUP_LED, LOW);
    delay(200);
  }

  Serial.println("protoArtoo P4 bringup initialized.");
  Serial.print("Chip: ");
  Serial.println(ESP.getChipModel());
  Serial.print("Revision: ");
  Serial.println(ESP.getChipRevision());
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
}

__attribute__((weak))
void loop() {
  digitalWrite(BRINGUP_LED, HIGH);
  delay(1000);
  digitalWrite(BRINGUP_LED, LOW);
  delay(4000);

  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 30000) {
    Serial.print("Uptime: ");
    Serial.print(millis() / 1000);
    Serial.print("s, Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");
    lastPrint = millis();
  }
}
