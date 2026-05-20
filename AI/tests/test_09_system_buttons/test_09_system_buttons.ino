/*
 * TEST 09 — Boutons Systeme (BP1, BP2 via interruptions)
 * Teste les boutons poussoir sur interruptions externes.
 *
 * Branchements (identiques a final.ino) :
 *   BP1 -> pin 2 (INPUT_PULLUP, interruption FALLING)
 *   BP2 -> pin 3 (INPUT_PULLUP, interruption FALLING)
 *
 * Comportement (inspire de final.ino) :
 *   BP1 = bascule systemOn/systemOff (toggle)
 *   BP2 = bascule modeAffichage (0/1)
 *
 * Serial (115200 bauds) : log des evenements.
 * LCD I2C 0x27 : affiche l'etat.
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte BTN1 = 2;
const byte BTN2 = 3;

volatile bool systemOn = true;
volatile byte modeAffichage = 0;

void basculeSystem() {
  systemOn = !systemOn;
}

void basculeAffichage() {
  modeAffichage = (modeAffichage + 1) % 2;
}

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== TEST 09 — Boutons Systeme ==="));
  Serial.println(F("BP1 (pin 2) = ON/OFF | BP2 (pin 3) = changer affichage"));
  Serial.println(F("Appuyez sur les boutons.\n"));

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BTN1), basculeSystem, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN2), basculeAffichage, FALLING);

  lcd.init();
  lcd.backlight();
}

void loop() {
  static bool lastSysOn = !systemOn;
  static byte lastMode = 0xFF;

  if (systemOn != lastSysOn) {
    lastSysOn = systemOn;
    Serial.print(F("[SYS] "));
    Serial.println(systemOn ? F("ON") : F("OFF (maintenance)"));
  }

  if (modeAffichage != lastMode) {
    lastMode = modeAffichage;
    Serial.print(F("[AFF] mode "));
    Serial.println(modeAffichage);
  }

  char buf[17];

  if (!systemOn) {
    lcd.setCursor(0, 0); lcd.print(F("MODE:MAINTENANCE"));
    lcd.setCursor(0, 1); lcd.print(F("SYSTEME ARRETE  "));
  } else if (modeAffichage == 1) {
    lcd.setCursor(0, 0); lcd.print(F("Cmd effectuees: "));
    lcd.setCursor(0, 1); lcd.print(F("nb=            "));
  } else {
    lcd.setCursor(0, 0); lcd.print(F("Systeme Actif   "));
    lcd.setCursor(0, 1); lcd.print(F("BP1=OFF BP2=vue"));
  }

  delay(100);
}
