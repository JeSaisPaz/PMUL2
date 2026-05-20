/*
 * TEST 03 — Servo Moteurs
 * Controle individuel des 3 servos avec cycle angle -> maintien 10s -> retour 0.
 *
 * Branchements (identiques a final.ino) :
 *   servoScan      -> pin 11  (bloque/debloque l'actionneur)
 *   servoStock      -> pin 10  (aiguillage stock)
 *   servoCommande   -> pin 9   (aiguillage commande)
 *
 * DEUX MODES D'UTILISATION (Serial, 115200 bauds) :
 *
 *   Mode guide (recommande) :
 *     Tapez 's' pour lancer le prompt interactif :
 *       -> "Choisir servo (a=Scan, b=Stock, c=Commande) :"
 *       -> "Angle (0-180) :"
 *       Le servo bouge, attend 10s, puis retourne a 0.
 *
 *   Mode rapide :
 *     a<angle>  ex: a45  -> Scan a 45°, attend 10s, retourne a 0
 *     b<angle>  ex: b90  -> Stock a 90°, attend 10s, retourne a 0
 *     c<angle>  ex: c30  -> Commande a 30°, attend 10s, retourne a 0
 *     a/b/c seul -> retour force a 0
 *     all        -> tous les servos a 0
 *     h          -> affiche l'aide
 */

#include <Servo.h>

Servo servos[3];
const byte PINS[3] = {11, 10, 9};
const char LABELS[3] = {'A', 'B', 'C'};
const char* NOMS[3] = {"Scan", "Stock", "Commande"};

enum ServoState : byte { IDLE, MOVING_TO, HOLDING, MOVING_HOME };

struct ServoData {
  ServoState state;
  byte       targetAngle;
  unsigned long holdStart;
};

ServoData data[3];

// mode guide interactif
enum PromptStep : byte { PROMPT_IDLE, PROMPT_SERVO, PROMPT_ANGLE };
PromptStep promptStep = PROMPT_SERVO;
byte promptServoIdx = 0;
bool menuShown = false;

void setup() {
  Serial.begin(115200);
  Serial.println(F("\n=== TEST 03 — Servo Moteurs ==="));
  showMenu();
  printPrompt();

  for (byte i = 0; i < 3; i++) {
    servos[i].attach(PINS[i]);
    servos[i].write(0);
    data[i].state = IDLE;
    data[i].targetAngle = 0;
  }
}

void loop() {
  handleSerial();
  updateServos();
}

void showMenu() {
  if (menuShown) return;
  menuShown = true;

  Serial.println(F("+----------------------------------+"));
  Serial.println(F("|  A) Scan     (pin 11)            |"));
  Serial.println(F("|  B) Stock    (pin 10)            |"));
  Serial.println(F("|  C) Commande (pin 9)             |"));
  Serial.println(F("|                                  |"));
  Serial.println(F("|  h = aide    all = tous a 0      |"));
  Serial.println(F("|  s = lancer le mode pas-a-pas    |"));
  Serial.println(F("+----------------------------------+"));
  Serial.println();
}

void printPrompt() {
  switch (promptStep) {
    case PROMPT_SERVO:
      Serial.print(F("Choisir servo (a=Scan, b=Stock, c=Commande) : "));
      break;
    case PROMPT_ANGLE:
      Serial.print(F("Angle pour servo "));
      Serial.print(LABELS[promptServoIdx]);
      Serial.print(F(" ("));
      Serial.print(NOMS[promptServoIdx]);
      Serial.print(F(") [0-180] : "));
      break;
    case PROMPT_IDLE:
      break;
  }
}

// --- Serial ---

char cmdBuf[8];
byte cmdLen = 0;

void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (cmdLen > 0) {
        cmdBuf[cmdLen] = '\0';
        processInput();
        cmdLen = 0;
      }
      continue;
    }

    if (cmdLen < 7) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

void processInput() {
  // en mode guide, seul le prompt gere les entrees
  if (promptStep == PROMPT_SERVO) {
    handlePromptServo();
    return;
  }
  if (promptStep == PROMPT_ANGLE) {
    handlePromptAngle();
    return;
  }

  // mode commande rapide
  parseCommand();
}

