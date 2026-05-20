/*
 * TEST 05 — Encodeur Rotatoire + Bouton
 * Teste la rotation et le bouton poussoir de l'encodeur.
 *
 * Branchements (identiques a final.ino) :
 *   CLK -> pin 22 (interruption)
 *   DT  -> pin 23
 *   SW  -> pin 24 (bouton poussoir)
 *
 * Tourner l'encodeur : affiche le delta et la position cumulee.
 * Appuyer sur le bouton : affiche PRESS/RELEASE.
 *
 * Serial : 115200 bauds
 */

#include "pmul2-lib.h"

Pmul2Encoder encoder;

int16_t position = 0;
bool lastBtn = false;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== TEST 05 — Encodeur Rotatoire ==="));
  Serial.println(F("Tournez pour changer la position."));
  Serial.println(F("Appuyez sur le bouton de l'encodeur.\n"));
}

void loop() {
  // rotation
  int8_t delta = encoder.readDelta();
  if (delta != 0) {
    position += delta;
    Serial.print(F("Delta="));
    Serial.print(delta > 0 ? F("+") : F(""));
    Serial.print(delta);
    Serial.print(F(" | Position="));
    Serial.println(position);
  }

  // bouton
  bool pressed = encoder.pressed();
  if (pressed) {
    Serial.println(F("[BTN] PRESS (front descendant)"));
  }

  delay(1);
}
