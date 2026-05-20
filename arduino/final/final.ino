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
ItemDecision currentDecision = ItemDecision::NO_DECISION;
uint8_t     currentOrderId  = 0;
uint8_t     currentHue = 0, currentSaturation = 0, currentValue = 0, currentTeam = 0;

// keypad
Pmul2Keypad  keypad;

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

  servoScan.write(10);
  servoStock.write(45);
  servoCommande.write(45);

  etapeActu = 0;
}

bool needScan = false;

void checkScanArea() {
    if(!etatsIR[IR_SCAN] && etatsIR[IR_NEXT]||!etatsIR[IR_SCAN] && !etatsIR[IR_NEXT]) {
    if(currentDecision != ItemDecision::NO_DECISION &&( !tapeActu == 1 || !etapeActu ==  2)) {
      servoScan.write(0);
      needScan = false;
    
    }
    
  }
  else {
    servoScran.write(10);
    needScan = true;
  }
}

void loop() {

  // 1 Partie capteurs et handler
  checkScanArea();
  sendSensorStatus(etatsIR[]);


  // debug
  objetPmul.handlePing();

  // 2 Continue le process avec la machine a etat

  // Couleurs du back
  {
    uint8_t colors[4];
    uint8_t count;
    if (objetPmul.readColorList(colors, count)) {
      activeColorCount = count;
      for (uint8_t i = 0; i < count; i++) {
        activeColors[i] = colors[i];
      }
      //Serial1.print("[COLORS] ");
      //Serial1.println(activeColorCount);
    }
  }

  if(systemOn) {
    switch(etapeActu) {
      // Attente
      case 0: {
        if(needScan) {
          objetPmul.sendScanNeeded();
          if(objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId, currentHue, currentSaturation, currentValue, currentTeam)) {
             etapeActu++;
          }
        }

      }
      // Scan
      case 1: {
        switch (currentDecision) {
              case ItemDecision::ORDER:
                servoCommande.write(0);
                break;
              case ItemDecision::STOCK:
                servoStock.write(0);
                break;
              default:
                // Pas de decision = tout droit (PASS)
                break;
              }

              etapeActu++;

      }
      // Confirmation
      case 2: {

        switch(currentDecision) {
          case ItemDecision::ORDER: {
            if(!etatsIR[IR_ORDER]) {
              servoComande.write(45);
              break;

          }
          case ItemDecision::STOCK: {
            if(!etatsIR[IR_STOCK]) {
              servoStock.write(45);
              break;
            }
          }
          case ItemDecision::PASS: {
            if(!etatsIR[IR_PASS]) {
              break;
            }

          }
          default:
            break;
          }
        }
        etapeActu = 0;
      }
    }
  }
}