#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "pmul2-lib.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

byte btn1 = 2;
byte btn2 = 3;

volatile bool systemOn = true;
volatile byte modeAffichage = 0;
int totalArticlesTries = 0;

// Com Raspberry Pi via USB
Pmul2Lib objetPmul(Serial);

// 0=Attente, 1=Scan, 2=Aiguillage, 3=Sortie
byte etapeActu = 0;

// Capteurs IR1 a 5
byte pinsIR[] = {8, 7, 6, 5, 4};
bool etatsIR[] = {0, 0, 0, 0, 0};

// Servo Moteur
Servo servoScan;
Servo servoStock;
Servo servoCommande;

unsigned long tempsActuel = 0;
unsigned long tempsDepart = 0;
long attenteServo = 500;

// infos du bloc en cours de scan
uint16_t    currentItemId = 0;
ItemDecision currentDecision = ItemDecision::PASS;
uint8_t     currentOrderId  = 0;

// keypad + encodeur
Pmul2Keypad  keypad;
Pmul2Encoder encoder;

// mode: false = SCAN, true = ORDER (saisie commande)
bool modeOrder = false;

// etat du menu de saisie: 0=choix couleur, 1=qte, 2=resume
byte orderPage = 0;

// couleurs actives envoyees par le backend (max 4, mappees sur A,B,C,D)
uint8_t activeColors[4] = {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA};
uint8_t activeColorCount = 3;

// commande en cours de saisie (max 8 lignes)
uint8_t localColors[8];
uint8_t localQtys[8];
uint8_t localLineCount = 0;

// pour la page 1: index dans activeColors, qte en cours de saisie
uint8_t editingColorIdx = 0;
uint8_t editingQty = 0;
bool editingQtyStarted = false;

void basculeSystem(){
  systemOn = !systemOn;
}

void basculeAffichage(){
  modeAffichage = (modeAffichage+1)%2;
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  // dit au Pi qu'on est pret AVANT lcd.init() qui peut bloquer
  Serial.write('R');
  Serial1.println("Pret.");

  lcd.init();
  lcd.backlight();
  lcd.print("Systeme Pret");

  pinMode(btn1, INPUT_PULLUP);
  pinMode(btn2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(btn1), basculeSystem, FALLING);
  attachInterrupt(digitalPinToInterrupt(btn2), basculeAffichage, FALLING);

  for (byte i = 0; i < 5; i++) {
    pinMode(pinsIR[i], INPUT_PULLUP);
  }

  servoScan.attach(11);
  servoStock.attach(10);
  servoCommande.attach(9);

  servoScan.write(0);
  servoStock.write(0);
  servoCommande.write(0);
}

void loop() {
  updateLCD();

  // diag
  objetPmul.handlePing();

  // recoit les couleurs actives du backend (poll continu)
  {
    uint8_t colors[4];
    uint8_t count;
    if (objetPmul.readColorList(colors, count)) {
      activeColorCount = count;
      for (uint8_t i = 0; i < count; i++) {
        activeColors[i] = colors[i];
      }
    }
  }

  // keypad (toujours actif, meme en SCAN)
  char key = keypad.read();
  if (key) handleKeypad(key);

  // encodeur (toujours actif)
  int8_t encDelta = encoder.readDelta();
  if (encDelta != 0) handleEncoder(encDelta);
  if (encoder.pressed()) handleEncoderButton();

  if (!systemOn) {
    servoScan.write(0);
    servoStock.write(0);
    servoCommande.write(0);
    etapeActu = 0;
    return;
  }

  // en mode ORDER, on bloque le scan
  if (modeOrder) return;

  tempsActuel = millis();

  // Capteurs IR
  for (byte i = 0; i < 5; i++) {
    bool lecture = (digitalRead(pinsIR[i]) == LOW);
    if (lecture != etatsIR[i]) {
      etatsIR[i] = lecture;
      Serial1.print("IR");
      Serial1.print(i + 1);
      Serial1.print(": ");
      Serial1.println(etatsIR[i] ? "1" : "0");
    }
  }

  switch (etapeActu) {

    case 0: // Attente
      if (etatsIR[0]) {
        servoScan.write(90);
        etapeActu = 1;
      }
      break;

    case 1: { // Scan
      static bool scanRequested = false;
      if (!scanRequested) {
        objetPmul.sendScanNeeded();
        scanRequested = true;
      }

      if (objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId)) {
        scanRequested = false;

        switch (currentDecision) {
          case ItemDecision::ORDER:
            servoCommande.write(45);
            break;
          case ItemDecision::STOCK:
            servoStock.write(45);
            break;
          default:
            break;
        }

        tempsDepart = tempsActuel;
        etapeActu = 2;
      }
      break;
    }

    case 2: // Aiguillage
      if ((tempsActuel - tempsDepart >= attenteServo) && (etatsIR[1] == 0)) {
        servoScan.write(0);
        etapeActu = 3;
      }
      break;

    case 3: // Sortie
      if (etatsIR[2] || etatsIR[3] || etatsIR[4]) {
        objetPmul.sendScanResult(currentItemId, ItemStatus::CONFIRMED);
        totalArticlesTries++;

        servoStock.write(0);
        servoCommande.write(0);
        etapeActu = 0;
      }
      break;
  }
}

// clavier

