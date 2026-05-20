/*
 * TEST 08 — Mode Saisie de Commande (Order Mode)
 * Teste le workflow complet de saisie d'une commande via keypad + encodeur.
 * Reprend la logique exacte de final.ino pour handleKeypad, handleEncoder
 * et sendLocalOrder.
 *
 * Necessite la librairie pmul2-lib.
 *
 * Branchements (identiques a final.ino) :
 *   Keypad  -> rows=22-25, cols=26-29
 *   Encodeur -> CLK=22 (clavier retarde), DT=23, SW=24
 *   ATTENTION : conflit pin 22 utilisee par le keypad ET l'encodeur.
 *   Ce test utilise les memes pins que final.ino. Sur l'Arduino Mega,
 *   le keypad utilise 22-29 et l'encodeur 22-24. Les pins 22,23,24
 *   sont partagees (le keypad les utilise en sortie, l'encodeur en entree).
 *   Adaptation: l'encodeur utilise 30(CLK) 31(DT) 32(SW) pour ce test.
 *
 * Serial1 (115200) pour les logs.
 * LCD I2C 0x27 pour l'affichage du menu.
 *
 * Workflow :
 *   1) Appuyer sur * pour entrer en mode commande
 *   2) Tourner l'encodeur pour choisir une couleur
 *   3) Appuyer sur le bouton de l'encodeur pour passer a la quantite
 *   4) Tourner l'encodeur pour ajuster la quantite, ou taper des chiffres
 *   5) Appuyer sur * pour confirmer la ligne
 *   6) Repeter 2-5 pour ajouter d'autres lignes
 *   7) Appuyer sur # pour voir le resume
 *   8) Appuyer sur * pour envoyer la commande
 *   D = supprimer derniere ligne
 *   # (en page 0) = aller au resume
 *   # (en page 2) = annuler
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "pmul2-lib.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);
Pmul2Lib objetPmul(Serial);

Pmul2Encoder encoder;
Pmul2Keypad  keypad;

bool modeOrder = false;
byte orderPage = 0;
byte editingColorIdx = 0;
uint8_t editingQty = 0;
bool editingQtyStarted = false;

uint8_t activeColors[4] = {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA};
uint8_t activeColorCount = 3;
uint8_t localColors[8], localQtys[8];
uint8_t localLineCount = 0;

void setup() {
  Serial1.begin(115200);
  Serial1.println(F("\n=== TEST 08 — Mode Saisie Commande ==="));
  Serial1.println(F("*=entrer/confirmer  #=fin/resume/annuler  D=supprimer"));
  Serial1.println(F("Encodeur=selection/quantite  Bouton encodeur=valider"));

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(F("Test Order Mode"));
  lcd.setCursor(0, 1);
  lcd.print(F("* pour demarrer"));
}

void loop() {
  char key = keypad.read();
  if (key) handleKeypad(key);

  int8_t encDelta = encoder.readDelta();
  if (encDelta != 0) handleEncoder(encDelta);
  if (encoder.pressed()) handleEncoderButton();

  if (modeOrder) updateLCD();
  delay(30);
}

// --- logique keypad (copiee de final.ino, simplifiee) ---

void handleKeypad(char key) {
  Serial1.print(F("[KEY] '"));
  Serial1.print(key);
  Serial1.print(F("' page="));
  Serial1.print(orderPage);
  Serial1.print(F(" mode="));
  Serial1.println(modeOrder);

  if (key == '*') {
    if (!modeOrder) {
      enterOrderMode();
    } else {
      confirmOrderStep();
    }
    return;
  }

  if (!modeOrder) return;

  if (key == '#') {
    if (orderPage == 0) {
      if (localLineCount > 0) {
        orderPage = 2;
        Serial1.println(F("[ORDER] -> resume"));
      } else {
        exitOrderMode(false);
      }
    } else if (orderPage == 2) {
      exitOrderMode(false);
    } else {
      orderPage = 0;
      editingColorIdx = 0;
      editingQty = 0;
      editingQtyStarted = false;
      Serial1.println(F("[ORDER] retour page couleur"));
    }
    return;
  }

  if (orderPage == 0 && key == 'D') {
    if (localLineCount > 0) {
      localLineCount--;
      Serial1.print(F("[ORDER] ligne supprimee, reste "));
      Serial1.println(localLineCount);
    }
    return;
  }

  if (orderPage == 1 && key >= '0' && key <= '9') {
    uint8_t digit = key - '0';
    if (!editingQtyStarted) {
      editingQty = digit;
      editingQtyStarted = true;
    } else {
      uint16_t tmp = editingQty * 10 + digit;
      editingQty = (tmp > 10) ? 10 : (uint8_t)tmp;
    }
    Serial1.print(F("[ORDER] qte="));
    Serial1.println(editingQty);
    return;
  }
}

void enterOrderMode() {
  modeOrder = true;
  orderPage = 0;
  localLineCount = 0;
  editingColorIdx = 0;
  editingQty = 0;
  editingQtyStarted = false;
  Serial1.println(F("[ORDER] mode saisie actif"));
}

void handleEncoder(int8_t delta) {
  if (!modeOrder) return;

  if (orderPage == 0) {
    if (delta > 0 && editingColorIdx + 1 < activeColorCount) editingColorIdx++;
    else if (delta < 0 && editingColorIdx > 0) editingColorIdx--;
    Serial1.print(F("[ENC] couleur idx="));
    Serial1.println(editingColorIdx);
  }
  else if (orderPage == 1) {
    int16_t q = (int16_t)editingQty + delta;
    if (q < 0) q = 0;
    if (q > 10) q = 10;
    editingQty = (uint8_t)q;
    editingQtyStarted = true;
    Serial1.print(F("[ENC] qte="));
    Serial1.println(editingQty);
  }
}

void handleEncoderButton() {
  if (!modeOrder) return;
  Serial1.println(F("[ENC_BTN] press"));

  if (orderPage == 0 && activeColorCount > 0) {
    editingQty = 0;
    editingQtyStarted = false;
    orderPage = 1;
    Serial1.println(F("[ORDER] -> page qte"));
  }
  else if (orderPage == 1) {
    confirmOrderStep();
  }
  else if (orderPage == 2) {
    confirmOrderStep();
  }
}

void confirmOrderStep() {
  if (orderPage == 1) {
    if (editingQty > 0 && editingColorIdx < activeColorCount && localLineCount < 8) {
      localColors[localLineCount] = activeColors[editingColorIdx];
      localQtys[localLineCount]   = editingQty;
      localLineCount++;
      Serial1.print(F("[ORDER] ligne ajoutee: couleur="));
      Serial1.print(activeColors[editingColorIdx], HEX);
      Serial1.print(F(" qte="));
      Serial1.println(editingQty);
    }
    orderPage = 0;
    editingColorIdx = 0;
    editingQty = 0;
    editingQtyStarted = false;
  }
  else if (orderPage == 2) {
    sendLocalOrder();
    exitOrderMode(true);
  }
}

void sendLocalOrder() {
  if (localLineCount == 0) return;
  objetPmul.sendLocalOrder(localLineCount, localColors, localQtys);
  Serial1.print(F("[ORDER] envoye: "));
  Serial1.print(localLineCount);
  Serial1.print(F(" lignes ["));
  for (uint8_t i = 0; i < localLineCount; i++) {
    if (i > 0) Serial1.print(',');
    Serial1.print(localQtys[i]);
    Serial1.print('x');
    Serial1.print(localColors[i], HEX);
  }
  Serial1.println(']');
}

void exitOrderMode(bool sent) {
  modeOrder = false;
  orderPage = 0;
  localLineCount = 0;
  Serial1.println(sent ? F("[ORDER] commande envoyee") : F("[ORDER] annule"));
}

// --- LCD ---

void updateLCD() {
  static unsigned long dernierRefresh = 0;
  if (millis() - dernierRefresh < 250) return;
  dernierRefresh = millis();

  char buf[17];

  if (orderPage == 0) {
    if (localLineCount == 0) {
      lcd.setCursor(0, 0); lcd.print(F("Ajouter ligne? "));
    } else {
      byte pos = snprintf(buf, 17, "Cmd: ");
      for (uint8_t i = 0; i < localLineCount && pos < 16; i++) {
        buf[pos++] = colorNameById(localColors[i])[0];
        if (localQtys[i] >= 10 && pos < 16) buf[pos++] = '0' + (localQtys[i] / 10);
        if (pos < 16) buf[pos++] = '0' + (localQtys[i] % 10);
        if (i < localLineCount - 1 && pos < 16) buf[pos++] = ',';
      }
      buf[pos] = '\0';
      lcd.setCursor(0, 0); lcd.print(buf);
    }

    lcd.setCursor(0, 1);
    if (activeColorCount > 0 && editingColorIdx < activeColorCount) {
      snprintf(buf, 17, "[%s]", colorNameById(activeColors[editingColorIdx]));
    } else {
      snprintf(buf, 17, "[-]");
    }
    if (localLineCount > 0) {
      byte len = strlen(buf);
      snprintf(buf + len, 17 - len, " #=fin");
    }
    lcd.print(buf);
  }
  else if (orderPage == 1) {
    lcd.setCursor(0, 0);
    if (editingColorIdx < activeColorCount) {
      lcd.print(colorNameById(activeColors[editingColorIdx]));
    }
    lcd.setCursor(0, 1);
    snprintf(buf, 17, "Qte: %u  *=ok", editingQty);
    lcd.print(buf);
  }
  else if (orderPage == 2) {
    uint8_t b = countColor(COLOR_BLUE), j = countColor(COLOR_YELLOW), m = countColor(COLOR_MAGENTA);
    snprintf(buf, 17, "B%u J%u M%u", b, j, m);
    lcd.setCursor(0, 0); lcd.print(buf);
    lcd.setCursor(0, 1); lcd.print(F("*=envoi #=annul"));
  }
}

uint8_t countColor(uint8_t colorId) {
  uint8_t total = 0;
  for (uint8_t i = 0; i < localLineCount; i++) {
    if (localColors[i] == colorId) total += localQtys[i];
  }
  return total;
}
