#define BTN_SEND       2
#define BTN_RECEIVE    3

#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); 

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
    // Si le bouton d'envoi est pressé
    if (flagSend) {
        Serial.println("Hello from Arduino !");

        // Affichage sur le LCD comme tu le souhaitais
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Hello from");
        lcd.setCursor(0, 1);
        lcd.print("Arduino !");

        delay(200); // Anti-rebond rapide
        flagSend = false; 
    }

    // Si le bouton de lecture est pressé
    if (flagReceive) {
        if (Serial.available() > 0) {
            String payload = Serial.readStringUntil('\n');
            lcd.clear();
            lcd.print(payload); // Affiche le message du PC
        } else {
            lcd.clear();
            lcd.print("Rien a lire");
        }

        delay(200); // Anti-rebond rapide
        flagReceive = false; 
    }
}
