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
char keys[ROWS][COLS] = {
                          {'1','2','3','A'},
                          {'4','5','6','B'},
                          {'7','8','9','C'},
                          {'*','0','#','D'}
                        };
uint8_t rowPins[ROWS] = {31, 33, 35, 37}, colPins[COLS] = {39, 41, 43, 45}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
char key = 0;

//Process
uint8_t currentOrderActiveId = 0;
uint8_t currentOrderQuantities[5] = {0, 0, 0, 0, 0};
uint16_t completedOrdersCount = 0;
uint8_t orderLineCount;
ItemDecision currentDecision = ItemDecision::NO_DECISION;
uint16_t currentItemId = 0;
uint8_t currentOrderId = 0;
uint8_t step = 0;
uint8_t orderPage = 0;
uint8_t activeColors[4] = {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA, COLOR_BROWN}, activeColorCount = 4;
uint8_t colorQuantities[5];

unsigned long stepEnteredAt = 0, sentAt = 0, lastSendIr = 0;

void resetQuantities() {
  for (uint8_t i = 0; i < sizeof(colorQuantities); i++) {
    colorQuantities[i] = 0;
  }
}

void updateCurrentOrder(){
  uint8_t tempId;
  uint8_t tempQtys[4];
  
  if (objetPmul.readCurrentOrder(tempId, tempQtys)) {
    currentOrderActiveId = tempId;
    currentOrderQuantities[COLOR_YELLOW]  = tempQtys[0];
    currentOrderQuantities[COLOR_BLUE]    = tempQtys[1];
    currentOrderQuantities[COLOR_MAGENTA] = tempQtys[2];
    currentOrderQuantities[COLOR_BROWN]   = tempQtys[3];
    
    needRedisplay = true;
  }
}

void updateIr() {
  for (byte i = 0; i < 5; i++) statesIR[i] = !digitalRead(pinsIR[i]);
}

void updateColor(){
  uint8_t colors[4], count;
  if (objetPmul.readColorList(colors, count)) {
    activeColorCount = count;
    for (uint8_t i = 0; i < count; i++) activeColors[i] = colors[i];
    needRedisplay = true;

      //reset de la commande en cours
      resetQuantities();
      selectedColor = 0;
      orderMode = false;
      orderPage = 0;
  }
}

void sendIr(){
  if(millis() - lastSendIr > 500){
    objetPmul.sendSensorStatus(statesIR[0], statesIR[1], statesIR[2], statesIR[3], statesIR[4]);
    lastSendIr = millis();
  }
}

void processDisplay(){
  lcd.clear();
  lcd.setCursor(0,0);
  
  if(step > 1){
    lcd.print("Current item :");
    lcd.setCursor(0,1);
    lcd.print("#");
    lcd.print(currentItemId);
    if(currentDecision==ItemDecision::ORDER){
      lcd.print(" ORDER #");
      lcd.print(currentOrderId);
    }
    if(currentDecision==ItemDecision::STOCK) lcd.print(" STOCK");
    if(currentDecision==ItemDecision::PASS) lcd.print(" PASS");
  }
  else if(displayMode){
    if (currentOrderActiveId == 0) {
      lcd.print("No active order");
    } else {
      lcd.print("Order #");
      lcd.print(currentOrderActiveId);
      lcd.setCursor(0, 1);
      for(uint8_t i = 0; i < activeColorCount; i++){
        uint8_t currentColorId = activeColors[i];
        if(currentOrderQuantities[currentColorId] > 0){
          lcd.print(colorDisplayFormatById(currentColorId, true));
          lcd.print(currentOrderQuantities[currentColorId]);
        }
      }
    }
  }
  else {
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
    case COLOR_BLUE: return shortF ? "BL:" : "BLUE:";
    case COLOR_YELLOW: return shortF ? "YL:" : "YELLOW:";
    case COLOR_MAGENTA: return shortF ? "MG:" : "MAGENTA:";
    case COLOR_BROWN: return shortF ? "BR:" : "BROWN:";
    default: return "?";
  }
}

