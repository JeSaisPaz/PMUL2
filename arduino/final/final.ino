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

// keypad
Pmul2Keypad keypad;

// mode: false = SCAN, true = ORDER (saisie commande)
bool modeOrder = false;

// etat du menu de saisie de commande
// 0 = ecran d'accueil commande (choisir couleur ou finir)
// 1 = choix de la quantite pour la couleur selectionnee
// 2 = resume / confirmation d'envoi
byte orderPage = 0;

// commande en cours de saisie (max 8 lignes)
uint8_t localColors[8];
uint8_t localQtys[8];
uint8_t localLineCount = 0;

// pour la page 1: couleur en cours, qte en cours de saisie
KeypadColor editingColor = KeypadColor::NONE;
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

  Serial.write('R');
  Serial1.println("Pret.");
}

void loop() {
  updateLCD();

  // diag
  objetPmul.handlePing();

  // keypad (toujours actif, meme en SCAN)
  char key = keypad.read();
  if (key) handleKeypad(key);

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
      editingColor = KeypadColor::NONE;
      editingQty = 0;
      editingQtyStarted = false;
    }
    return;
  }

  if (orderPage == 0 && (key == 'A' || key == 'B' || key == 'C')) {
    // choix de la couleur
    editingColor = keyToColor(key);
    editingQty = 0;
    editingQtyStarted = false;
    orderPage = 1;
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
  editingColor = KeypadColor::NONE;
  editingQty = 0;
  editingQtyStarted = false;
}

void confirmOrderStep() {
  if (orderPage == 1) {
    // on a fini de saisir la qte, on ajoute la ligne
    if (editingQty > 0 && editingColor != KeypadColor::NONE && localLineCount < 8) {
      localColors[localLineCount] = static_cast<uint8_t>(editingColor);
      localQtys[localLineCount]   = editingQty;
      localLineCount++;
    }
    // retour a la page 0 pour ajouter une autre ligne ou finir
    orderPage = 0;
    editingColor = KeypadColor::NONE;
    editingQty = 0;
    editingQtyStarted = false;

    // si on a deja des lignes, on propose le resume
    if (localLineCount > 0) {
      // on verifie si tout est pret pour envoyer
      // (on attend que l'utilisateur appuie sur # pour passer au resume)
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
  objetPmul.sendLocalOrder(0x01, localLineCount, localColors, localQtys);
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
      lcd.print("Nouvelle cmd");
    } else {
      lcd.print("Cmd: ");
      // resume compact
      for (uint8_t i = 0; i < localLineCount && i < 4; i++) {
        lcd.print(colorName(static_cast<KeypadColor>(localColors[i]))[0]);
        lcd.print(localQtys[i]);
        if (i < localLineCount - 1) lcd.print(",");
      }
    }
    lcd.setCursor(0, 1);
    if (localLineCount > 0) {
      lcd.print("A/B/C + #=fin D=del");
    } else {
      lcd.print("A/B/C=couleur #=fin");
    }
  }
  else if (orderPage == 1) {
    lcd.setCursor(0, 0);
    lcd.print(colorName(editingColor));
    lcd.setCursor(0, 1);
    lcd.print("Qte: ");
    lcd.print(editingQty);
    lcd.print(" *=ok");
  }
  else if (orderPage == 2) {
    // resume avant envoi
    lcd.setCursor(0, 0);
    lcd.print("B");
    lcd.print(countColor(KeypadColor::BLUE));
    lcd.print(" J");
    lcd.print(countColor(KeypadColor::YELLOW));
    lcd.print(" M");
    lcd.print(countColor(KeypadColor::MAGENTA));
    lcd.setCursor(0, 1);
    lcd.print("*=envoyer #=annul");
  }
}

uint8_t countColor(KeypadColor c) {
  uint8_t total = 0;
  for (uint8_t i = 0; i < localLineCount; i++) {
    if (static_cast<KeypadColor>(localColors[i]) == c) {
      total += localQtys[i];
    }
  }
  return total;
}

// overload keyToColor via pmul2-colors.h keypad->color mapping
// (already defined inline in the header)