void handlePromptServo() {
  char c = cmdBuf[0];
  byte idx = 0xFF;

  if (c == 'a' || c == 'A') idx = 0;
  else if (c == 'b' || c == 'B') idx = 1;
  else if (c == 'c' || c == 'C') idx = 2;

  if (idx == 0xFF) {
    Serial.print(F("! Invalide. "));
    printPrompt();
    return;
  }

  promptServoIdx = idx;
  promptStep = PROMPT_ANGLE;
  printPrompt();
}

void handlePromptAngle() {
  int angle = atoi(cmdBuf);
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;

  byte idx = promptServoIdx;

  Serial.println();
  Serial.print(F("["));
  Serial.print(NOMS[idx]);
  Serial.print(F("] -> "));
  Serial.print(angle);
  Serial.println(F(" deg (maintien 10s, puis retour 0)"));

  servos[idx].write(angle);
  data[idx].state = MOVING_TO;
  data[idx].holdStart = millis();
  data[idx].targetAngle = angle;

  // retour au menu
  promptStep = PROMPT_SERVO;
  printPrompt();
}

void parseCommand() {
  char servoChar = cmdBuf[0];

  // commande "s" = lancer le mode guide interactif
  if (servoChar == 's' && cmdLen == 1) {
    Serial.println(F("\n--- Mode guide (tapez 'q' pour quitter) ---"));
    promptStep = PROMPT_SERVO;
    printPrompt();
    return;
  }

  // commande "q" = quitter le mode guide
  if (servoChar == 'q' && cmdLen == 1) {
    promptStep = PROMPT_IDLE;
    Serial.println(F("Fin du mode guide. Commandes rapides : a<angle> b<angle> c<angle>"));
    return;
  }

  // commande "all"
  if (servoChar == 'a' && cmdBuf[1] == 'l' && cmdBuf[2] == 'l' && cmdLen == 3) {
    Serial.println(F("[ALL] retour de tous les servos a 0"));
    for (byte i = 0; i < 3; i++) {
      sendHome(i);
    }
    return;
  }

  // commande "help"
  if (servoChar == 'h') {
    menuShown = false;
    showMenu();
    return;
  }

  // identifier le servo
  byte idx = 0xFF;
  if (servoChar == 'a' || servoChar == 'A') idx = 0;
  else if (servoChar == 'b' || servoChar == 'B') idx = 1;
  else if (servoChar == 'c' || servoChar == 'C') idx = 2;

  if (idx == 0xFF) {
    Serial.print(F("[ERR] servo inconnu '"));
    Serial.print(servoChar);
    Serial.println(F("'. Utilisez a, b ou c."));
    return;
  }

  // retour a 0 immediat si pas d'angle
  if (cmdLen == 1) {
    Serial.print(F("["));
    Serial.print(NOMS[idx]);
    Serial.println(F("] retour force a 0"));
    sendHome(idx);
    return;
  }

  // parser l'angle
  int angle = atoi(&cmdBuf[1]);
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;

  Serial.print(F("["));
  Serial.print(NOMS[idx]);
  Serial.print(F("] -> "));
  Serial.print(angle);
  Serial.println(F(" deg (maintien 10s puis retour 0)"));

  servos[idx].write(angle);
  data[idx].state = MOVING_TO;
  data[idx].holdStart = millis();
  data[idx].targetAngle = angle;
}

// --- Machine d'etat par servo ---

void updateServos() {
  unsigned long now = millis();

  for (byte i = 0; i < 3; i++) {
    switch (data[i].state) {

      case IDLE:
        break;

      case MOVING_TO:
        // attend 500ms que le servo ait termine son mouvement
        if (now - data[i].holdStart > 500) {
          data[i].state = HOLDING;
          data[i].holdStart = now;
        }
        break;

      case HOLDING:
        // maintient la position 10 secondes
        if (now - data[i].holdStart >= 10000) {
          Serial.print(F("["));
          Serial.print(NOMS[i]);
          Serial.print(F("] retour a 0"));
          Serial.println();
          servos[i].write(0);
          data[i].state = IDLE;
          data[i].targetAngle = 0;
        }
        break;

      case MOVING_HOME:
        // attend que le servo revienne a 0
        if (now - data[i].holdStart > 500) {
          data[i].state = IDLE;
          data[i].targetAngle = 0;
        }
        break;
    }
  }
}

void sendHome(byte idx) {
  servos[idx].write(0);
  data[idx].state = MOVING_HOME;
  data[idx].holdStart = millis();
  data[idx].targetAngle = 0;
}
