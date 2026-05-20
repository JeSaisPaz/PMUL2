/*
 * TEST 04 — Keypad 4x4
 * Teste chaque touche du clavier matriciel.
 *
 * Branchements (identiques a final.ino) :
 *   Rangees -> pins 22, 23, 24, 25
 *   Colonnes -> pins 26, 27, 28, 29
 *
 * Appuyer sur les touches, le resultat s'affiche sur Serial (115200 bauds).
 * La touche # termine le test et affiche un resume.
 */

#include <Keypad.h>

const byte ROWS = 4;
const byte COLS = 4;

byte rowPins[ROWS] = {22, 23, 24, 25};
byte colPins[COLS] = {26, 27, 28, 29};

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

Keypad keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

bool keysHit[16] = {false};
byte hitCount = 0;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== TEST 04 — Keypad 4x4 ==="));
  Serial.println(F("Appuyez sur toutes les touches."));
  Serial.println(F("Touche '#' = terminer le test.\n"));

  Serial.print(F("Touches detectees: "));
}

void loop() {
  char key = keypad.getKey();
  if (!key) return;

  Serial.print(key);
  Serial.print(' ');

  // marque la touche comme vue
  byte idx;
  if (key >= '0' && key <= '9') idx = key - '0';
  else if (key == '*') idx = 10;
  else if (key == '#') idx = 11;
  else if (key == 'A') idx = 12;
  else if (key == 'B') idx = 13;
  else if (key == 'C') idx = 14;
  else if (key == 'D') idx = 15;
  else return;

  if (!keysHit[idx]) {
    keysHit[idx] = true;
    hitCount++;
  }

  // # termine
  if (key == '#') {
    Serial.println();
    Serial.print(F("Total touches uniques : "));
    Serial.print(hitCount);
    Serial.println(F("/16"));
    Serial.print(F("Manquantes : "));
    afficherManquantes();
    Serial.println(F("\nTest termine."));
    while (1) delay(1000);
  }
}

void afficherManquantes() {
  bool first = true;
  for (byte i = 0; i < 16; i++) {
    if (!keysHit[i]) {
      if (!first) Serial.print(',');
      first = false;
      if (i <= 9) Serial.print((char)('0' + i));
      else if (i == 10) Serial.print('*');
      else if (i == 11) Serial.print('#');
      else Serial.print((char)('A' + i - 12));
    }
  }
  if (first) Serial.print(F("aucune"));
}
