#define BTN_SEND       22
#define BTN_RECEIVE    23

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); 

// Variables volatiles pour être accessibles dans les interruptions
volatile bool flagSend = false;
volatile bool flagReceive = false;

void isrSend() {
    flagSend = true;
}

void isrReceive() {
    flagReceive = true;
}

void setup() {
    Serial.begin(9600);
    
    lcd.init();
    lcd.backlight();
    lcd.print("Ready");

    pinMode(BTN_SEND, INPUT_PULLUP);
    pinMode(BTN_RECEIVE, INPUT_PULLUP);
    
    attachInterrupt(digitalPinToInterrupt(BTN_SEND), isrSend, FALLING);
    attachInterrupt(digitalPinToInterrupt(BTN_RECEIVE), isrReceive, FALLING);
}

void loop() {
    // Si le bouton d'envoi a été pressé
    if (flagSend) {
        Serial.println("Hello from Arduino !");
        flagSend = false; // Reset le drapeau
    }

    // Si le bouton de lecture a été pressé
    if (flagReceive) {
        if (Serial.available() > 0) {
            String payload = Serial.readStringUntil('\n');
            lcd.clear();
            lcd.print(payload);
        } else {
            lcd.clear();
            lcd.print("Rien a lire");
        }
        flagReceive = false; // Reset le drapeau
    }
}
