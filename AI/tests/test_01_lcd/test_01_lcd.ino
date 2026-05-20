/*
 * TEST 01 — LCD Display
 * Teste chaque vue/mode de l'affichage LCD (I2C 0x27, 16x2).
 *
 * Branchements : identiques a final.ino
 *   LCD I2C -> SDA=20, SCL=21
 *   BP1 -> pin 2 (INPUT_PULLUP)
 *   BP2 -> pin 3 (INPUT_PULLUP)
 *
 * Utilisation :
 *   BP1 = changer de page
 *   BP2 = basculer sous-vue (quand applicable)
 *   Serial Monitor (115200 bauds) pour les logs
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte BTN1 = 2;
const byte BTN2 = 3;

byte page = 0;
const byte PAGE_COUNT = 7;

byte sousVue = 0;
unsigned long lastDebounce = 0;
bool lastBtn1 = HIGH, lastBtn2 = HIGH;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== TEST 01 — LCD Display ==="));
  Serial.println(F("BP1=page suivante, BP2=toggle sous-vue\n"));

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);

  lcd.init();
  lcd.backlight();
}

void loop() {
  // debounce buttons
  if (millis() - lastDebounce > 80) {
    bool b1 = digitalRead(BTN1);
    bool b2 = digitalRead(BTN2);

    if (b1 == LOW && lastBtn1 == HIGH) {
      page = (page + 1) % PAGE_COUNT;
      sousVue = 0;
      Serial.print(F("[BTN1] page="));
      Serial.println(page);
    }
    if (b2 == LOW && lastBtn2 == HIGH) {
      sousVue = !sousVue;
      Serial.print(F("[BTN2] toggle sousVue="));
      Serial.println(sousVue);
    }

    lastBtn1 = b1;
    lastBtn2 = b2;
    lastDebounce = millis();
  }

  drawPage();
  delay(100);
}

void drawPage() {
  lcd.clear();
  char buf[17];

  switch (page) {
    case 0:  // page d'accueil
      lcd.setCursor(0, 0); lcd.print(F("PMUL2 Test LCD"));
      lcd.setCursor(0, 1); lcd.print(F("Page 0/6 Accueil"));
      break;

    case 1:  // ecran d'attente (comme etapeActu=0)
      lcd.setCursor(0, 0); lcd.print(F("En attente"));
      if (sousVue) lcd.setCursor(0, 1); lcd.print(F("bloc..."));
      else {
        snprintf(buf, 17, "Total: %u", (uint16_t)millis() / 1000);
        lcd.setCursor(0, 1); lcd.print(buf);
      }
      break;

    case 2:  // scan avec infos couleur (HSV)
      lcd.setCursor(0, 0); lcd.print(F("H128 S200 V255"));
      lcd.setCursor(0, 1); lcd.print(F("T01 ORDER #3"));
      break;

    case 3:  // stock avec infos couleur
      lcd.setCursor(0, 0); lcd.print(F("H64 S180 V220"));
      lcd.setCursor(0, 1); lcd.print(F("T03 STOCK #0"));
      break;

    case 4:  // menu commande - page 0 (selection couleur)
      lcd.setCursor(0, 0); lcd.print(F("Cmd: B2 J5 M1"));
      lcd.setCursor(0, 1); lcd.print(F("[Blue] #=fin"));
      if (sousVue) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("[Magenta]"));
        lcd.setCursor(0, 1); lcd.print(F("  #=fin"));
      }
      break;

    case 5:  // menu commande - page 1 (quantite)
      lcd.setCursor(0, 0); lcd.print(F("Yellow"));
      lcd.setCursor(0, 1); lcd.print(F("Qte: 5 pressez=ok"));
      break;

    case 6:  // resume commande
      lcd.setCursor(0, 0); lcd.print(F("B2 J5 M1"));
      lcd.setCursor(0, 1); lcd.print(F("pressez=envoi"));
      break;
  }
}
