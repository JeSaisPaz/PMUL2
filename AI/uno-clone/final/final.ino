/*
 * PMUL2 - Systeme de Tri Automatise (ARDUINO UNO)
 * 
 * Clone adapte pour Arduino Uno (pins et memoires limites).
 * 
 * PINOUT UNO:
 * ============
 * 0, 1      : Serial (USB -> Raspberry Pi)
 * 2, 3      : BP1, BP2 (boutons poussoirs, interrupt-capable)
 * 4         : IR_PASS
 * 5         : IR_ORDER
 * 6         : IR_STOCK
 * 7         : IR_NEXT
 * 8         : IR_SCAN (deprecated)
 * 9,10,11   : ServoCommande, ServoStock, ServoScan
 * 12,13     : Keypad col 2,3
 * A0-A3     : Keypad row 0-3  
 * A4,A5     : Keypad col 0,1
 * 
 * NOUVEAU PROCESSUS DE BLOCAGE/DEBLOCAGE:
 * =========================================
 * 1. Par defaut: ServoScan en position BLOQUEE (10) - les boites sont toujours bloquees
 * 2. Detection: Quand IR_NEXT detecte une boite -> demande scan au backend
 * 3. Scan: Reception de la decision (ORDER/STOCK/PASS) du backend
 * 4. Aiguillage: Configuration des servos d'aiguillage selon la decision
 * 5. Liberation: Deblocage du servoScan (0) pour laisser passer la boite
 * 6. Reblocage: Des que IR_NEXT detecte la boite suivante -> blocage immediat
 * 7. Confirmation: Attente que la boite precedente atteigne son capteur de destination
 * 8. Reset: Retour des aiguillages en position neutre, retour a l'etape 1
 * 
 * CAPTEURS IR (INPUT_PULLUP - actif LOW):
 * - IR_NEXT (pin 7): Detecte la boite suivante (une boite derriere le blocage)
 * - IR_ORDER (pin 5): Confirmation arrivee dans bac commande
 * - IR_STOCK (pin 6): Confirmation arrivee dans bac stock  
 * - IR_PASS (pin 4): Confirmation passage tout droit
 * 
 * Note: Pas de Serial1 sur Uno. Debug via LCD uniquement.
 */

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

// Machine a etats pour le processus de tri
// 0=Attente boite, 1=Attente scan, 2=Aiguillage, 3=Liberation, 4=Attente prochaine, 5=Confirmation
byte etapeActu = 0;
byte etapePrecedente = 255; // Pour detecter les changements d'etat

// Gestion des timeouts
unsigned long tempsEntreeEtat = 0;  // Moment ou on entre dans un etat
const unsigned long TIMEOUT_SCAN = 5000;        // 5 secondes pour recevoir le scan
const unsigned long TIMEOUT_CONFIRMATION = 10000; // 10 secondes pour confirmation
const unsigned long TIMEOUT_ATTENTE_BOITE = 30000; // 30 secondes d'attente max pour une boite

// Variables pour l'etat 4 (plus de static!)
bool previousBoxCleared = false;

// Capteurs IR: pin, role, confirmation associee
// IMPORTANT: Avec INPUT_PULLUP, LOW = objet detecte, HIGH = pas d'objet
byte pinsIR[] = {8, 7, 6, 5, 4};
bool etatsIR[] = {0, 0, 0, 0, 0};

// Optimisation envoi capteurs
bool etatsIRPrecedents[] = {0, 0, 0, 0, 0};
unsigned long dernierEnvoiCapteurs = 0;
const unsigned long INTERVALLE_ENVOI_CAPTEURS = 1000; // Envoi force toutes les secondes
bool capteursOntChange = false;

#define IR_SCAN    0  // pin 8 - DEPRECATED (sera retire)
#define IR_NEXT    1  // pin 7 - detecte la boite suivante (une boite derriere)
#define IR_STOCK   2  // pin 6 - confirmation stock
#define IR_ORDER   3  // pin 5 - confirmation commande
#define IR_PASS    4  // pin 4 - confirmation autre (passe tout droit)

// Servo Moteur
Servo servoScan;      // Servo de blocage/deblocage (pin 11)
Servo servoStock;     // Servo aiguillage stock (pin 10)
Servo servoCommande;  // Servo aiguillage commande (pin 9)

// Positions servo blocage
#define SERVO_BLOQUE    10  // Position bloquee (empeche les boites de passer)
#define SERVO_LIBRE     0   // Position libre (laisse passer les boites)

// Positions servo aiguillage
#define SERVO_AIGUILLE  0   // Position pour devier
#define SERVO_NEUTRE    45  // Position neutre/repos

