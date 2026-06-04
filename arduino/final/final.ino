/* PMUL2 - Systeme de Tri Automatise */
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "pmul2-lib.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

uint8_t btn1 = 2, btn2 = 3;
volatile unsigned long btn1UpdateTime = 0, btn2UpdateTime = 0;
volatile bool systemOn = true, modeAffichage = false, modeAffichageChanged = true;
uint16_t completedOrdersCount = 0, newCount = 0;

Pmul2Lib objetPmul(Serial);

uint8_t etapeActu = 0, etapePrecedente = 255; 
unsigned long tempsEntreeEtat = 0;   
const unsigned long TIMEOUT_SCAN = 5000, TIMEOUT_CONFIRMATION = 10000; 
bool previousBoxCleared = false, entreeEtat2 = false;

uint8_t pinsIR[] = {8, 7, 6, 5, 4};
bool etatsIR[] = {0, 0, 0, 0, 0}, etatsIRPrecedents[] = {0, 0, 0, 0, 0};
unsigned long dernierEnvoiCapteurs = 0;
const unsigned long INTERVALLE_ENVOI_CAPTEURS = 1000; 
bool capteursOntChange = false;

#define IR_SCAN 0
#define IR_NEXT 1
#define IR_STOCK 2
#define IR_ORDER 3
#define IR_PASS 4

Servo servoScan, servoStock, servoCommande;
#define SERVO_BLOQUE 0
#define SERVO_LIBRE 20
#define SERVO_AIGUILLAGE 0
#define SERVO_NEUTRE 50

const uint8_t ROWS = 4, COLS = 4; 
char keys[ROWS][COLS] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}};
uint8_t rowPins[ROWS] = {31, 33, 35, 37}, colPins[COLS] = {43, 45, 47, 49}; 
Keypad customKeypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS); 

uint16_t currentItemId = 0;
ItemDecision currentDecision = ItemDecision::NO_DECISION;
uint8_t currentOrderId = 0, currentHue = 0, currentSaturation = 0, currentValue = 0, currentTeam = 0;
bool modeOrder = false;
uint8_t orderPage = 0, tempQty = 0, selectedColorForQty = 0;
bool menuNeedsUpdate = true;
uint8_t activeColors[4] = {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA}, activeColorCount = 3;
uint8_t orderLine[6][2], orderLineCount = 0, orderColors[6], orderQuantities[6];

void basculeSystem() {
  unsigned long t = millis();
  if(t - btn1UpdateTime < 200) return;
  btn1UpdateTime = t;
  systemOn = !systemOn;
  modeAffichageChanged = true;
}

void basculeAffichage() {
  if(!systemOn) {
    unsigned long t = millis();
    if(t - btn2UpdateTime < 200) return;
    btn2UpdateTime = t;
    modeAffichage = !modeAffichage;
    modeAffichageChanged = true;
  }
}

String colorDisplayFormatById(uint8_t id, bool shortF) {
  switch(id) {
    case COLOR_BLUE: return shortF ? "BL" : "BL:1";
    case COLOR_YELLOW: return shortF ? "YL" : "YL:2";
    case COLOR_MAGENTA: return shortF ? "MG" : "MG:3";
    case COLOR_BROWN: return shortF ? "BR" : "BR:4";
    case COLOR_ORANGE: return shortF ? "OR" : "OR:5";
    default: return "?";
  }
}

uint8_t colorIndexCharToId(char c) {
  switch(c) {
    case '1': return activeColors[0];
    case '2': return activeColors[1];
    case '3': return activeColors[2];
    case '4': return activeColors[3];
    default: return 0xFF;
  }
}

bool isColorInOrder(uint8_t id) {
  for (uint8_t i = 0; i < orderLineCount; i++) if (orderLine[i][0] == id) return true;
  return false;
}

