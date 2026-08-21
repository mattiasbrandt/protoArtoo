/**
 * Minimal P4 bringup application — first-flash deliverable for #183.
 * Lives outside src/ (fenced) in bringup/p4_bringup.cpp.
 *
 * Full firmware compilation is blocked by #185 (WiFi seam adaptation).
 * Known blockers in src/:
 * - dome_link.cpp: WiFi API symbols (P4 uses ESP-Hosted over SDIO)
 * - audio_chirp.cpp:294: memset over non-trivial struct (reported as #196)
 *
 * This sketch provides the only definition of setup()/loop() to the linker.
 */

#include <Arduino.h>

// GPIO 3 is LED_BUILTIN on FireBeetle 2 ESP32-P4 (DFR1172)
#define BRINGUP_LED 3

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