unsigned long tempsActuel = 0;
unsigned long tempsDepart = 0;
long attenteServo = 500;

// keypad
const byte ROWS = 4; 
const byte COLS = 4; 

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// connexions des broches (evite les pins IR et servo)
byte rowPins[ROWS] = {A0, A1, A2, A3}; // Lignes sur A0-A3
byte colPins[COLS] = {A4, A5, 12, 13}; // Colonnes sur A4,A5,12,13

// initialisation du clavier
Keypad customKeypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS); 

// infos du bloc en cours de scan
uint16_t    currentItemId = 0;
ItemDecision currentDecision = ItemDecision::NO_DECISION;
uint8_t     currentOrderId  = 0;
uint8_t     currentHue = 0, currentSaturation = 0, currentValue = 0, currentTeam = 0;

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
}

void basculeAffichage(){
  modeAffichage = (modeAffichage+1)%2;
}

void setup() {
  Serial.begin(9600);

  // dit au Pi qu'on est pret AVANT lcd.init() qui peut bloquer
  Serial.write('R');

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

  // Initialisation: blocage actif, aiguillages neutres
  servoScan.write(SERVO_BLOQUE);      // Bloque les boites par defaut
  servoStock.write(SERVO_NEUTRE);     // Position neutre
  servoCommande.write(SERVO_NEUTRE);  // Position neutre

  etapeActu = 0;

}

