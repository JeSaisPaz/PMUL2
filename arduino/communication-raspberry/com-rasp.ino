#define BTN_SEND       22
#define BTN_RECEIVE    23

#include               <Wire.h> 
#include               <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); 

// Interruption pour lire un message du raspberry
void readSerial() {
    lcd.clear();
    String payload = Serial.readStringUntil('\n');
    lcd.print(payload);
}

// Interruption pour envoyer un message au raspberry
void writeSerial() {
    String payload = "Hello from Arduino !";
    Serial.println(payload);
}

void setup() {
    Serial.begin(9600);
    delay(2);

    lcd.init();
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Ecran Pret !");

    pinMode(BTN_SEND, INPUT_PULLUP);
    pinMode(BTN_RECEIVE, INPUT_PULLUP);
    
    // On appelle les fonctions sans arguments ici
    attachInterrupt(digitalPinToInterrupt(BTN_SEND), writeSerial, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_RECEIVE), readSerial, FALLING);
}

void loop() {

}