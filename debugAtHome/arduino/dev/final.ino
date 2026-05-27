#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "pmul2-lib.h"

// Com Raspberry Pi via USB
Pmul2Lib objetPmul(Serial);

//IR sensors
uint8_t pinsIR[] = {8, 7, 6, 5, 4};
bool statesIR[] = {0, 0, 0, 0, 0};
#define IR_SCAN    0  // pin 8   
#define IR_NEXT    1  // pin 7
#define IR_STOCK   2  // pin 6
#define IR_ORDER   3  // pin 5
#define IR_PASS    4  // pin 4

// Servo Motors
Servo servoScan; // pin 11
Servo servoStock; // pin 10
Servo servoOrder; // pin 9
#define SERVO_SCAN_ON 0 
#define SERVO_SCAN_OFF 20
#define SERVO_PUSH 0 //Ajout d'un angle pour push la boite mais donc on dois repositioner le bras pour qu'il soit à 90° quand il est à 0°
#define SERVO_ON 45   
#define SERVO_OFF 90

//User interface
LiquidCrystal_I2C lcd(0x27, 16, 2);
uint8_t selectedColor;
bool needRedisplay = true;
bool displayMode = true;
bool maintenance = true;
bool orderMode = false;
volatile unsigned long lastBtn1;
volatile unsigned long lastBtn2;
#define btn1 2
#define btn2 3

const uint8_t ROWS = 4, COLS = 4; 
char keys[ROWS][COLS] = {{'1','2','3','A'},{'4','5','6','B'},{'7','8','9','C'},{'*','0','#','D'}};
uint8_t rowPins[ROWS] = {31, 33, 35, 37}, colPins[COLS] = {39, 41, 43, 45}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
char key = 0;

//Process
uint16_t completedOrdersCount = 0;
uint8_t orderLineCount;
ItemDecision currentDecision = ItemDecision::NO_DECISION;
uint16_t currentItemId = 0;
uint8_t currentOrderId = 0;
uint8_t step = 0;
uint8_t orderPage = 0;
uint8_t activeColors[4], activeColorCount;

unsigned long stepEnteredAt = 0;

bool updateIR() {
  bool sensorsChanged = false;
  for (byte i = 0; i < 5; i++) {
    bool state = !digitalRead(pinsIR[i]);
    if (state != statesIR[i]) {
      statesIR[i] = state;
      sensorsChanged = true;
    }
  }
  return sensorsChanged;
}

void updateColor(){
  uint8_t colors[4], count;
  if (objetPmul.readColorList(colors, count)) {
    activeColorCount = count;
    for (uint8_t i = 0; i < count; i++) activeColors[i] = colors[i];
    needRedisplay = true;
  }
}

void sendIR(){
  objetPmul.sendSensorStatus(statesIR[0], statesIR[1], statesIR[2], statesIR[3], statesIR[4]);
}

void processDisplay(){
  lcd.clear();
  lcd.setCursor(0,0);
  if(displayMode){
    lcd.print("Current order :");
    lcd.setCursor(0, 1);

  }else{
    lcd.print("Completed Orders");
    lcd.setCursor(0, 1);
    lcd.print(completedOrdersCount);
  }
  needRedisplay = false;
}

void maintenanceDisplay(){
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("MAINTENANCE");
  needRedisplay = false;
}

String colorDisplayFormatById(uint8_t id, bool shortF) {
  switch(id) {
    case COLOR_BLUE: return shortF ? "BLUE" : "BL:";
    case COLOR_YELLOW: return shortF ? "YELLOW" : "YL:";
    case COLOR_MAGENTA: return shortF ? "MAGENTA" : "MG:";
    case COLOR_BROWN: return shortF ? "BROWN" : "BR:";
    default: return "?";
  }
}

void orderMenu(){
  switch(orderPage){
    case 0 :
      if(key == '*'){
        selectedColor = constrain(selectedColor - 1, 0, activeColorCount-1);
        needRedisplay = true;
      }
      if(key == '#'){
        selectedColor = selectedColor = constrain(selectedColor + 1, 0, activeColorCount-1);
        needRedisplay = true;
      }
      if(needRedisplay){
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print(colorDisplayFormatById(activeColors[selectedColor], false));
        needRedisplay = false;
      }
      break;
  }
}

// Interruptions
void switchSystem(){
  unsigned long now = millis();
  if(now - lastBtn1 > 200){
    lastBtn1 = now; 
    maintenance = !maintenance;
    needRedisplay = true;
  }
}

void switchDisplay(){
  unsigned long now = millis();
  if(now - lastBtn2 > 200 && !maintenance){
    lastBtn2 = now;
    displayMode = !displayMode;
    needRedisplay = true;
  }
}

