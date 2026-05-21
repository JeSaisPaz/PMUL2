/*
 * PMUL2 - Systeme de Tri Automatise
 * 
 * NOUVEAU PROCESSUS DE BLOCAGE/DEBLOCAGE & MENU ASYNCHRONE
 * =========================================
 * - Menu de commande locale non-bloquant (utilisation d'une machine à états)
 * - Plus aucun 'while' pour le clavier -> le ping et les capteurs restent réactifs
 * - Remplacement de la touche '=' (inexistante) par '#' pour valider la quantité
 */

#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include "pmul2-lib.h"

LiquidCrystal_I2C lcd(0x27, 16, 2);

uint8_t btn1 = 2;
uint8_t btn2 = 3;

volatile bool systemOn = true;
volatile bool modeAffichage = false;
volatile bool modeAffichageChanged = true; // Forcer l'affichage initial
uint16_t totalArticlesTries = 0; 

// Com Raspberry Pi via USB
Pmul2Lib objetPmul(Serial);

// Machine a etats pour le processus de tri
uint8_t etapeActu = 0;
uint8_t etapePrecedente = 255; 

// Gestion des timeouts
unsigned long tempsEntreeEtat = 0;  
const unsigned long TIMEOUT_SCAN = 5000;          
const unsigned long TIMEOUT_CONFIRMATION = 10000; 
const unsigned long TIMEOUT_ATTENTE_BOITE = 30000; 

bool previousBoxCleared = false;
bool entreeEtat2 = false;

// Capteurs IR: pin, role, confirmation associee
uint8_t pinsIR[] = {8, 7, 6, 5, 4};
bool etatsIR[] = {0, 0, 0, 0, 0};
bool etatsIRPrecedents[] = {0, 0, 0, 0, 0};
unsigned long dernierEnvoiCapteurs = 0;
const unsigned long INTERVALLE_ENVOI_CAPTEURS = 1000; 
bool capteursOntChange = false;

#define IR_NEXT    1  // pin 7 
#define IR_STOCK   2  // pin 6 
#define IR_ORDER   3  // pin 5 
#define IR_PASS    4  // pin 4 

// Servo Moteurs
Servo servoScan;      // pin 11
Servo servoStock;     // pin 10
Servo servoCommande;  // pin 9

#define SERVO_BLOQUE    0 
#define SERVO_LIBRE     10   
#define SERVO_AIGUILLAGE  0   
#define SERVO_NEUTRE    45  

// --- KEYPAD ---
const uint8_t ROWS = 4; 
const uint8_t COLS = 4; 

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

/* ATTENTION: Sur Nano/Uno, A6 et A7 sont EXCLUSIVEMENT analogiques et ne 
   marcheront pas ici. Si vous utilisez une carte Mega, cela fonctionne. */
uint8_t rowPins[ROWS] = {A3, A2, A1, A0}; 
uint8_t colPins[COLS] = {A7, A6, 13, 12}; 

Keypad customKeypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS); 

// Infos du bloc en cours de scan
uint16_t    currentItemId = 0;
ItemDecision currentDecision = ItemDecision::NO_DECISION;
uint8_t     currentOrderId  = 0;
uint8_t     currentHue = 0, currentSaturation = 0, currentValue = 0, currentTeam = 0;

// Variables pour le Menu Local Non-Bloquant
bool modeOrder = false;
uint8_t orderPage = 0; // 0=Choix Couleur, 1=Confirmation, 2=Saisie Qte
bool menuNeedsUpdate = true; // Pour ne rafraichir le LCD que quand necessaire
uint8_t tempQty = 0;
uint8_t selectedColorForQty = 0;

uint8_t activeColors[4] = {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA};
uint8_t activeColorCount = 3;

uint8_t orderLine[6][2];
uint8_t orderLineCount = 0;
uint8_t orderColors[6];
uint8_t orderQuantities[6];

// Interruptions
void basculeSystem(){
  systemOn = !systemOn;
  Serial1.println(systemOn ? "[SYS] ON" : "[SYS] OFF (maintenance)");
}

