/**
 * Minimal P4 bringup application — first-flash deliverable for #183.
 * Lives outside src/ (fenced) in bringup/p4_bringup.cpp.
 *
 * Full firmware adaptation to the FireBeetle 2's C6/ESP-Hosted WiFi path is
 * tracked in #188/#189.
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

// Weak symbols to coexist with .dummy/sketch.cpp.o in the library pass
__attribute__((weak))
void setup() {
  // Serial is the native USB CDC endpoint, which only exists once the host has
  // enumerated and opened the port. Start it before the blinks so enumeration
  // overlaps them, then allow a short settle before the first write - output
  // sent to a not-yet-opened CDC endpoint is discarded, not buffered.
  Serial.begin(115200);

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);

  // Three blinks to signal boot
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);
    digitalWrite(LED_BUILTIN, LOW);
    delay(200);
  }
#else
  // The generic custom_sdkconfig library pass may omit board-variant symbols.
  // Keep that pass buildable without inventing a numeric fallback.
  Serial.println("protoArtoo P4 bringup: LED_BUILTIN unavailable; blink disabled.");
#endif

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
#if defined(LED_BUILTIN)
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1000);
  digitalWrite(LED_BUILTIN, LOW);
  delay(4000);
#else
  delay(5000);
#endif

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
