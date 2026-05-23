/*
 * TEST COMMUNICATION + SCAN + API
 * =================================
 * Utilise exactement le même câblage que final.ino :
 *   - LCD I2C  : 0x27, 16x2
 *   - Keypad   : rows=A3,A2,A1,A0 / cols=A7,A6,13,12
 *   - Serial   : communication avec le Pi (9600 baud)
 *
 * TOUCHES DU KEYPAD :
 *   1 → Simuler détection bloc  (sendScanNeeded)
 *   2 → Confirmer CONFIRMED     (après avoir reçu une réponse)
 *   3 → Confirmer FAILED        (après avoir reçu une réponse)
 *   4 → Envoyer état capteurs IR (tous actifs)
 *   A → Afficher dernier item reçu sur LCD
 *   B → Réinitialiser / revenir à l'écran d'accueil
 *   # → Ping
 *
 * LCD :
 *   Ligne 0 : état courant du test
 *   Ligne 1 : détail (item ID, décision, erreur...)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "pmul2-lib.h"

// ── Périphériques ──────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

const uint8_t ROWS = 4;
const uint8_t COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
uint8_t rowPins[ROWS] = {31, 33, 35, 37};
uint8_t colPins[COLS] = {39, 41, 43, 45};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ── Communication Pi ────────────────────────────────────────
Pmul2Lib com(Serial);

// ── Données du dernier item reçu ───────────────────────────
uint16_t     lastItemId   = 0;
ItemDecision lastDecision = ItemDecision::NO_DECISION;
uint8_t      lastOrderId  = 0;
uint8_t      lastHue      = 0;
uint8_t      lastSat      = 0;
uint8_t      lastVal      = 0;
uint8_t      lastTeam     = 0;

// ── État du test ────────────────────────────────────────────
enum TestState {
  TS_IDLE,       // Écran d'accueil, attente touche
  TS_WAIT_REPLY, // sendScanNeeded envoyé, on attend PID_ITEM_INFO
  TS_GOT_REPLY,  // Réponse reçue, attente confirmation (2 ou 3)
};
TestState testState = TS_IDLE;

unsigned long scanSentAt       = 0;
const unsigned long REPLY_TIMEOUT = 7000; // 7s (Pi scan + backend)

uint16_t scanCount = 0; // Compteur de blocs simulés

// ── Helpers LCD ─────────────────────────────────────────────
void lcdPrint(const char* line0, const char* line1 = "") {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

void lcdPrint(const char* line0, String line1) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(line0);
  lcd.setCursor(0, 1); lcd.print(line1);
}

// Traduit ItemDecision en texte court pour le LCD
const char* decisionLabel(ItemDecision d) {
  switch(d) {
    case ItemDecision::ORDER: return "ORDER";
    case ItemDecision::STOCK: return "STOCK";
    case ItemDecision::PASS:  return "PASS";
    default:                  return "???";
  }
}

// ── Setup ───────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
  Serial.write('R'); // Signal "Arduino prêt" pour le Pi

  lcd.init();
  lcd.backlight();
  lcdPrint("TEST COM/SCAN", "1:Scan A:Info");
}

// ── Loop ────────────────────────────────────────────────────
void loop() {

  // ── 1. Lecture keypad ─────────────────────────────────────
  char key = keypad.getKey();

  if (key) {
    switch(key) {

      // ── Touche 1 : simuler un bloc détecté ─────────────────
      case '1':
        if (testState == TS_WAIT_REPLY) {
          lcdPrint("Deja en attente", "Patiente...");
          break;
        }
        scanCount++;
        testState  = TS_WAIT_REPLY;
        scanSentAt = millis();
        lastItemId = 0;
        lastDecision = ItemDecision::NO_DECISION;

        com.sendScanNeeded();

        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Scan #");
        lcd.print(scanCount);
        lcd.print(" envoye");
        lcd.setCursor(0, 1); lcd.print("Attente Pi...");
        break;

      // ── Touche 2 : confirmer CONFIRMED ─────────────────────
      case '2':
        if (testState != TS_GOT_REPLY || lastItemId == 0) {
          lcdPrint("Pas d'item actif", "Fais 1 d'abord");
          break;
        }
        com.sendScanResult(lastItemId, ItemStatus::CONFIRMED);
        lcdPrint("CONFIRMED envoye", ("Item #" + String(lastItemId)).c_str());
        testState  = TS_IDLE;
        lastItemId = 0;
        break;

      // ── Touche 3 : confirmer FAILED ────────────────────────
      case '3':
        if (testState != TS_GOT_REPLY || lastItemId == 0) {
          lcdPrint("Pas d'item actif", "Fais 1 d'abord");
          break;
        }
        com.sendScanResult(lastItemId, ItemStatus::FAILED);
        lcdPrint("FAILED envoye", ("Item #" + String(lastItemId)).c_str());
        testState  = TS_IDLE;
        lastItemId = 0;
        break;

      // ── Touche 4 : envoyer état capteurs IR ────────────────
      case '4':
        com.sendSensorStatus(1, 1, 1, 1, 1);
        lcdPrint("IR envoye", "Tous actifs");
        delay(800);
        com.sendSensorStatus(0, 0, 0, 0, 0);
        lcdPrint("IR envoye", "Tous inactifs");
        break;

      // ── Touche A : afficher les infos du dernier item ───────
      case 'A':
        if (lastItemId == 0) {
          lcdPrint("Aucun item recu", "Lance un scan");
        } else {
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("#"); lcd.print(lastItemId);
          lcd.print(" "); lcd.print(decisionLabel(lastDecision));
          lcd.print(" O:"); lcd.print(lastOrderId);
          lcd.setCursor(0, 1);
          lcd.print("H:"); lcd.print(lastHue);
          lcd.print(" S:"); lcd.print(lastSat);
          lcd.print(" V:"); lcd.print(lastVal);
        }
        break;

      // ── Touche B : retour accueil ───────────────────────────
      case 'B':
        testState = TS_IDLE;
        lastItemId = 0;
        lcdPrint("TEST COM/SCAN", "1:Scan A:Info");
        break;

      // ── Touche # : ping ─────────────────────────────────────
      case '#':
        com.sendPong();
        lcdPrint("Ping envoye", "Attente pong...");
        break;
    }
  }

  // ── 2. Réception réponse Pi -> Arduino (PID_ITEM_INFO) ────
  if (testState == TS_WAIT_REPLY) {
    uint16_t     itemId;
    ItemDecision decision;
    uint8_t      orderId, hue, sat, val, team;

    if (com.readItemInfo(itemId, decision, orderId, hue, sat, val, team)) {
      // Réponse reçue !
      lastItemId   = itemId;
      lastDecision = decision;
      lastOrderId  = orderId;
      lastHue      = hue;
      lastSat      = sat;
      lastVal      = val;
      lastTeam     = team;
      testState    = TS_GOT_REPLY;

      // Affichage résultat sur LCD
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("#"); lcd.print(itemId);
      lcd.print(" "); lcd.print(decisionLabel(decision));
      if (decision == ItemDecision::ORDER) {
        lcd.print(" Ord:"); lcd.print(orderId);
      }
      lcd.setCursor(0, 1);
      lcd.print("2:OK 3:FAIL");
    }

    // Timeout
    if (millis() - scanSentAt > REPLY_TIMEOUT) {
      testState = TS_IDLE;
      lcdPrint("TIMEOUT!", "Pi pas repondu");
    }
  }

  // ── 3. Ping auto (répond si le Pi envoie un ping) ────────
  if (com.handlePing()) {
    // Pas d'affichage pour ne pas perturber l'écran en cours
  }

  delay(10);
}