void basculeAffichage(){
  modeAffichage = !modeAffichage;
  Serial1.print("[AFF] BP2 -> mode ");
  Serial1.println(modeAffichage);
  modeAffichageChanged = true;
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  Serial.write('R');
  Serial1.println("Ready.");

  lcd.init();
  lcd.backlight();
  lcd.print("System ready");

  pinMode(btn1, INPUT_PULLUP);
  pinMode(btn2, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(btn1), basculeSystem, RISING);
  attachInterrupt(digitalPinToInterrupt(btn2), basculeAffichage, RISING);

  for (uint8_t i = 0; i < 5; i++) {
    pinMode(pinsIR[i], INPUT_PULLUP);
  }

  servoScan.attach(11);
  servoStock.attach(10);
  servoCommande.attach(9);

  servoScan.write(SERVO_BLOQUE);      
  servoStock.write(SERVO_NEUTRE);     
  servoCommande.write(SERVO_NEUTRE);  

  etapeActu = 0;
}

String colorDisplayFormatById(uint8_t colorId, bool shortFormat) {
  switch(colorId) {
    case COLOR_BLUE: return shortFormat ? "BL" : "BL:1";    
    case COLOR_YELLOW: return shortFormat ? "YL" : "YL:2";  
    case COLOR_MAGENTA: return shortFormat ? "MG" : "MG:3"; 
    case COLOR_BROWN: return shortFormat ? "BR" : "BR:4";   
    case COLOR_ORANGE: return shortFormat ? "OR" : "OR:5";  
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

bool isColorInOrder(uint8_t colorId) {
  for (uint8_t i = 0; i < orderLineCount; i++) {
    if (orderLine[i][0] == colorId) return true;
  }
  return false;
}

void loop() {
  // 1. GESTION DES CAPTEURS 

  
  capteursOntChange = false;
  for (uint8_t i = 0; i < 5; i++) {
    bool nouvelEtat = !digitalRead(pinsIR[i]); 
    if (nouvelEtat != etatsIR[i]) capteursOntChange = true;
    etatsIR[i] = nouvelEtat;
  }
  
  unsigned long maintenant = millis();
  if (capteursOntChange || (maintenant - dernierEnvoiCapteurs > INTERVALLE_ENVOI_CAPTEURS)) {
    objetPmul.sendSensorStatus(etatsIR[0], etatsIR[1], etatsIR[2], etatsIR[3], etatsIR[4]);
    dernierEnvoiCapteurs = maintenant;
    for (uint8_t i = 0; i < 5; i++) etatsIRPrecedents[i] = etatsIR[i];
  }

  // 2. LECTURE CLAVIER
  char key = customKeypad.getKey();

  // Activer modeOrder si * pressee et tri pas en cours
  if(key == '*' && !modeOrder && (etapeActu == 0 || etapeActu == 1)) {
    modeOrder = true;
    orderPage = 0;
    orderLineCount = 0; 
    menuNeedsUpdate = true;
    Serial1.println("[ORDER] Mode saisie active");
    key = 0; // On consomme la touche pour ne pas l'utiliser dans le menu
  }

  // 3. GESTION DU MENU DE COMMANDE LOCALE (MACHINE A ETATS NON BLOQUANTE)
  if(modeOrder) {
    
    // --- Page 0 : Choix de la couleur ---
    if(orderPage == 0) {
      if(menuNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0,0);
        for (uint8_t i = 0; i < activeColorCount && i < 3; i++) {
          lcd.print(colorDisplayFormatById(activeColors[i], false));
        }
        lcd.setCursor(0,1);
        if(activeColorCount >= 4) lcd.print(colorDisplayFormatById(activeColors[3], false));
        lcd.print(" #:Suite");
        menuNeedsUpdate = false;
      }

      if(key >= '1' && key <= '4') {
        uint8_t colorId = colorIndexCharToId(key);
        if(colorId != 0xFF && !isColorInOrder(colorId) && orderLineCount < 6) {
          selectedColorForQty = colorId;
          tempQty = 0;
          orderPage = 2; // On passe a la page de saisie de quantite
          menuNeedsUpdate = true;
        }
      } 
      else if(key == '#') {
        if(orderLineCount > 0) {
          orderPage = 1; // On passe a la page confirmation
          menuNeedsUpdate = true;
        }
      }
    }
    
    // --- Page 2 : Saisie de la Quantité ---
    else if(orderPage == 2) {
      if(menuNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("Qty: ");
        lcd.print(colorDisplayFormatById(selectedColorForQty, true));
        lcd.print(" 1+ 2- #OK");
        lcd.setCursor(0, 1);
        lcd.print(tempQty);
        menuNeedsUpdate = false;
      }

      if(key == '1') {
        if(tempQty < 10) tempQty++;
        menuNeedsUpdate = true;
      } 
      else if(key == '2') {
        if(tempQty > 0) tempQty--;
        menuNeedsUpdate = true;
      } 
      else if(key == '#') { // Valider la quantite
        orderLine[orderLineCount][0] = selectedColorForQty;
        orderLine[orderLineCount][1] = tempQty;
        orderLineCount++;
        orderPage = 0; // Retour choix couleurs
        menuNeedsUpdate = true;
      }
    }
    
    // --- Page 1 : Confirmation ---
    else if(orderPage == 1) {
      if(menuNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0);
        for(uint8_t i = 0; i < orderLineCount && i < 3; i++) {
          lcd.print(colorDisplayFormatById(orderLine[i][0], true));
          lcd.print(":"); lcd.print(orderLine[i][1]); lcd.print(" ");
        }
        lcd.setCursor(0, 1);
        lcd.print("C:Annul  D:Conf.");
        menuNeedsUpdate = false;
      }

      if(key == 'C') { // Annuler
        modeOrder = false;
        orderLineCount = 0;
        Serial1.println("[ORDER] Annulee");
        modeAffichageChanged = true;
      } 
      else if(key == 'D') { // Confirmer
        for(uint8_t i = 0; i < orderLineCount; i++) {
          orderColors[i] = orderLine[i][0];
          orderQuantities[i] = orderLine[i][1];
        }
        objetPmul.sendLocalOrder(orderLineCount, orderColors, orderQuantities);
        Serial1.println("[ORDER] Envoyee");
        modeOrder = false;
        orderLineCount = 0;
        
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Cmd Envoyee!");
        modeAffichageChanged = true;
      }
    }
  }

  // 4. AFFICHAGE STANDARD (Si pas dans le menu de commande)
  if(!modeOrder && modeAffichageChanged) {
    modeAffichageChanged = false;
    if(modeAffichage) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Completed Orders:");
      lcd.setCursor(0, 1);
      lcd.print(totalArticlesTries);
    } else {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Item: #")
      lcd.print(itemId);
      lcd.setCursor(0, 1);
      switch(currentDecision) {
        case ItemDecision::ORDER:
          lcd.print("TO ORDER #");
          lcd.print(orderId);
          break;
        case ItemDecision::STOCK: 
          lcd.print("TO STOCK");
          break;
        case ItemDecision::PASS:
          lcd.print("PASS");
        default:
          lcd.print("TO ?");
          break;

      }
    }
  }

  // 5. RECEPTION COULEURS DU BACKEND
  uint8_t colors[4];
  uint8_t count;
  if (objetPmul.readColorList(colors, count)) {
    activeColorCount = count;
    for (uint8_t i = 0; i < count; i++) {
      activeColors[i] = colors[i];
    }
    modeAffichageChanged = true; // Forcer refresh si changement
  }

  // 6. MACHINE A ETATS DU TRI (uniquement si le système est actif et hors menu)
  if(systemOn && !modeOrder) {
    
    if (etapeActu != etapePrecedente) {
      tempsEntreeEtat = millis();
      if (etapeActu == 4) previousBoxCleared = false;  
      if (etapeActu == 2) entreeEtat2 = false;
      etapePrecedente = etapeActu;
    }
    
    switch(etapeActu) {
      
      case 0: // ATTENTE BOITE
        servoScan.write(SERVO_BLOQUE);
        if(etatsIR[IR_NEXT]) {
          Serial1.println("[ETAT 0] Boite detectee - demande scan");
          objetPmul.sendScanNeeded();
          etapeActu = 1; 
        }
        if (millis() - tempsEntreeEtat > TIMEOUT_ATTENTE_BOITE) {
          tempsEntreeEtat = millis(); 
        }
        break;
      
      case 1: // ATTENTE SCAN
        if (millis() - tempsEntreeEtat > TIMEOUT_SCAN) {
          Serial1.println("[TIMEOUT] Scan non recu, passage en PASS");
          currentDecision = ItemDecision::PASS;
          currentItemId = 0; currentOrderId = 0;
          etapeActu = 2;
          break;
        }
        if(objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId, currentHue, currentSaturation, currentValue, currentTeam)) {
          Serial1.print("[SCAN OK] Item #");
          Serial1.print(currentItemId);
          Serial1.print(" Decision: ");
          Serial1.println((int)currentDecision);
          etapeActu = 2;
        }
        break;
      
      case 2: // AIGUILLAGE
        if (!entreeEtat2) {
          entreeEtat2 = true; 
          Serial1.print("[ETAT 2] Aiguillage: ");
          switch (currentDecision) {
            case ItemDecision::ORDER:
              Serial1.println("ORDER");
              servoCommande.write(SERVO_AIGUILLAGE);  
              break;
            case ItemDecision::STOCK:
              Serial1.println("STOCK");
              servoStock.write(SERVO_AIGUILLAGE);     
              break;
            case ItemDecision::PASS:
            default:
              Serial1.println("PASS");
              break;
          }
        }
        etapeActu = 3; 
        break;
      
      case 3: // LIBERATION
        Serial1.println("[ETAT 3] Liberation");
        servoScan.write(SERVO_LIBRE);  
        etapeActu = 4; 
        break;
      
      case 4: // ATTENTE PROCHAINE BOITE
        if(!previousBoxCleared && !etatsIR[IR_NEXT]) {
          previousBoxCleared = true;  
          Serial1.println("[ETAT 4] Boite actuelle sortie");
        }
        if(previousBoxCleared && etatsIR[IR_NEXT]) {
          servoScan.write(SERVO_BLOQUE);
          Serial1.println("[ETAT 4] Nouvelle boite - BLOCAGE");
          etapeActu = 5; 
        }
        break;
      
      case 5: // CONFIRMATION
        if (millis() - tempsEntreeEtat > TIMEOUT_CONFIRMATION) {
          Serial1.println("[TIMEOUT] Confirmation");
          servoStock.write(SERVO_NEUTRE);
          servoCommande.write(SERVO_NEUTRE);
          currentDecision = ItemDecision::NO_DECISION;  
          etapeActu = 0;  
          break;
        }
        
        bool confirmed = false;
        switch(currentDecision) {
          case ItemDecision::ORDER:
            if(etatsIR[IR_ORDER]) { servoCommande.write(SERVO_NEUTRE); confirmed = true; }
            break;
          case ItemDecision::STOCK:
            if(etatsIR[IR_STOCK]) { servoStock.write(SERVO_NEUTRE); confirmed = true; }
            break;
          case ItemDecision::PASS:
            if(etatsIR[IR_PASS]) { confirmed = true; }
            break;
          default:
            confirmed = true;
            break;
        }
        
        if(confirmed) {
          totalArticlesTries++;
          currentDecision = ItemDecision::NO_DECISION;  
          etapeActu = 0;  
        }
        break;
    }
  }
  else if (!systemOn) {
    // Systeme en maintenance
    servoScan.write(SERVO_BLOQUE);
    servoStock.write(SERVO_NEUTRE);
    servoCommande.write(SERVO_NEUTRE);
    etapeActu = 0;
  }
}