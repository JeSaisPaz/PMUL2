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
Order targetOrder;
Order currentOrder;
Color detectedBlockColor;
Team   detectedBlockTeam = Team::TeamUnknown;
bool   currentBlockForOrder = false;

// 0=Attente, 1=Scan, 2=Aiguillage, 3=Sortie
byte etapeActu = 0;

// Capteurs IR1 à 5
byte pinsIR[] = {8, 7, 6, 5, 4}; // IR1=Scan, IR2=Intermédiaire, IR3/IR4/IR5=Confirmation
bool etatsIR[] = {0, 0, 0, 0, 0}; 

// Servo Moteur
Servo servoScan;
Servo servoStock;
Servo servoCommande;

unsigned long tempsActuel = 0;
unsigned long tempsDepart = 0;
long attenteServo = 500;

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

  // Position Initiale
  servoScan.write(0);
  servoStock.write(0);
  servoCommande.write(0);

  Serial1.println("--- Processus en Marche ---");

  // dit au Pi qu'on est pret (un seul byte pour pas polluer le stream)
  Serial.write('R');
}

void loop() {
  updateLCD();

  // diag: toujours ecouter les pings
  objetPmul.handlePing();

  if(!systemOn){
    servoScan.write(0);
    servoStock.write(0);
    servoCommande.write(0);
    etapeActu = 0;
    return;
  }

  tempsActuel = millis(); // On prend l'heure actuelle

// Partie 1: Capteurs IR (Test)

  for (byte i = 0; i < 5; i++) {
    bool lecture = (digitalRead(pinsIR[i]) == LOW);

    // Test monitor pour IRi
    if (lecture != etatsIR[i]){
      etatsIR[i] = lecture;

      // Message formaté : "IRx: 1" ou "IRx: 0"
      Serial1.print("IR");
      Serial1.print(i + 1);
      Serial1.print(": ");
      Serial1.println(etatsIR[i] ? "1" : "0");
    }
  }
// Partie 2: Servos Moteurs

  switch(etapeActu) {
    // Etape Attente
    case 0:
      if (objetPmul.readTargetOrder(targetOrder)) {
        currentOrder.teamId = targetOrder.teamId;
        currentOrder.reset();
        Serial1.print("Nouvelle commande: Team ");
        Serial1.print(targetOrder.teamId);
        Serial1.print(" Cible B=");
        Serial1.print(targetOrder.blueAmount);
        Serial1.print(" Y=");
        Serial1.print(targetOrder.yellowAmount);
        Serial1.print(" M=");
        Serial1.println(targetOrder.magentaAmount);
      }
      if (etatsIR[0]) {
        servoScan.write(90);
        etapeActu = 1;
      }
      break;

    // Etape Scan
    case 1: {
      static bool scanRequested = false;
      if (!scanRequested) {
        objetPmul.sendScanNeeded(); // dit au Pi de scanner maintenant
        scanRequested = true;
      }
      if (objetPmul.readBlockInfo(detectedBlockColor, detectedBlockTeam)){
        scanRequested = false;
        Serial1.print("Block detecte: Couleur=");
        Serial1.print(static_cast<int>(detectedBlockColor));
        Serial1.print(" Team=");
        Serial1.println(static_cast<int>(detectedBlockTeam));

        if (detectedBlockTeam != Team::TeamUnknown){
          servoCommande.write(45);
          Serial1.println("Decision: Commande");
          currentBlockForOrder = true;
        } else {
          servoStock.write(45);
          Serial1.println("Decision: Stock");
          currentBlockForOrder = false;
        }
        tempsDepart = tempsActuel;
        etapeActu = 2;
      }
      }  // fin du bloc case 1
      break;

    // Etape Aiguillage
    case 2:
      if ((tempsActuel - tempsDepart >= attenteServo) && (etatsIR[1] == 0)){
        servoScan.write(0);
        etapeActu = 3;
      }
      break;
      
    // Etape Sortie & Reset
    case 3:
      if(etatsIR[2] || etatsIR[3] || etatsIR[4]){
        if (currentBlockForOrder && targetOrder.teamId != 0xFF) {
          currentOrder.addBox(detectedBlockColor);
          totalArticlesTries++;

          Serial1.print("Progres: B=");
          Serial1.print(currentOrder.blueAmount);
          Serial1.print(" Y=");
          Serial1.print(currentOrder.yellowAmount);
          Serial1.print(" M=");
          Serial1.println(currentOrder.magentaAmount);

          if (currentOrder.isComplete(targetOrder)) {
            objetPmul.sendOrderDone();
            Serial1.println(">>> COMMANDE TERMINEE <<<");
            currentOrder.reset();
          } else {
            objetPmul.sendOrder(currentOrder);
          }
        }

        servoStock.write(0);
        servoCommande.write(0);
        etapeActu = 0;
      }
      break;
  }
}

void updateLCD(){
  static unsigned long dernierRefresh = 0;
  if(millis()-dernierRefresh < 500)
    return;
  dernierRefresh = millis();

  lcd.setCursor(0,0);
  if(!systemOn){
    lcd.print("MODE:MAINTENANCE");
    lcd.setCursor(0,1);
    lcd.print("SYSTEME ARRETE");
  } else{
    lcd.print("Total Tries: ");
    lcd.setCursor(0,1);
    lcd.print(totalArticlesTries);
    lcd.print(" articles");
  }
}