void handleKeypad(char key) {
  if (key == '*') {
    if (!modeOrder) {
      // entre en mode commande
      enterOrderMode();
    } else {
      // confirme l'etape en cours
      confirmOrderStep();
    }
    return;
  }

  if (!modeOrder) return;

  if (key == '#') {
    if (!modeOrder) return;

    if (orderPage == 0) {
      if (localLineCount > 0) {
        // passer au resume de commande
        orderPage = 2;
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
    }
    return;
  }

  if (orderPage == 0 && key >= '0' && key <= '9') {
    // en page 0, les chiffres ne font rien (on utilise l'encodeur)
    return;
  }

  if (orderPage == 0 && key == 'D') {
    // supprime la derniere ligne
    if (localLineCount > 0) localLineCount--;
    return;
  }

  if (orderPage == 1 && key >= '0' && key <= '9') {
    // saisie de la quantite
    uint8_t digit = key - '0';
    if (!editingQtyStarted) {
      editingQty = digit;
      editingQtyStarted = true;
    } else {
      editingQty = editingQty * 10 + digit;
    }
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
}

void handleEncoder(int8_t delta) {
  if (!modeOrder) return;

  if (orderPage == 0) {
    // cycle les couleurs actives
    if (delta > 0) {
      if (editingColorIdx + 1 < activeColorCount) editingColorIdx++;
    } else {
      if (editingColorIdx > 0) editingColorIdx--;
    }
  }
  else if (orderPage == 1) {
    // ajuste la quantite (steps de 1, boucle 0-10)
    int16_t q = (int16_t)editingQty + delta;
    if (q < 0) q = 0;
    if (q > 10) q = 10;
    editingQty = (uint8_t)q;
    editingQtyStarted = true;
  }
}

void handleEncoderButton() {
  if (!modeOrder) return;

  if (orderPage == 0 && activeColorCount > 0) {
    // encoder press = selectionner la couleur
    editingQty = 0;
    editingQtyStarted = false;
    orderPage = 1;
  }
  else if (orderPage == 1) {
    // encoder press = confirmer la quantite (comme *)
    confirmOrderStep();
  }
  else if (orderPage == 2) {
    // encoder press = confirmer envoi (comme *)
    confirmOrderStep();
  }
}

void confirmOrderStep() {
  if (orderPage == 1) {
    if (editingQty > 0 && editingColorIdx < activeColorCount && localLineCount < 8) {
      localColors[localLineCount] = activeColors[editingColorIdx];
      localQtys[localLineCount]   = editingQty;
      localLineCount++;
    }
    orderPage = 0;
    editingColorIdx = 0;
    editingQty = 0;
    editingQtyStarted = false;
    if (localLineCount > 0) {
      // l'utilisateur appuiera sur # pour passer au resume
    }
  }
  else if (orderPage == 2) {
    // confirmation d'envoi
    sendLocalOrder();
    exitOrderMode(true);
  }
}

void sendLocalOrder() {
  if (localLineCount == 0) return;
  objetPmul.sendLocalOrder(localLineCount, localColors, localQtys);
}

void exitOrderMode(bool sent) {
  modeOrder = false;
  orderPage = 0;
  localLineCount = 0;
}

void updateLCD() {
  static unsigned long dernierRefresh = 0;
  if (millis() - dernierRefresh < 250)
    return;
  dernierRefresh = millis();

  lcd.setCursor(0, 0);
  if (!systemOn) {
    lcd.print("MODE:MAINTENANCE");
    lcd.setCursor(0, 1);
    lcd.print("SYSTEME ARRETE");
    return;
  }

  // mode ORDER: menu de saisie
  if (modeOrder) {
    drawOrderMenu();
    return;
  }

  // mode SCAN normal
  if (etapeActu >= 2 && currentDecision == ItemDecision::ORDER) {
    lcd.print("Commande #");
    lcd.print(currentOrderId);
    lcd.setCursor(0, 1);
    lcd.print("Tri en cours...");
  } else {
    lcd.print("Total Tries: ");
    lcd.setCursor(0, 1);
    lcd.print(totalArticlesTries);
    lcd.print(" articles");
  }
}

void drawOrderMenu() {
  lcd.clear();

  if (orderPage == 0) {
    lcd.setCursor(0, 0);
    if (localLineCount == 0) {
      lcd.print("Ajouter ligne?");
    } else {
      lcd.print("Cmd: ");
      for (uint8_t i = 0; i < localLineCount && i < 4; i++) {
        lcd.print(colorNameById(localColors[i])[0]);
        lcd.print(localQtys[i]);
        if (i < localLineCount - 1) lcd.print(",");
      }
    }
    lcd.setCursor(0, 1);
    if (activeColorCount > 0 && editingColorIdx < activeColorCount) {
      lcd.print("[");
      lcd.print(colorNameById(activeColors[editingColorIdx]));
      lcd.print("]");
    }
    if (localLineCount > 0) {
      lcd.print(" #=fin");
    }
  }
  else if (orderPage == 1) {
    lcd.setCursor(0, 0);
    if (editingColorIdx < activeColorCount) {
      lcd.print(colorNameById(activeColors[editingColorIdx]));
    }
    lcd.setCursor(0, 1);
    lcd.print("Qte: ");
    lcd.print(editingQty);
    lcd.print(" pressez=ok");
  }
  else if (orderPage == 2) {
    lcd.setCursor(0, 0);
    uint8_t b = countColor(COLOR_BLUE), j = countColor(COLOR_YELLOW), m = countColor(COLOR_MAGENTA);
    lcd.print("B");
    lcd.print(b);
    lcd.print(" J");
    lcd.print(j);
    lcd.print(" M");
    lcd.print(m);
    lcd.setCursor(0, 1);
    lcd.print("pressez=envoi #=annul");
  }
}

uint8_t countColor(uint8_t colorId) {
  uint8_t total = 0;
  for (uint8_t i = 0; i < localLineCount; i++) {
    if (localColors[i] == colorId) {
      total += localQtys[i];
    }
  }
  return total;
}

// overload keyToColor via pmul2-colors.h keypad->color mapping
// (already defined inline in the header)