void loop() {

  // 1. Lecture des capteurs IR
  // INPUT_PULLUP: LOW = detecte, HIGH = rien
  capteursOntChange = false;
  for (byte i = 0; i < 5; i++) {
    bool nouvelEtat = !digitalRead(pinsIR[i]); // Inverse pour avoir true = detecte
    if (nouvelEtat != etatsIR[i]) {
      capteursOntChange = true;
    }
    etatsIR[i] = nouvelEtat;
  }
  
  // Envoi status capteurs au backend seulement si changement ou timeout
  unsigned long maintenant = millis();
  if (capteursOntChange || (maintenant - dernierEnvoiCapteurs > INTERVALLE_ENVOI_CAPTEURS)) {
    objetPmul.sendSensorStatus(etatsIR[0], etatsIR[1], etatsIR[2], etatsIR[3], etatsIR[4]);
    dernierEnvoiCapteurs = maintenant;
    
    // Mise a jour pour la prochaine comparaison
    for (byte i = 0; i < 5; i++) {
      etatsIRPrecedents[i] = etatsIR[i];
    }
  }

  // Debug ping
  objetPmul.handlePing();

  // 2. Reception des couleurs actives du backend
  {
    uint8_t colors[4];
    uint8_t count;
    if (objetPmul.readColorList(colors, count)) {
      activeColorCount = count;
      for (uint8_t i = 0; i < count; i++) {
        activeColors[i] = colors[i];
      }
    }
  }

  // Detection changement d'etat pour reset timer et variables
  if (etapeActu != etapePrecedente) {
    tempsEntreeEtat = millis();
    
    // Reset des variables specifiques a certains etats
    if (etapeActu == 4) {
      previousBoxCleared = false;  // Reset pour l'etat 4
    }
    
    etapePrecedente = etapeActu;
  }

  // 3. Machine a etats principale (uniquement si systeme actif)
  if(systemOn) {
    // Affichage LCD selon le mode
    if(modeAffichage == 0) {
      lcd.setCursor(0, 0);
      lcd.print("Current Order ID:");
      // Afficher les details de la commande en cours
      lcd.print(currentItemId);
      
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Completed Orders:");
      lcd.setCursor(0, 1);
      lcd.print(totalArticlesTries);
      lcd.print("    ");
    }
    
    switch(etapeActu) {
      
      // ====== ETAT 0: ATTENTE BOITE ======
      // Blocage actif, attend qu'une boite arrive a IR_NEXT
      case 0: {
        // S'assurer que le blocage est actif
        servoScan.write(SERVO_BLOQUE);
        
        // Si une boite est detectee a IR_NEXT
        if(etatsIR[IR_NEXT]) {
          // Demander le scan au backend
          objetPmul.sendScanNeeded();
          etapeActu = 1; // Passer a l'attente du scan
        }
        break;
      }
      
      // ====== ETAT 1: ATTENTE SCAN ======
      // Attend la reponse du backend avec la decision
      case 1: {
        // Verifier le timeout
        if (millis() - tempsEntreeEtat > TIMEOUT_SCAN) {
          // Timeout! Le backend n'a pas repondu
          
          // Decision par defaut: laisser passer
          currentDecision = ItemDecision::PASS;
          currentItemId = 0;
          currentOrderId = 0;
          
          // Passer directement a l'aiguillage
          etapeActu = 2;
          break;
        }
        
        // Essayer de lire les infos depuis le backend
        if(objetPmul.readItemInfo(currentItemId, currentDecision, currentOrderId, currentHue, currentSaturation, currentValue, currentTeam)) {
          // Scan recu, passer a l'aiguillage
          etapeActu = 2;
        }
        break;
      }
      
      // ====== ETAT 2: AIGUILLAGE ======
      // Configure les servos d'aiguillage selon la decision
      case 2: {
        switch (currentDecision) {
          case ItemDecision::ORDER:
            servoCommande.write(SERVO_AIGUILLE);  // Devier vers commande
            break;
          case ItemDecision::STOCK:
            servoStock.write(SERVO_AIGUILLE);     // Devier vers stock
            break;
          case ItemDecision::PASS:
          default:
            // Pas d'aiguillage, la boite passe tout droit
            break;
        }
        
        // Petit delai pour laisser les servos bouger
        delay(100);
        etapeActu = 3; // Passer a la liberation
        break;
      }
      
      // ====== ETAT 3: LIBERATION ======
      // Debloque pour laisser passer la boite
      case 3: {
        servoScan.write(SERVO_LIBRE);  // Debloquer
        etapeActu = 4; // Passer a l'attente de la prochaine boite
        break;
      }
      
      // ====== ETAT 4: ATTENTE PROCHAINE BOITE ======
      // Des que IR_NEXT detecte la boite suivante, on rebloque immediatement
      case 4: {
        // Note: previousBoxCleared est maintenant une variable globale, 
        // reset a false quand on entre dans cet etat (voir debut du loop)
        
        // D'abord, attendre que la boite actuelle quitte IR_NEXT
        if(!previousBoxCleared && !etatsIR[IR_NEXT]) {
          previousBoxCleared = true;  // La boite actuelle est partie
        }
        
        // Ensuite, detecter l'arrivee de la prochaine boite
        if(previousBoxCleared && etatsIR[IR_NEXT]) {
          // Rebloquer immediatement pour arreter les boites suivantes
          servoScan.write(SERVO_BLOQUE);
          etapeActu = 5; // Passer a la confirmation
        }
        // Note: Si pas de boite suivante, on reste en etat 4 (debloque)
        // jusqu'a ce qu'une boite arrive
        break;
      }
      
      // ====== ETAT 5: CONFIRMATION ======
      // Attend que la boite precedente atteigne son capteur de confirmation
      // puis remet les aiguillages en position neutre
      case 5: {
        // Verifier le timeout
        if (millis() - tempsEntreeEtat > TIMEOUT_CONFIRMATION) {
          // Timeout! La boite n'a pas atteint le capteur de confirmation
          
          // Remettre les aiguillages en position neutre quand meme
          servoStock.write(SERVO_NEUTRE);
          servoCommande.write(SERVO_NEUTRE);
          
          // Compter comme un echec mais continuer
          totalArticlesTries++;  // On compte quand meme
          currentDecision = ItemDecision::NO_DECISION;  // Reset decision
          etapeActu = 0;  // Retour a l'attente
          break;
        }
        
        bool confirmed = false;
        
        switch(currentDecision) {
          case ItemDecision::ORDER:
            if(etatsIR[IR_ORDER]) {
              servoCommande.write(SERVO_NEUTRE);  // Remettre en position neutre
              confirmed = true;
            }
            break;
            
          case ItemDecision::STOCK:
            if(etatsIR[IR_STOCK]) {
              servoStock.write(SERVO_NEUTRE);     // Remettre en position neutre
              confirmed = true;
            }
            break;
            
          case ItemDecision::PASS:
            if(etatsIR[IR_PASS]) {
              confirmed = true;
            }
            break;
            
          default:
            // Si pas de decision, considerer comme confirme
            confirmed = true;
            break;
        }
        
        // Si confirmation recue, retour a l'etat initial
        if(confirmed) {
          totalArticlesTries++;
          currentDecision = ItemDecision::NO_DECISION;  // Reset decision
          etapeActu = 0;  // Retour a l'attente
          
          // Note: Le blocage est deja actif depuis l'etat 4
          // La prochaine boite est deja en attente a IR_NEXT
        }
        break;
      }
    }
  }
  else {
    // Systeme en pause/maintenance
    // S'assurer que tout est bloque et en position neutre
    servoScan.write(SERVO_BLOQUE);
    servoStock.write(SERVO_NEUTRE);
    servoCommande.write(SERVO_NEUTRE);
    etapeActu = 0;
  }
}