void setup() {
  Serial.begin(9600);
  Serial.write('R');
  for(byte i=0; i<5; i++) pinMode(pinsIR[i], INPUT_PULLUP);
  pinMode(btn1, INPUT_PULLUP);
  pinMode(btn2, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(btn1), switchSystem, FALLING);
  attachInterrupt(digitalPinToInterrupt(btn2), switchDisplay, FALLING);
  servoScan.attach(11);
  servoStock.attach(10);
  servoOrder.attach(9);
  servoScan.write(SERVO_SCAN_OFF);      
  servoStock.write(SERVO_OFF);     
  servoOrder.write(SERVO_OFF);
  updateColor();
  lcd.init();
  lcd.backlight();
  lcd.print("System ready");
}

void loop() {
  //Update des capteurs + envoie au rasberry
  if(updateIR()) sendIR();
  //Update des couleurs
  updateColor();

  key = keypad.getKey();
  if(key == 'A' && !orderMode) {
    orderMode = true;
    orderPage = 0;
    orderLineCount = 0;
    needRedisplay = true;
  }

  if(orderMode) orderMenu();

  if(!maintenance){
    //affichage current order OU completed order count
    if(needRedisplay && !orderMode) processDisplay();

    //Machine à états
    switch(step){
      case 0 : // ATTENTE BOITE
        servoScan.write(SERVO_SCAN_ON);
        if(statesIR[IR_SCAN]){ 
          objetPmul.sendScanNeeded(); 
          step = 1;
          stepEnteredAt = millis();
        }
        break;

      case 1 : // ATTENTE SCAN
        if (millis() - stepEnteredAt > 4000) {
            currentDecision = ItemDecision::NO_DECISION;
            currentItemId   = 0;
            currentOrderId  = 0;
            step = 2;
            stepEnteredAt = millis();
        } else if (objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId)) {
            step = 2;
            stepEnteredAt = millis();
        }
        break;

      case 2 : // AIGUILLAGE et LIBERATION
        if(currentDecision==ItemDecision::ORDER) servoOrder.write(SERVO_ON);
        else if(currentDecision==ItemDecision::STOCK) servoStock.write(SERVO_ON);

        if(millis() - stepEnteredAt > 500){
          servoScan.write(SERVO_SCAN_OFF);
          step = 3;
          stepEnteredAt = millis();
        }
        break;

      case 3 : // ATTENTE FIN DE PASSAGE - la boite doit quitter le capteur IR_SCAN
        if(statesIR[IR_NEXT] || millis() - stepEnteredAt > 2000){
          servoScan.write(SERVO_SCAN_ON);
          step = 4;
          stepEnteredAt = millis();
        }
        break;

      case 4 : // CONFIRMATION
        if(millis() - stepEnteredAt > 4000) {
          step = 0;
          if(currentItemId != 0){
            objetPmul.sendScanResult(currentItemId, ItemStatus::FAILED);
            servoStock.write(SERVO_OFF);
            servoOrder.write(SERVO_OFF);
          }
        } else {
          bool conf = (currentDecision==ItemDecision::ORDER && statesIR[IR_ORDER]) ||
                      (currentDecision==ItemDecision::STOCK && statesIR[IR_STOCK]) ||
                      (currentDecision==ItemDecision::PASS && statesIR[IR_PASS]) ||
                      (currentDecision==ItemDecision::NO_DECISION);
          if(conf) {
            if(currentDecision==ItemDecision::ORDER) servoOrder.write(SERVO_PUSH);
            else if(currentDecision==ItemDecision::STOCK) servoStock.write(SERVO_PUSH);

            if(currentItemId != 0) objetPmul.sendScanResult(currentItemId, ItemStatus::CONFIRMED);
            step = 5;
            stepEnteredAt = millis();
          }
        }
        break;

      case 5 : //ATTENTE SERVO
        if (millis() - stepEnteredAt > 600) {
          servoStock.write(SERVO_OFF);
          servoOrder.write(SERVO_OFF);
          step=6;
          stepEnteredAt = millis();
        }
        break;

      case 6 : //RÉPONSE PI CompletedCount
        if (objetPmul.readCompletedCount(completedOrdersCount) || millis() - stepEnteredAt > 1000) {
          needRedisplay = true;
          step = 0;
        }
        break;
    }

  }else{
    servoScan.write(SERVO_SCAN_OFF);      
    servoStock.write(SERVO_OFF);     
    servoOrder.write(SERVO_OFF); 
    if(needRedisplay && !orderMode){
      maintenanceDisplay();
    }
  }
}
