#include <Servo.h>
#include "pmul2-lib.h"

Pmul2Lib objetPmul(Serial);
Order targetOrder;
Order currentOrder;
Color detected;
Team teamDeteced;

// Capteurs IR
byte ir1 = 8;
byte ir2 = 7; 
byte ir3 = 6;
byte ir4 = 5;
byte ir5 = 4;

byte pinsIR[] = {ir1, ir2, ir3, ir4, ir5};
byte derniersEtats[] = {2, 2, 2, 2, 2}; 
unsigned long dernierMoment[] = {0, 0, 0, 0, 0};
byte intervalleDebounce = 50;

// Servo Moteur
Servo servoScan;
Servo servoStock;
Servo servoCommande;

void setup() {
  Serial.begin(9600);
  for (byte i = 0; i < 5; i++) {
    pinMode(pinsIR[i], INPUT_PULLUP);
  }

  servoScan.attach(11);
  servoStock.attach(10);
  servoCommande.attach(9);

  servoScan.write(0);
  servoStock.write(0);
  servoCommande.write(0);

  Serial.println("--- Processus en Marche ---");
}

void loop() {
  unsigned long tempsActuel = millis();

  for (byte i = 0; i < 5; i++) {
    byte lecture = digitalRead(pinsIR[i]);
    byte etatObjet;

    if (lecture == LOW) {
      etatObjet = 1;
    } else {
      etatObjet = 0;
    }

    if (etatObjet != derniersEtats[i]) {
      if (tempsActuel - dernierMoment[i] > intervalleDebounce) {
        
        Serial.print("Etat Objet: ");
        Serial.print(etatObjet);
        
        if (i == 0) Serial.println(" (ir1: SCAN)");
        else if (i == 1) 
            Serial.println(" (ir2: PASSAGE)");
        else if (i == 2) 
            Serial.println(" (ir3: STOCK)");
        else if (i == 3) 
            Serial.println(" (ir4: COMMANDE)");
        else if (i == 4) 
            Serial.println(" (ir5: TEAM)");

        derniersEtats[i] = etatObjet;
        dernierMoment[i] = tempsActuel;
      }
    }
  }

  if (derniersEtats[0] == 1) {
    servoScan.write(90);
  } 
  
  if(objetPmul.readTargetOrder(targetOrder)){
    if (currentOrder.isComplete(targetOrder)) { 
        servoCommande.write(45);
        delay(500);
        servoScan.write(0);
    } else {
        servoStock.write(45);
        delay(500);
        servoScan.write(0);
    }
  }

  if (derniersEtats[2] == 1 || derniersEtats[3] == 1 || derniersEtats[4] == 1){
    objetPmul.sendOrderDone();
    delay(300);
    servoStock.write(0);
    servoCommande.write(0);
    derniersEtats[2] = 0;
    derniersEtats[3] = 0;
    derniersEtats[4] = 0;
  }
}