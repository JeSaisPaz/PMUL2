/*
 * TEST 02 — Capteurs IR
 * Teste les 5 capteurs infrarouges (barriere optique).
 * Affiche l'etat de chaque capteur en temps reel via Serial.
 * Encode aussi le masque binaire comme final.ino (PID_SENSOR_STATUS).
 *
 * Branchements (identiques a final.ino) :
 *   IR_SCAN  -> pin 8  (bloc en position, face actionneur)
 *   IR_NEXT  -> pin 7  (boite derriere)
 *   IR_STOCK -> pin 6  (confirmation stock)
 *   IR_ORDER -> pin 5  (confirmation commande)
 *   IR_PASS  -> pin 4  (confirmation autre)
 *
 * Convention : LOW  = faisceau coupe (bloc present)
 *              HIGH = faisceau libre
 *
 * Serial : 115200 bauds
 */

const byte PINS[] = {8, 7, 6, 5, 4};
const char* NOMS[] = {"SCAN", "NEXT", "STOCK", "ORDER", "PASS"};
const byte NB = 5;

bool etats[5] = {false, false, false, false, false};

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== TEST 02 — Capteurs IR ==="));
  Serial.println(F("LOW=faisceau coupe (bloc), HIGH=libre"));
  Serial.println(F("Affichage uniquement sur changement d'etat.\n"));

  for (byte i = 0; i < NB; i++) {
    pinMode(PINS[i], INPUT_PULLUP);
  }

  // affiche l'etat initial
  for (byte i = 0; i < NB; i++) {
    etats[i] = (digitalRead(PINS[i]) == LOW);
  }
  afficherEtat();
}

void loop() {
  bool changed = false;

  for (byte i = 0; i < NB; i++) {
    bool lecture = (digitalRead(PINS[i]) == LOW);
    if (lecture != etats[i]) {
      etats[i] = lecture;

      Serial.print(F("[IR-"));
      Serial.print(NOMS[i]);
      Serial.print(F("] "));
      Serial.println(etats[i] ? F("ON (bloc detecte)") : F("OFF (libre)"));

      changed = true;
    }
  }

  if (changed) {
    afficherEtat();
  }

  delay(50);
}

void afficherEtat() {
  // affiche le masque (comme final.ino)
  uint8_t mask = (etats[0] ? 0x01 : 0x00)
               | (etats[1] ? 0x02 : 0x00)
               | (etats[2] ? 0x04 : 0x00)
               | (etats[3] ? 0x08 : 0x00)
               | (etats[4] ? 0x10 : 0x00);

  Serial.print(F("Etat: "));
  for (byte i = 0; i < NB; i++) {
    Serial.print(etats[i] ? '1' : '0');
  }
  Serial.print(F(" | mask=0x"));
  if (mask < 0x10) Serial.print('0');
  Serial.println(mask, HEX);
}