void updateIRStates() {
  capteursOntChange = false;
  for (uint8_t i = 0; i < 5; i++) {
    bool e = (digitalRead(pinsIR[i]) == LOW);
    if (e != etatsIRPrecedents[i]) {
      etatsIR[i] = e;
      capteursOntChange = true;
    }
  }
}

void setup() {
  Serial.begin(9600);
  Serial.write('R');
  lcd.init();
  lcd.backlight();
  pinMode(btn1, INPUT_PULLUP);
  pinMode(btn2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(btn1), basculeSystem, RISING);
  attachInterrupt(digitalPinToInterrupt(btn2), basculeAffichage, RISING);
  for (uint8_t i = 0; i < 5; i++) pinMode(pinsIR[i], INPUT_PULLUP);
  servoScan.attach(11);
  servoStock.attach(10);
  servoCommande.attach(9);
  servoScan.write(SERVO_BLOQUE);
  servoStock.write(SERVO_NEUTRE);
  servoCommande.write(SERVO_NEUTRE);
  
  uint8_t colors[4], count;
  if (objetPmul.readColorList(colors, count)) {
    activeColorCount = count;
    for (uint8_t i = 0; i < count; i++) activeColors[i] = colors[i];
  }
}

void loop() {
  updateIRStates();
  unsigned long now = millis();
  if (capteursOntChange || (now - dernierEnvoiCapteurs > INTERVALLE_ENVOI_CAPTEURS)) {
    objetPmul.sendSensorStatus(etatsIR[0], etatsIR[1], etatsIR[2], etatsIR[3], etatsIR[4]);
    dernierEnvoiCapteurs = now;
    for (uint8_t i = 0; i < 5; i++) etatsIRPrecedents[i] = etatsIR[i];
  }
  
  char key = customKeypad.getKey();
  if(key == '*' && !modeOrder && (etapeActu == 0 || etapeActu == 1)) {
    modeOrder = true;
    orderPage = 0;
    orderLineCount = 0;
    menuNeedsUpdate = true;
  }
  
  if(modeOrder) {
    if(orderPage == 0) {
      if(menuNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0,0);
        for (uint8_t i=0; i<activeColorCount && i<3; i++) lcd.print(colorDisplayFormatById(activeColors[i], false));
        lcd.setCursor(0,1);
        if(activeColorCount>=4) lcd.print(colorDisplayFormatById(activeColors[3], false));
        lcd.print(" #:Suite");
        menuNeedsUpdate=false;
      }
      if(key >= '1' && key <= '4') {
        uint8_t id = colorIndexCharToId(key);
        if(id!=0xFF && !isColorInOrder(id) && orderLineCount<6) {
          selectedColorForQty=id;
          tempQty=0;
          orderPage=2;
          menuNeedsUpdate=true;
        }
      }
      else if(key == '#' && orderLineCount > 0) {
        orderPage=1;
        menuNeedsUpdate=true;
      }
    } else if(orderPage == 2) {
      if(menuNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Qty: ");
        lcd.print(colorDisplayFormatById(selectedColorForQty, true));
        lcd.print(" 1+ 2- #OK");
        lcd.setCursor(0,1);
        lcd.print(tempQty);
        menuNeedsUpdate=false;
      }
      if(key == '1' && tempQty < 10) { tempQty++; menuNeedsUpdate=true; }
      else if(key == '2' && tempQty > 0) { tempQty--; menuNeedsUpdate=true; }
      else if(key == '#') {
        orderLine[orderLineCount][0]=selectedColorForQty;
        orderLine[orderLineCount][1]=tempQty;
        orderLineCount++;
        orderPage=0;
        menuNeedsUpdate=true;
      }
    } else if(orderPage == 1) {
      if(menuNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0,0);
        for(uint8_t i=0; i<orderLineCount && i<3; i++) {
          lcd.print(colorDisplayFormatById(orderLine[i][0], true));
          lcd.print(":");
          lcd.print(orderLine[i][1]);
        }
        lcd.setCursor(0,1);
        lcd.print("C:Annul D:Conf.");
        menuNeedsUpdate=false;
      }
      if(key == 'C') { modeOrder=false; orderLineCount=0; modeAffichageChanged=true; }
      else if(key == 'D') {
        for(uint8_t i=0; i<orderLineCount; i++) {
          orderColors[i]=orderLine[i][0];
          orderQuantities[i]=orderLine[i][1];
        }
        objetPmul.sendLocalOrder(orderLineCount, orderColors, orderQuantities);
        modeOrder=false;
        orderLineCount=0;
        lcd.clear();
        lcd.print("Cmd Envoyee!");
        modeAffichageChanged=true;
      }
    }
  }
  
  if(!modeOrder && modeAffichageChanged) {
    modeAffichageChanged = false;
    lcd.clear();
    lcd.setCursor(0,0);
    if(modeAffichage) {
      lcd.print("Completed Orders:");
      lcd.setCursor(0,1);
      lcd.print(completedOrdersCount);
    } else if(currentDecision != ItemDecision::NO_DECISION) {
      lcd.print("Item: #");
      lcd.print(currentItemId);
      lcd.setCursor(0,1);
      switch(currentDecision) {
        case ItemDecision::ORDER: lcd.print("ORDER #"); lcd.print(currentOrderId); break;
        case ItemDecision::STOCK: lcd.print("STOCK"); break;
        case ItemDecision::PASS: lcd.print("PASS"); break;
        default: lcd.print("?");
      }
    } else lcd.print("No item");
    modeAffichageChanged = true;
  }
  
  if(systemOn && !modeOrder) {
    if (etapeActu != etapePrecedente) { tempsEntreeEtat = millis(); etapePrecedente = etapeActu; }
    switch(etapeActu) {
      case 0:
        servoScan.write(SERVO_BLOQUE);
        if(etatsIR[IR_SCAN]) { objetPmul.sendScanNeeded(); etapeActu=1; }
        break;
      case 1:
        if (now - tempsEntreeEtat > TIMEOUT_SCAN) { currentDecision=ItemDecision::PASS; etapeActu=2; }
        else if(objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId, currentHue, currentSaturation, currentValue, currentTeam)) etapeActu=2;
        break;
      case 2:
        if(!entreeEtat2) {
          entreeEtat2=true;
          if(currentDecision==ItemDecision::ORDER) servoCommande.write(SERVO_AIGUILLAGE);
          else if(currentDecision==ItemDecision::STOCK) servoStock.write(SERVO_AIGUILLAGE);
        }
        if(now - tempsEntreeEtat > 300) etapeActu=3;
        break;
      case 3:
        servoScan.write(SERVO_LIBRE);
        etapeActu=4;
        break;
      case 4:
        if(!etatsIR[IR_SCAN]) { servoScan.write(SERVO_BLOQUE); etapeActu=5; }
        break;
      case 5:
        if(now - tempsEntreeEtat > TIMEOUT_CONFIRMATION) {
          etapeActu=0;
          objetPmul.sendScanResult(currentItemId, ItemStatus::FAILED);
        } else {
          bool conf = (currentDecision==ItemDecision::ORDER && etatsIR[IR_ORDER]) ||
                      (currentDecision==ItemDecision::STOCK && etatsIR[IR_STOCK]) ||
                      (currentDecision==ItemDecision::PASS && etatsIR[IR_PASS]) ||
                      (currentDecision==ItemDecision::NO_DECISION);
          if(conf) {
            objetPmul.sendScanResult(currentItemId, ItemStatus::CONFIRMED);
            servoStock.write(SERVO_NEUTRE);
            servoCommande.write(SERVO_NEUTRE);
            completedOrdersCount = objetPmul.readCompletedCount(newCount);
            etapeActu=0;
          }
        }
        break;
    }
  } else if (!systemOn) {
    servoScan.write(SERVO_LIBRE);
    servoStock.write(SERVO_NEUTRE);
    servoCommande.write(SERVO_NEUTRE);
  }
}