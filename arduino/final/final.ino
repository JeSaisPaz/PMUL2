#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "pmul2-lib.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

byte btn1 = 2;
byte btn2 = 3;

volatile bool systemOn = true;
volatile byte modeAffichage = 0;
int totalArticlesTries = 0;

// Com Raspberry Pi via USB (Serial = port USB natif du MEGA)
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

// infos du bloc en cours de scan (recu du Pi/backend)
uint16_t    currentItemId = 0;
ItemDecision currentDecision = ItemDecision::PASS;
uint8_t     currentOrderId  = 0;

void basculeSystem(){
  systemOn = !systemOn;
}

void basculeAffichage(){
  modeAffichage = (modeAffichage+1)%2;
}

void setup() {
  Serial.begin(9600);   // protocole SerialTransfer vers Raspberry Pi
  Serial1.begin(9600);  // debug (pins 18/19, optionnel)

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

  // dit au Pi qu'on est pret (evite de parler au bootloader)
  Serial.write('R');
  Serial1.println("Processus en Marche");
}

void loop() {
  updateLCD();

  // diag: toujours ecouter les pings
  objetPmul.handlePing();

  if (!systemOn) {
    servoScan.write(0);
    servoStock.write(0);
    servoCommande.write(0);
    etapeActu = 0;
    return;
  }

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

    // Attente : on attend qu'un bloc arrive
    case 0:
      if (etatsIR[0]) {
        servoScan.write(90);  // bloque le bloc
        etapeActu = 1;
      }
      break;

    // Scan : on demande l'info du bloc au Pi/backend
    case 1: {
      static bool scanRequested = false;
      if (!scanRequested) {
        objetPmul.sendScanNeeded();  // dit au Pi de scanner
        scanRequested = true;
      }

      if (objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId)) {
        scanRequested = false;

        Serial1.print("Item #");
        Serial1.print(currentItemId);
        Serial1.print(" decision=");
        Serial1.print(static_cast<int>(currentDecision));
        Serial1.print(" orderId=");
        Serial1.println(currentOrderId);

        // aiguillage selon la decision du backend
        switch (currentDecision) {
          case ItemDecision::ORDER:
            servoCommande.write(45);
            Serial1.println("Decision: Commande");
            break;
          case ItemDecision::STOCK:
            servoStock.write(45);
            Serial1.println("Decision: Stock");
            break;
          default: // PASS - on laisse passer, pas de shunt
            Serial1.println("Decision: Pass");
            break;
        }

        tempsDepart = tempsActuel;
        etapeActu = 2;
      }
      break;
    }

    // Aiguillage : on attend le delai servo puis on libere le bloc
    case 2:
      if ((tempsActuel - tempsDepart >= attenteServo) && (etatsIR[1] == 0)) {
        servoScan.write(0);  // libere le bloc
        etapeActu = 3;
      }
      break;

    // Sortie : confirmation IR, on envoie le resultat
    case 3:
      if (etatsIR[2] || etatsIR[3] || etatsIR[4]) {
        // bloc bien passe par les capteurs de confirmation
        objetPmul.sendScanResult(currentItemId, ItemStatus::CONFIRMED);
        totalArticlesTries++;

        Serial1.print("Resultat: item #");
        Serial1.print(currentItemId);
        Serial1.println(" CONFIRMED");

        servoStock.write(0);
        servoCommande.write(0);
        etapeActu = 0;
      }
      break;
  }
}

void updateLCD() {
  static unsigned long dernierRefresh = 0;
  if (millis() - dernierRefresh < 500)
    return;
  dernierRefresh = millis();

  lcd.setCursor(0, 0);
  if (!systemOn) {
    lcd.print("MODE:MAINTENANCE");
    lcd.setCursor(0, 1);
    lcd.print("SYSTEME ARRETE");
    return;
  }

  // si on est en train de trier un bloc ORDER, on affiche l'orderId
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