void orderMenu(){
    if(orderPage == 0){
      if(key == 'C'){
        resetQuantities();
        selectedColor = 0;
        orderMode = false;
        orderPage = 0;
        needRedisplay = true;
        return;
      }
      if(key == 'D'){
        orderPage = 1;
        selectedColor = 0;
        needRedisplay = true;
      }
      if(key == '*'){
        selectedColor = constrain(selectedColor - 1, 0, activeColorCount-1);
        needRedisplay = true;
      }
      if(key == '#'){
        selectedColor = constrain(selectedColor + 1, 0, activeColorCount-1);
        needRedisplay = true;
      }
      uint8_t currentColorId = activeColors[selectedColor];
      if(key >= '0' && key <= '9'){
        colorQuantities[currentColorId] = key - '0'; // Convertit le caractère '0'-'9' en valeur numérique 0-9
        needRedisplay = true;
      }
      if(needRedisplay && orderPage == 0){
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print(colorDisplayFormatById(currentColorId, false));
        lcd.print(colorQuantities[currentColorId]);
        lcd.setCursor(0,1);
        lcd.print("C:Annul D:Conf.");
        needRedisplay = false;
      }
    }
    else if(orderPage == 1){
      if(key == 'C'){
        resetQuantities();
        orderMode = false;
        orderPage = 0;
        needRedisplay = true;
        return;
      }
      if(key == 'D'){
        uint8_t colorsToSend[4]; // Tableau temporaire pour les IDs des couleurs commandées
        uint8_t qtysToSend[4];   // Tableau temporaire pour les quantités correspondantes
        uint8_t lineCount = 0;   // Compteur de lignes valides à envoyer

        // On parcourt les couleurs actuellement actives du système
        for (uint8_t i = 0; i < activeColorCount; i++) {
          uint8_t colorId = activeColors[i];
          uint8_t qty = colorQuantities[colorId];

          // On n'envoie la couleur QUE si l'utilisateur a demandé au moins 1 boîte
          if (qty > 0) {
            colorsToSend[lineCount] = colorId;
            qtysToSend[lineCount] = qty;
            lineCount++; // On passe à la ligne suivante
          }
        }
        // Si au moins une couleur a une quantité > 0, on envoie la commande
        if (lineCount > 0) {
          objetPmul.sendLocalOrder(lineCount, colorsToSend, qtysToSend);
          orderPage = 2;
        }else{
          orderPage = 0;
        }
        sentAt =millis();
        resetQuantities();
        needRedisplay = true;
        return;
      }
      if(needRedisplay && orderPage == 1){
        lcd.clear();
        lcd.setCursor(0,0);
        for(uint8_t i = 0; i<activeColorCount; i++){
          uint8_t currentColorId = activeColors[i];
          if(colorQuantities[currentColorId] > 0){
            lcd.print(colorDisplayFormatById(currentColorId, true));
            lcd.print(colorQuantities[currentColorId]);
          }
        }
        lcd.setCursor(0,1);
        lcd.print("C:Annul D:Conf.");
        needRedisplay = false;
      }
    }
    else if(orderPage == 2){
      if(needRedisplay && orderPage == 2){
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("ORDER SENT");
        needRedisplay = false;
      }
      if(millis() - sentAt > 500){
        orderPage = 0;
        orderMode = false;
        needRedisplay = true;
      }
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
  Serial.begin(115200);
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
  lcd.init();
  lcd.backlight();
  lcd.print("System ready");
  delay(500);
}

void loop() {
  objetPmul.pollAll();
  if (objetPmul.readCompletedCount(completedOrdersCount)) needRedisplay = true;
  updateColor();
  updateCurrentOrder();
  //Update des capteurs + envoie au rasberry
  updateIr();
  sendIr();
  key = keypad.getKey();
  if(key == 'A' && !orderMode) {
    orderMode = true;
    orderPage = 0;
    orderLineCount = 0;
    needRedisplay = true;
  }

  if(orderMode)orderMenu();
  if (needRedisplay && !orderMode) {
    if (maintenance) {
      maintenanceDisplay();
    } else {
      processDisplay();
    }
  }

  if(!maintenance){
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
            needRedisplay = true;
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
          step = 5;
          if(currentItemId != 0){
            objetPmul.sendScanResult(currentItemId, ItemStatus::FAILED);
          }
        } else {
          bool conf = (currentDecision==ItemDecision::ORDER && statesIR[IR_ORDER]) ||
                      (currentDecision==ItemDecision::STOCK && statesIR[IR_STOCK]) ||
                      (currentDecision==ItemDecision::PASS && statesIR[IR_PASS]) ||
                      (currentDecision==ItemDecision::NO_DECISION);
          if(conf) {
            if(currentDecision==ItemDecision::ORDER) servoOrder.write(SERVO_PUSH);
            else if(currentDecision==ItemDecision::STOCK) servoStock.write(SERVO_PUSH);

            if(currentItemId != 0) {
              objetPmul.sendScanResult(currentItemId, ItemStatus::CONFIRMED);
              currentItemId = 0;
            }
            step = 5;
            stepEnteredAt = millis();
          }
        }
        break;

      case 5 : //ATTENTE SERVO
        if (millis() - stepEnteredAt > 600) {
          needRedisplay = true;
          servoStock.write(SERVO_OFF);
          servoOrder.write(SERVO_OFF);
          step=0;
        }
        break;
    }

  }else{
    servoScan.write(SERVO_SCAN_OFF);      
    servoStock.write(SERVO_OFF);     
    servoOrder.write(SERVO_OFF); 
  }
}
