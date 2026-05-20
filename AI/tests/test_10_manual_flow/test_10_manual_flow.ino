/*
 * TEST 10 — Flux Manuel Complet (simulation sans Pi)
 * Reprend la machine d'etat complete de final.ino avec simulation
 * des capteurs IR et du backend via le Serial Monitor.
 *
 * Tous les branchements materiels sont identiques a final.ino,
 * mais le Pi est simule par des commandes Serial (USB).
 *
 * Branchements :
 *   Capteurs IR -> pins 8,7,6,5,4
 *   Servos       -> pins 11,10,9
 *   BP1/BP2      -> pins 2,3
 *   LCD I2C      -> 0x27
 *   Keypad       -> rows 22-25, cols 26-29
 *   Encodeur     -> pins 22,23,24 (CLK,DT,SW)
 *
 * Commandes Serial (115200 bauds) pour simuler le backend :
 *   1/2/3  = repondre PASS/ORDER/STOCK au prochain scan
 *   c      = envoyer COLOR_LIST (Bleu, Jaune, Magenta)
 *   m      = envoyer COMPLETED_COUNT (incremente)
 *   s      = envoyer PING
 *   r      = reset etat
 *
 * Les capteurs IR sont lus sur le materiel reel.
 * Le Serial Monitor affiche chaque transition d'etat.
 */

#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "pmul2-lib.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);
Pmul2Lib objetPmul(Serial);

byte btn1 = 2, btn2 = 3;
volatile bool systemOn = true;
volatile byte modeAffichage = 0;

byte pinsIR[] = {8, 7, 6, 5, 4};
bool etatsIR[] = {0, 0, 0, 0, 0};

#define IR_SCAN  0
#define IR_NEXT  1
#define IR_STOCK 2
#define IR_ORDER 3
#define IR_PASS  4

Servo servoScan, servoStock, servoCommande;

byte etapeActu = 0;
unsigned long tempsDepart = 0;
long attenteServo = 500;

uint16_t currentItemId = 0;
uint8_t currentDecision = 0;  // 0=PASS, 1=ORDER, 2=STOCK
uint8_t currentOrderId = 0;
uint8_t currentHue = 0, currentSaturation = 0, currentValue = 0, currentTeam = 0;
int totalArticlesTries = 0;
uint16_t completedOrders = 0;

Pmul2Keypad  keypad;
Pmul2Encoder encoder;
bool modeOrder = false;

// simulation backend
unsigned long dernierScan = 0;
bool scanRequested = false;
byte scanRetries = 0;
bool freshScan = true;
bool reponsePrete = false;
uint8_t decisionSimulee = 0;
uint8_t idSimule = 0;

void basculeSystem() { systemOn = !systemOn; }
void basculeAffichage() { modeAffichage = (modeAffichage + 1) % 2; }

void setup() {
  Serial1.begin(115200);
  Serial1.println(F("\n=== TEST 10 — Flux Manuel Complet ==="));
  Serial1.println(F("Commandes: 1=PASS 2=ORDER 3=STOCK c=colors m=completed s=ping r=reset"));
  Serial1.println(F("Keypad * pour entrer en mode commande"));

  for (byte i = 0; i < 5; i++) pinMode(pinsIR[i], INPUT_PULLUP);
  pinMode(btn1, INPUT_PULLUP);
  pinMode(btn2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(btn1), basculeSystem, FALLING);
  attachInterrupt(digitalPinToInterrupt(btn2), basculeAffichage, FALLING);

  servoScan.attach(11);   servoScan.write(0);
  servoStock.attach(10);  servoStock.write(45);
  servoCommande.attach(9); servoCommande.write(45);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print(F("Test Flux Manuel"));
  lcd.setCursor(0, 1); lcd.print(F("Pret."));
}

