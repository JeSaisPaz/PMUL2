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
uint16_t completedOrders = 0; // recu du backend via PID_COMPLETED_COUNT

// Com Raspberry Pi via USB
Pmul2Lib objetPmul(Serial);

// 0=Attente, 1=Scan, 2=Liberation, 3=Confirmation
byte etapeActu = 0;

// Capteurs IR: pin, role, confirmation associee
byte pinsIR[] = {8, 7, 6, 5, 4};
bool etatsIR[] = {0, 0, 0, 0, 0};

#define IR_SCAN    0  // pin 8 - en face de l'actionneur (bloc en position)
#define IR_NEXT    1  // pin 7 - une boite derriere (prochain bloc)
#define IR_STOCK   2  // pin 6 - confirmation stock
#define IR_ORDER   3  // pin 5 - confirmation commande
#define IR_PASS    4  // pin 4 - confirmation autre (passe tout droit)

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
uint8_t     currentHue = 0, currentSaturation = 0, currentValue = 0, currentTeam = 0;

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
  Serial1.println(systemOn ? "[SYS] ON" : "[SYS] OFF (maintenance)");
}

void basculeAffichage(){
  modeAffichage = (modeAffichage+1)%2;
  Serial1.print("[AFF] BP2 -> mode ");
  Serial1.println(modeAffichage);
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

  // log changement d'etape
  {
    static byte lastEtape = 0xFF;
    if (etapeActu != lastEtape) {
      const char* noms[] = {"Attente", "Scan", "Liberation", "Confirmation"};
      Serial1.print("[ETAPE] ");
      Serial1.println(noms[etapeActu]);
      lastEtape = etapeActu;
    }
  }

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
      Serial1.print("[COLORS] ");
      Serial1.println(activeColorCount);
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
      const char* noms[] = {"SCAN", "NEXT", "STOCK", "ORDER", "PASS"};
      Serial1.print("[IR-");
      Serial1.print(noms[i]);
      Serial1.print("] ");
      Serial1.println(etatsIR[i] ? "ON" : "OFF");
      objetPmul.sendSensorStatus(etatsIR[0], etatsIR[1], etatsIR[2], etatsIR[3], etatsIR[4]);
    }
  }

  switch (etapeActu) {

    case 0: // Attente — on attend qu'un bloc arrive a l'actionneur
      if (etatsIR[IR_SCAN]) {
        servoScan.write(90);
        etapeActu = 1;
        Serial1.println("[SCAN] bloc bloque, demande info...");
      }
      break;

    case 1: { // Scan — on demande l'info au Pi, max 3 essais
      static bool scanRequested = false;
      static byte scanRetries = 0;

      if (!scanRequested) {
        objetPmul.sendScanNeeded();
        scanRequested = true;
        scanRetries = 0;
      }

      if (objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId,
                                   currentHue, currentSaturation, currentValue, currentTeam)) {
        scanRequested = false;

        Serial1.print("[ITEM] #");
        Serial1.print(currentItemId);
        Serial1.print(" dec=");
        Serial1.print(static_cast<int>(currentDecision));
        Serial1.print(" order=");
        Serial1.print(currentOrderId);
        Serial1.print(" H=");
        Serial1.print(currentHue);
        Serial1.print(" T=");
        Serial1.println(currentTeam);

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
      } else {
        // si toujours pas de reponse apres ~1.5s, on re-essaie
        if (tempsActuel - tempsDepart > 1500 && scanRetries < 3) {
          scanRequested = false; // force re-envoi du scan needed
          scanRetries++;
          Serial1.print("[SCAN] retry ");
          Serial1.println(scanRetries);
        }
        // 3 essais sans reponse -> on laisse passer (PASS)
        if (scanRetries >= 3 && tempsActuel - tempsDepart > 4500) {
          currentDecision = ItemDecision::PASS;
          Serial1.println("[SCAN] 3 essais echoues, -> PASS");
          scanRequested = false;
          tempsDepart = tempsActuel;
          etapeActu = 2;
        }
      }
      break;
    }

    case 2: // Liberation — delai servo puis on libere le bloc
      if (tempsActuel - tempsDepart >= attenteServo) {
        servoScan.write(0);
        // attend que le bloc quitte la zone de scan
        if (etatsIR[IR_SCAN] == 0) {
          etapeActu = 3;
          Serial1.println("[RELACHE] bloc parti, attente confirmation...");
        }
      }
      break;

    case 3: // Confirmation — le bon capteur IR confirme le passage
      {
        bool confirmed = false;
        switch (currentDecision) {
          case ItemDecision::ORDER:
            confirmed = etatsIR[IR_ORDER];
            break;
          case ItemDecision::STOCK:
            confirmed = etatsIR[IR_STOCK];
            break;
          default: // PASS
            confirmed = etatsIR[IR_PASS];
            break;
        }

        if (confirmed) {
          objetPmul.sendScanResult(currentItemId, ItemStatus::CONFIRMED);
          totalArticlesTries++;

          Serial1.print("[SORTIE] item ");
          Serial1.print(currentItemId);
          Serial1.print(" tries=");
          Serial1.println(totalArticlesTries);

          // check le nombre de commandes completes
          uint16_t newCount;
          if (objetPmul.readCompletedCount(newCount) && newCount != completedOrders) {
            Serial1.print("[COMPLETED] ");
            Serial1.print(completedOrders);
            Serial1.print(" -> ");
            Serial1.println(newCount);
            completedOrders = newCount;
          }

          servoStock.write(0);
          servoCommande.write(0);
          etapeActu = 0;
        }
      }  // fin bloc case 3
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
  Serial1.println("[ORDER] mode saisie active");
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
  Serial1.print("[ORDER] envoye: ");
  Serial1.print(localLineCount);
  Serial1.println(" lignes");
}

void exitOrderMode(bool sent) {
  modeOrder = false;
  orderPage = 0;
  localLineCount = 0;
  Serial1.println(sent ? "[ORDER] commande envoyee, retour scan" : "[ORDER] annule, retour scan");
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
  // BP2 (modeAffichage) switch entre deux vues
  if (modeAffichage == 1) {
    lcd.print("Cmd effectuees:");
    lcd.setCursor(0, 1);
    lcd.print(completedOrders);
    return;
  }

  if (etapeActu >= 2 && currentDecision == ItemDecision::ORDER) {
    lcd.print("H");
    lcd.print(currentHue);
    lcd.print(" S");
    lcd.print(currentSaturation);
    lcd.print(" V");
    lcd.print(currentValue);
    lcd.setCursor(0, 1);
    if (currentTeam >= 0x01 && currentTeam <= 0x05) {
      lcd.print("T0");
      lcd.print(currentTeam);
    } else {
      lcd.print("T?");
    }
    lcd.print(" ORDER #");
    lcd.print(currentOrderId);
  } else if (etapeActu >= 2 && currentDecision == ItemDecision::STOCK) {
    lcd.print("H");
    lcd.print(currentHue);
    lcd.print(" S");
    lcd.print(currentSaturation);
    lcd.print(" V");
    lcd.print(currentValue);
    lcd.setCursor(0, 1);
    if (currentTeam >= 0x01 && currentTeam <= 0x05) {
      lcd.print("T0");
      lcd.print(currentTeam);
    } else {
      lcd.print("T?");
    }
    lcd.print(" STOCK");
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
