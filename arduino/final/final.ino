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
  // FIX: 9600 bauds saturait le buffer UART (64o) en ~67ms lors de prints rapides,
  //      bloquant la loop et retardant les envois capteurs vers le Pi.
  Serial1.begin(115200);

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
  servoStock.write(45);
  servoCommande.write(45);
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

      // notifie le Pi du changement d'etat
      switch (etapeActu) {
        case 0: objetPmul.sendReady(); break;
        case 1: objetPmul.sendBusy();  break;
      }
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
    servoStock.write(45);
    servoCommande.write(45);
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
        servoScan.write(10);
        etapeActu = 1;
        Serial1.println("[SCAN] bloc bloque, demande info...");
      }
      break;

    case 1: { // Scan — on demande l'info au Pi, max 3 essais
      // FIX: scanRequested etait initialise a true, ce qui empechait l'envoi de la
      //      demande au premier passage et introduisait un delai systematique de 1500ms.
      static bool scanRequested = false;
      static byte scanRetries = 0;
      static bool freshScan = true;

      // reinitialisation au premier passage apres un nouveau bloc
      if (freshScan) {
        scanRetries = 0;
        scanRequested = false;
        freshScan = false;
      }

      // envoi de la demande si pas encore fait et pas epuise
      if (!scanRequested && scanRetries < 3) {
        objetPmul.sendScanNeeded();
        scanRequested = true;
        tempsDepart = tempsActuel;
      }

      if (objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId,
                                   currentHue, currentSaturation, currentValue, currentTeam)) {
        freshScan = true;

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
            servoCommande.write(0);
            break;
          case ItemDecision::STOCK:
            servoStock.write(0);
            break;
          default:
            break;
        }

        tempsDepart = tempsActuel;
        etapeActu = 2;
      } else if (scanRequested && tempsActuel - tempsDepart > 1500) {
        // delai depasse sans reponse : re-essaie ou abandonne
        scanRequested = false;
        if (scanRetries < 3) {
          scanRetries++;
          Serial1.print("[SCAN] retry ");
          Serial1.println(scanRetries);
        }
        if (scanRetries >= 3) {
          currentDecision = ItemDecision::PASS;
          Serial1.println("[SCAN] 3 essais echoues, -> PASS");
          freshScan = true;
          tempsDepart = tempsActuel;
          etapeActu = 2;
        }
      }
      break;
    }

    case 2: { // Liberation — delai servo puis on libere le bloc
      // FIX: sans timeout, si le bloc restait coince sur IR_SCAN apres le relachement
      //      du servo, la machine bouclait indefiniment en case 2.
      static unsigned long tempsRelache = 0;

      if (tempsActuel - tempsDepart >= (unsigned long)attenteServo) {
        servoScan.write(0);

        // marque le moment du premier relachement pour le timeout
        if (tempsRelache == 0) tempsRelache = tempsActuel;

        // attend que le bloc quitte la zone de scan
        if (etatsIR[IR_SCAN] == 0) {
          tempsRelache = 0;
          tempsDepart = tempsActuel; // horodatage de depart pour le timeout du case 3
          etapeActu = 3;
          Serial1.println("[RELACHE] bloc parti, attente confirmation...");
        } else if (tempsActuel - tempsRelache > 3000) {
          // bloc toujours present 3s apres relachement : retour a l'attente
          Serial1.println("[WARN] bloc coince apres relachement, retour attente");
          tempsRelache = 0;
          etapeActu = 0;
        }
      }
      break;
    }

    case 3: // Confirmation — le bon capteur IR confirme le passage
      {
        // FIX: sans timeout, un bloc n'atteignant jamais son capteur de confirmation
        //      bloquait indefiniment le systeme, stoppant tout traitement ulterieur.
        if (tempsActuel - tempsDepart > 5000) {
          Serial1.println("[WARN] timeout confirmation, retour attente");
          servoStock.write(45);
          servoCommande.write(45);
          etapeActu = 0;
          break;
        }

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
            objetPmul.sendOrderDone();
          }

          servoStock.write(45);
          servoCommande.write(45);
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

  char buf[17];

  if (!systemOn) {
    lcdWriteLine(0, "MODE:MAINTENANCE");
    lcdWriteLine(1, "SYSTEME ARRETE");
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
    lcdWriteLine(0, "Cmd effectuees:");
    snprintf(buf, 17, "%u", completedOrders);
    lcdWriteLine(1, buf);
    return;
  }

  if (etapeActu >= 2 && (currentDecision == ItemDecision::ORDER || currentDecision == ItemDecision::STOCK)) {
    snprintf(buf, 17, "H%u S%u V%u", currentHue, currentSaturation, currentValue);
    lcdWriteLine(0, buf);
    if (currentTeam >= 0x01 && currentTeam <= 0x05) {
      snprintf(buf, 17, "T0%u %s #%u", currentTeam,
               currentDecision == ItemDecision::ORDER ? "ORDER" : "STOCK", currentOrderId);
    } else {
      snprintf(buf, 17, "T? %s #%u",
               currentDecision == ItemDecision::ORDER ? "ORDER" : "STOCK", currentOrderId);
    }
    lcdWriteLine(1, buf);
  } else {
    snprintf(buf, 17, "Total Tries: %u", totalArticlesTries);
    lcdWriteLine(0, buf);
    snprintf(buf, 17, "%u article%s", totalArticlesTries, totalArticlesTries <= 1 ? "" : "s");
    lcdWriteLine(1, buf);
  }
}