void loop() {
  // lecture commandes Serial
  while (Serial1.available()) {
    char c = Serial1.read();
    switch (c) {
      case '1': decisionSimulee = 0; reponsePrete = true;
                currentHue=128; currentSaturation=200; currentValue=255; currentTeam=1;
                idSimule++; Serial1.println(F("-> PASS pret")); break;
      case '2': decisionSimulee = 1; reponsePrete = true;
                currentHue=64; currentSaturation=180; currentValue=220; currentTeam=3;
                currentOrderId=5; idSimule++; Serial1.println(F("-> ORDER pret")); break;
      case '3': decisionSimulee = 2; reponsePrete = true;
                currentHue=32; currentSaturation=160; currentValue=200; currentTeam=2;
                idSimule++; Serial1.println(F("-> STOCK pret")); break;
      case 'c':
        Serial1.println(F("-> COLOR_LIST simule (B,J,M)"));
        break;
      case 'm':
        completedOrders++;
        Serial1.print(F("-> COMPLETED_COUNT="));
        Serial1.println(completedOrders);
        break;
      case 's':
        Serial1.println(F("-> PING simule"));
        break;
      case 'r':
        etapeActu = 0; reponsePrete = false; scanRequested = false; freshScan = true;
        scanRetries = 0;
        servoScan.write(0); servoStock.write(45); servoCommande.write(45);
        Serial1.println(F("-> RESET"));
        break;
    }
  }

  updateLCD();

  // log etape
  {
    static byte lastEtape = 0xFF;
    if (etapeActu != lastEtape) {
      const char* noms[] = {"Attente", "Scan", "Liberation", "Confirmation"};
      Serial1.print(F("[ETAPE] "));
      Serial1.println(noms[etapeActu]);
      lastEtape = etapeActu;
    }
  }

  // keypad (toujours actif)
  char key = keypad.read();
  if (key) handleKeypad(key);

  if (!systemOn) {
    servoScan.write(0); servoStock.write(45); servoCommande.write(45);
    etapeActu = 0;
    return;
  }
  if (modeOrder) return;

  unsigned long tempsActuel = millis();

  // capteurs IR
  for (byte i = 0; i < 5; i++) {
    bool lecture = (digitalRead(pinsIR[i]) == LOW);
    if (lecture != etatsIR[i]) {
      etatsIR[i] = lecture;
      const char* noms[] = {"SCAN", "NEXT", "STOCK", "ORDER", "PASS"};
      Serial1.print(F("[IR-"));
      Serial1.print(noms[i]);
      Serial1.print(F("] "));
      Serial1.println(etatsIR[i] ? F("ON") : F("OFF"));
    }
  }

  switch (etapeActu) {

    case 0: // Attente
      if (etatsIR[IR_SCAN]) {
        servoScan.write(10);
        etapeActu = 1;
        freshScan = true;
        scanRequested = false;
        scanRetries = 0;
        Serial1.println(F("[SCAN] bloc bloque"));
      }
      break;

    case 1: // Scan
      if (freshScan) {
        scanRetries = 0;
        scanRequested = false;
        freshScan = false;
      }

      if (reponsePrete) {
        currentItemId = idSimule;
        currentDecision = decisionSimulee;
        reponsePrete = false;
        freshScan = true;

        Serial1.print(F("[ITEM] #"));
        Serial1.print(currentItemId);
        Serial1.print(F(" dec="));
        Serial1.println(currentDecision == 1 ? "ORDER" : (currentDecision == 2 ? "STOCK" : "PASS"));

        if (currentDecision == 1) servoCommande.write(0);
        else if (currentDecision == 2) servoStock.write(0);

        tempsDepart = tempsActuel;
        etapeActu = 2;
      } else if (!scanRequested && scanRetries < 3) {
        scanRequested = true;
        tempsDepart = tempsActuel;
        Serial1.print(F("[SCAN] demande info"));
        if (scanRetries > 0) { Serial1.print(F(" (retry ")); Serial1.print(scanRetries); Serial1.print(')'); }
        Serial1.println();
      } else if (scanRequested && tempsActuel - tempsDepart > 2000) {
        scanRequested = false;
        scanRetries++;
        if (scanRetries >= 3) {
          currentDecision = 0;
          Serial1.println(F("[SCAN] 3 essais echoues -> PASS"));
          freshScan = true;
          tempsDepart = tempsActuel;
          etapeActu = 2;
        }
      }
      break;

    case 2: { // Liberation
      static unsigned long tempsRelache = 0;
      if (tempsActuel - tempsDepart >= (unsigned long)attenteServo) {
        servoScan.write(0);
        if (tempsRelache == 0) tempsRelache = tempsActuel;

        if (etatsIR[IR_SCAN] == 0) {
          tempsRelache = 0;
          tempsDepart = tempsActuel;
          etapeActu = 3;
          Serial1.println(F("[RELACHE] bloc parti"));
        } else if (tempsActuel - tempsRelache > 3000) {
          Serial1.println(F("[WARN] bloc coince, retour attente"));
          tempsRelache = 0;
          etapeActu = 0;
        }
      }
      break;
    }

    case 3: // Confirmation
      if (tempsActuel - tempsDepart > 5000) {
        Serial1.println(F("[WARN] timeout confirmation"));
        servoStock.write(45);
        servoCommande.write(45);
        etapeActu = 0;
        break;
      }

      {
        bool confirmed = false;
        switch (currentDecision) {
          case 1: confirmed = etatsIR[IR_ORDER]; break;
          case 2: confirmed = etatsIR[IR_STOCK]; break;
          default: confirmed = etatsIR[IR_PASS]; break;
        }

        if (confirmed) {
          totalArticlesTries++;
          Serial1.print(F("[SORTIE] item "));
          Serial1.print(currentItemId);
          Serial1.print(F(" tries="));
          Serial1.println(totalArticlesTries);
          servoStock.write(45);
          servoCommande.write(45);
          etapeActu = 0;
        }
      }
      break;
  }
}

// --- LCD ---

void updateLCD() {
  static unsigned long dernierRefresh = 0;
  if (millis() - dernierRefresh < 250) return;
  dernierRefresh = millis();

  if (!systemOn) {
    lcdWriteLine(0, "MODE:MAINTENANCE");
    lcdWriteLine(1, "SYSTEME ARRETE  ");
    return;
  }

  if (modeAffichage == 1) {
    lcdWriteLine(0, "Cmd effectuees: ");
    char buf[17]; snprintf(buf, 17, "%u", completedOrders);
    lcdWriteLine(1, buf);
    return;
  }

  if (etapeActu >= 2 && (currentDecision == 1 || currentDecision == 2)) {
    char buf[17];
    snprintf(buf, 17, "H%u S%u V%u", currentHue, currentSaturation, currentValue);
    lcdWriteLine(0, buf);
    snprintf(buf, 17, "T%u %s #%u", currentTeam,
             currentDecision == 1 ? "ORDER" : "STOCK", currentOrderId);
    lcdWriteLine(1, buf);
  } else {
    char buf[17];
    snprintf(buf, 17, "Total Tries: %u", totalArticlesTries);
    lcdWriteLine(0, buf);
    lcdWriteLine(1, "Attente bloc... ");
  }
}

static void lcdWriteLine(uint8_t line, const char* text) {
  lcd.setCursor(0, line);
  uint8_t len = 0;
  while (len < 16 && text[len]) { lcd.print(text[len]); len++; }
  while (len < 16) { lcd.print(' '); len++; }
}

// --- Keypad simplifie ---

void handleKeypad(char key) {
  if (key == '*') {
    modeOrder = !modeOrder;
    Serial1.println(modeOrder ? F("[ORDER] mode ON") : F("[ORDER] mode OFF"));
  }
}