void drawOrderMenu() {
  char buf[17];
  uint8_t pos;

  if (orderPage == 0) {
    if (localLineCount == 0) {
      lcdWriteLine(0, "Ajouter ligne?");
    } else {
      pos = snprintf(buf, 17, "Cmd: ");
      for (uint8_t i = 0; i < localLineCount && pos < 16; i++) {
        buf[pos++] = colorNameById(localColors[i])[0];
        if (localQtys[i] >= 10 && pos < 16) buf[pos++] = '0' + (localQtys[i] / 10);
        if (pos < 16) buf[pos++] = '0' + (localQtys[i] % 10);
        if (i < localLineCount - 1 && pos < 16) buf[pos++] = ',';
      }
      buf[pos] = '\0';
      lcdWriteLine(0, buf);
    }

    pos = 0;
    if (activeColorCount > 0 && editingColorIdx < activeColorCount) {
      buf[pos++] = '[';
      const char* nom = colorNameById(activeColors[editingColorIdx]);
      buf[pos++] = nom[0];
      if (nom[1]) buf[pos++] = nom[1];
      buf[pos++] = ']';
    }
    if (localLineCount > 0 && pos < 16) {
      while (pos < 5 && pos < 16) buf[pos++] = ' ';
      const char* fin = " #=fin";
      for (uint8_t i = 0; fin[i] && pos < 16; i++) buf[pos++] = fin[i];
    }
    buf[pos] = '\0';
    lcdWriteLine(1, buf);
  }
  else if (orderPage == 1) {
    if (editingColorIdx < activeColorCount) {
      snprintf(buf, 17, "%s", colorNameById(activeColors[editingColorIdx]));
      lcdWriteLine(0, buf);
    } else {
      lcdWriteLine(0, "");
    }
    snprintf(buf, 17, "Qte: %u pressez=ok", editingQty);
    lcdWriteLine(1, buf);
  }
  else if (orderPage == 2) {
    uint8_t b = countColor(COLOR_BLUE), j = countColor(COLOR_YELLOW), m = countColor(COLOR_MAGENTA);
    snprintf(buf, 17, "B%u J%u M%u", b, j, m);
    lcdWriteLine(0, buf);
    lcdWriteLine(1, "pressez=envoi #=annul");
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

// FIX: double-buffer statique
static void lcdWriteLine(uint8_t line, const char* text) {
  static char cache[2][17] = {"", ""};

  // construit la ligne paddee a 16 caracteres dans un buffer local
  char buf[17];
  uint8_t len = 0;
  while (len < 16 && text[len]) { buf[len] = text[len]; len++; }
  while (len < 16) buf[len++] = ' ';
  buf[16] = '\0';

  // si identique au dernier affichage, pas d'ecriture I2C
  if (memcmp(cache[line], buf, 16) == 0) return;
  memcpy(cache[line], buf, 17);

  lcd.setCursor(0, line);
  for (uint8_t i = 0; i < 16; i++) lcd.print(buf[i]);
}
