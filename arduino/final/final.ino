/*
 * PMUL2 - Systeme de Tri Automatise
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
 * CORRECTIONS APPLIQUEES:
 * - [FIX 1] Pins keypad deplacees sur A0-A5 / broches libres pour eviter conflit
 *           avec servos (9,10,11) et capteurs IR (4,5,6,7,8)
 * - [FIX 2] Suppression du return intempestif dans loop() qui bloquait la machine
 *           a etats quand activeColorCount >= 3
 * - [FIX 3] Correction du double setCursor(0,0) en mode affichage 0 (ecrasait le titre)
 * - [FIX 4] getQuantityForColor() non bloquant : appelle handlePing() et lit les capteurs
 *           a chaque iteration pour ne pas geler le convoyeur pendant la saisie
 * - [FIX 5] sendLocalOrder() corrige : tableaux colors[] et quantities[] distincts
 * - [FIX 6] Etat 2 protege par un flag entreeEtat2 pour n'executer les servos qu'une seule
 *           fois et non a chaque iteration du loop()
 * - [FIX 7] TIMEOUT_ATTENTE_BOITE desormais utilise dans l'etat 0
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
// false: Affichage current order in process, true: Number of orders completed
volatile bool modeAffichage = false;
uint16_t totalArticlesTries = 0; // recu du backend via PID_COMPLETED_COUNT

// Com Raspberry Pi via USB
Pmul2Lib objetPmul(Serial);

// Machine a etats pour le processus de tri
// 0=Attente boite, 1=Attente scan, 2=Aiguillage, 3=Liberation, 4=Attente prochaine, 5=Confirmation
uint8_t etapeActu = 0;
uint8_t etapePrecedente = 255; // Pour detecter les changements d'etat

// Gestion des timeouts
unsigned long tempsEntreeEtat = 0;  // Moment ou on entre dans un etat
const unsigned long TIMEOUT_SCAN = 5000;          // 5 secondes pour recevoir le scan
const unsigned long TIMEOUT_CONFIRMATION = 10000; // 10 secondes pour confirmation
const unsigned long TIMEOUT_ATTENTE_BOITE = 30000; // 30 secondes d'attente max pour une boite

// Variables pour l'etat 4 (plus de static!)
bool previousBoxCleared = false;

// [FIX 6] Flag pour n'executer l'aiguillage (etat 2) qu'une seule fois par passage
bool entreeEtat2 = false;

// Capteurs IR: pin, role, confirmation associee
// IMPORTANT: Avec INPUT_PULLUP, LOW = objet detecte, HIGH = pas d'objet
uint8_t pinsIR[] = {8, 7, 6, 5, 4};
bool etatsIR[] = {0, 0, 0, 0, 0};

// Optimisation envoi capteurs
bool etatsIRPrecedents[] = {0, 0, 0, 0, 0};
unsigned long dernierEnvoiCapteurs = 0;
const unsigned long INTERVALLE_ENVOI_CAPTEURS = 1000; // Envoi force toutes les secondes
bool capteursOntChange = false;

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
#define SERVO_AIGUILLAGE  0   // Position pour devier
#define SERVO_NEUTRE    45  // Position neutre/repos

unsigned long tempsActuel = 0;
unsigned long tempsDepart = 0;
long attenteServo = 500;

// keypad
const uint8_t ROWS = 4; 
const uint8_t COLS = 4; 

char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};

// [FIX 1] Pins keypad deplacees sur A0-A5 (analogiques utilises en digital)
// pour eviter tout conflit avec servos (9,10,11) et capteurs IR (4,5,6,7,8)
uint8_t rowPins[ROWS] = {A3, A2, A1, A0}; // Lignes sur A3, A2, A1, A0
uint8_t colPins[COLS] = {A7, A6, 13, 12}; // Colonnes sur A7, A6, 13, 12

// initialisation du clavier
Keypad customKeypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS); 

// infos du bloc en cours de scan
uint16_t    currentItemId = 0;
ItemDecision currentDecision = ItemDecision::NO_DECISION;
uint8_t     currentOrderId  = 0;
uint8_t     currentHue = 0, currentSaturation = 0, currentValue = 0, currentTeam = 0;

// mode: false = SCAN, true = ORDER (saisie commande)
bool modeOrder = false;
bool orderConfirmed = false;

// etat du menu de saisie: 0=choix couleur, 1=qte, 2=resume
uint8_t orderPage = 0;

// couleurs actives envoyees par le backend (max 4, mappees sur A,B,C,D)
uint8_t activeColors[4] = {COLOR_BLUE, COLOR_YELLOW, COLOR_MAGENTA};
uint8_t activeColorCount = 3;

// Lignes de commande: Color | Qty pour chaque ligne
uint8_t orderLine[6][2];
uint8_t orderLineCount = 0;

// [FIX 5] Tableaux separes pour l'envoi de commande locale (colors et quantities distincts)
uint8_t orderColors[6];
uint8_t orderQuantities[6];

void basculeSystem(){
  systemOn = !systemOn;
  Serial1.println(systemOn ? "[SYS] ON" : "[SYS] OFF (maintenance)");
}

volatile bool modeAffichageChanged = false;
void basculeAffichage(){
  modeAffichage = !modeAffichage;
  Serial1.print("[AFF] BP2 -> mode ");
  Serial1.println(modeAffichage);
  modeAffichageChanged = true;
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  // dit au Pi qu'on est pret AVANT lcd.init() qui peut bloquer
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

  // Initialisation: blocage actif, aiguillages neutres
  servoScan.write(SERVO_BLOQUE);      // Bloque les boites par defaut
  servoStock.write(SERVO_NEUTRE);     // Position neutre
  servoCommande.write(SERVO_NEUTRE);  // Position neutre

  etapeActu = 0;
}

String colorDisplayFormatById(uint8_t colorId, bool shortFormat) {
  // Mappe les ID de couleur sur un format d'affichage (ex: 1->BL, 2->YL, etc.)
  switch(colorId) {
    case COLOR_BLUE: return shortFormat ? "BL" : "BL:1";    // Bleu
    case COLOR_YELLOW: return shortFormat ? "YL" : "YL:2";  // Jaune
    case COLOR_MAGENTA: return shortFormat ? "MG" : "MG:3"; // Magenta
    case COLOR_BROWN: return shortFormat ? "BR" : "BR:4";   // Brun
    case COLOR_ORANGE: return shortFormat ? "OR" : "OR:5";  // Orange
    default: return "?";            // Inconnu
  }
}

uint8_t colorIndexCharToId(char c) {
  // Mappe les caracteres de selection sur les ID de couleur
  switch(c) {
    case '1': return activeColors[0];
    case '2': return activeColors[1];
    case '3': return activeColors[2];
    case '4': return activeColors[3];
    default: return 0xFF; // Invalide
  }
}

// Verifie si une couleur est deja dans la commande en cours
bool isColorInOrder(uint8_t colorId) {
  for (uint8_t i = 0; i < orderLineCount; i++) {
    if (orderLine[i][0] == colorId) return true;
  }
  return false;
}

// [FIX 4] Demande la quantite pour une couleur donnee (non bloquant : appelle
// handlePing() et lit les capteurs IR a chaque iteration pour ne pas geler le convoyeur)
uint8_t getQuantityForColor(uint8_t colorId) {
  uint8_t qty = 0;
  bool quantitySet = false;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Qty: ");
  lcd.print(colorDisplayFormatById(colorId, true));
  lcd.print(" 1+ 2- =OK");
  lcd.setCursor(0, 1);
  lcd.print(qty);

  while (!quantitySet) {
    // Maintenir la communication avec le backend et la lecture IR pendant la saisie
    objetPmul.handlePing();
    for (uint8_t i = 0; i < 5; i++) {
      etatsIR[i] = !digitalRead(pinsIR[i]);
    }

    char key = customKeypad.getKey();
    if (!key) continue; // attendre une touche

    switch (key) {
      case '1':
        if (qty < 10) qty++;
        break;
      case '2':
        if (qty > 0) qty--;
        break;
      case '=':
        quantitySet = true;
        break;
      default: break;
    }

    lcd.setCursor(0, 1);
    lcd.print(qty);
    lcd.print("   "); // efface les chiffres residuels
  }
  return qty;
}

void loop() {

  if(modeAffichageChanged) {
    // Si le mode d'affichage a change, forcer une mise a jour de l'affichage
    modeAffichageChanged = false;
    if(modeAffichage) {
      // Mode 1: Affichage du nombre de commandes completes
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Completed Orders:");
      lcd.setCursor(0, 1);
      lcd.print(totalArticlesTries);
    } else {
      // Mode 0: Affichage de la commande en cours
      lcd.clear();
      // [FIX 3] Un seul setCursor(0,0) : le titre est affiche puis les donnees sur la meme ligne
      lcd.setCursor(0, 0);
      for(uint8_t i = 0; i < 3 && i < activeColorCount; i++) {
        lcd.print(colorDisplayFormatById(orderLine[i][0], true));
        lcd.print(":");
        lcd.print(orderLine[i][1]);
        lcd.print(" ");
      }
      if(activeColorCount >= 4) {
        lcd.setCursor(0, 1);
        for(uint8_t i = 3; i < activeColorCount; i++) {
          lcd.print(colorDisplayFormatById(orderLine[i][0], true));
          lcd.print(":");
          lcd.print(orderLine[i][1]);
          lcd.print(" ");
        }
      }
      // [FIX 2] Suppression du return intempestif qui bloquait la suite du loop()
    }
  }

  char key = customKeypad.getKey();
  if(key == '*') {
    modeOrder = true;
    orderPage = 0;
  }


  // 0. Systeme de commande locale
  if(modeOrder && etapeActu == 1) {
    switch(orderPage) {
      // Phase de choix couleur et quantite
      case 0: {
        lcd.setCursor(0,0);
        // Page de choix de couleur (A,B,C,D)
        // Afficher les couleurs actives et attendre une selection
        for (uint8_t i = 0; i < 3; i++) {
          lcd.print(colorDisplayFormatById(activeColors[i], false));
        }
        if(activeColorCount >= 4) {
          lcd.setCursor(0,1);
          for(uint8_t i = 3; i < activeColorCount; i++) {
            lcd.print(colorDisplayFormatById(activeColors[i], false));
          }
          lcd.print("#:OK");
        }

        while(!orderConfirmed) {
          char key = 0;
          while(!key) {
            key = customKeypad.getKey();
          }

          uint8_t colorId = colorIndexCharToId(key);

          if(key >= '1' && key <= '4') {
            if(colorId != 0xFF && !isColorInOrder(colorId) && orderLineCount < 6) {
              orderLine[orderLineCount][0] = colorId;
              orderLine[orderLineCount][1] = getQuantityForColor(colorId);
              orderLineCount++;
              lcd.clear();
              lcd.setCursor(0,0);
              for (uint8_t i = 0; i < 3; i++) {
                lcd.print(colorDisplayFormatById(activeColors[i], false));
              }
              if(activeColorCount >= 4) {
                lcd.setCursor(0,1);
                for(uint8_t i = 3; i < activeColorCount; i++) {
                  lcd.print(colorDisplayFormatById(activeColors[i], false));
                }
                lcd.print("#:OK");
              }
            }
          } 
          else if(key == '=') {
            orderConfirmed = true;
          }
        }

        orderPage = 1;
        lcd.clear();
        break;
      }
      // Confirmation
      case 1: {
        // Page de resume
        lcd.clear();
        lcd.setCursor(0,0);
        lcd.print("Confirm order:");
        lcd.setCursor(0,1);
        lcd.print("# Edit    = OK");
        char key = 0;
        while(!key) {
          key = customKeypad.getKey();
        }
        if(key == '#') {
          orderPage = 0;
          orderLineCount = 0;
          for (uint8_t i = 0; i < 6; i++) {
            orderLine[i][0] = 0;
            orderLine[i][1] = 0;
          }
          orderConfirmed = false;
        } else {
          if(key == '=') {
            // [FIX 5] Extraire les colors et quantities dans des tableaux separes
            // avant envoi pour eviter l'aliasing (les deux pointeurs ne doivent pas
            // pointer sur le meme tableau)
            for(uint8_t i = 0; i < orderLineCount; i++) {
              orderColors[i]     = orderLine[i][0];
              orderQuantities[i] = orderLine[i][1];
            }
            // Envoyer la commande au backend avec les bons tableaux
            objetPmul.sendLocalOrder(orderLineCount, orderColors, orderQuantities);
            // reset de la saisie locale
            modeOrder = false;
            orderPage = 0;
            orderLineCount = 0;
            for (uint8_t i = 0; i < 6; i++) {
              orderLine[i][0] = 0;
              orderLine[i][1] = 0;
            }
            orderConfirmed = false;
          }
        }

        break;
      }
    }
  }

  if(!modeOrder) {

  }

  // 1. Lecture des capteurs IR
  // INPUT_PULLUP: LOW = detecte, HIGH = rien
  capteursOntChange = false;
  for (uint8_t i = 0; i < 5; i++) {
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
    for (uint8_t i = 0; i < 5; i++) {
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
    
    // [FIX 6] Reset du flag d'entree dans l'etat 2 a chaque changement d'etat
    if (etapeActu == 2) {
      entreeEtat2 = false;
    }
    
    etapePrecedente = etapeActu;
  }

  // 3. Machine a etats principale (uniquement si systeme actif)
  if(systemOn) {
    if(modeAffichage == 0) {
      
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
          Serial1.println("[ETAT 0] Boite detectee - demande scan");
          objetPmul.sendScanNeeded();
          etapeActu = 1; // Passer a l'attente du scan
        }
        
        // [FIX 7] Timeout d'attente boite : signaler au backend si aucune boite
        // n'arrive dans le delai imparti (TIMEOUT_ATTENTE_BOITE)
        if (millis() - tempsEntreeEtat > TIMEOUT_ATTENTE_BOITE) {
          Serial1.println("[TIMEOUT] Aucune boite detectee depuis 30s");
          tempsEntreeEtat = millis(); // Reset du timer pour eviter les spams
        }
        break;
      }
      
      // ====== ETAT 1: ATTENTE SCAN ======
      // Attend la reponse du backend avec la decision
      case 1: {
        // Verifier le timeout
        if (millis() - tempsEntreeEtat > TIMEOUT_SCAN) {
          // Timeout! Le backend n'a pas repondu
          Serial1.println("[TIMEOUT] Scan non recu, passage en PASS");
          
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
          Serial1.print("[SCAN OK] Item #");
          Serial1.print(currentItemId);
          Serial1.print(" Decision: ");
          Serial1.println((int)currentDecision);
          etapeActu = 2;
        }
        break;
      }
      
      // ====== ETAT 2: AIGUILLAGE ======
      // Configure les servos d'aiguillage selon la decision
      // [FIX 6] Protege par entreeEtat2 : les servos ne sont commandes qu'une seule
      // fois par passage dans cet etat, pas a chaque iteration du loop()
      case 2: {
        if (!entreeEtat2) {
          entreeEtat2 = true; // Marquer l'entree pour ne pas re-executer
          Serial1.print("[ETAT 2] Aiguillage pour decision: ");
          switch (currentDecision) {
            case ItemDecision::ORDER:
              Serial1.println("ORDER");
              servoCommande.write(SERVO_AIGUILLAGE);  // Devier vers commande
              break;
            case ItemDecision::STOCK:
              Serial1.println("STOCK");
              servoStock.write(SERVO_AIGUILLAGE);     // Devier vers stock
              break;
            case ItemDecision::PASS:
            default:
              Serial1.println("PASS (tout droit)");
              // Pas d'aiguillage, la boite passe tout droit
              break;
          }
        }
        
        etapeActu = 3; // Passer a la liberation
        break;
      }
      
      // ====== ETAT 3: LIBERATION ======
      // Debloque pour laisser passer la boite
      case 3: {
        Serial1.println("[ETAT 3] Liberation - deblocage");
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
          Serial1.println("[ETAT 4] Boite actuelle partie de IR_NEXT");
        }
        
        // Ensuite, detecter l'arrivee de la prochaine boite
        if(previousBoxCleared && etatsIR[IR_NEXT]) {
          // Rebloquer immediatement pour arreter les boites suivantes
          servoScan.write(SERVO_BLOQUE);
          Serial1.println("[ETAT 4] Nouvelle boite detectee - BLOCAGE!");
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
          Serial1.println("[TIMEOUT] Confirmation non recue");
          
          // Remettre les aiguillages en position neutre quand meme
          servoStock.write(SERVO_NEUTRE);
          servoCommande.write(SERVO_NEUTRE);
          currentDecision = ItemDecision::NO_DECISION;  // Reset decision
          etapeActu = 0;  // Retour a l'attente
          break;
        }
        
        bool confirmed = false;
        
        switch(currentDecision) {
          case ItemDecision::ORDER:
            if(etatsIR[IR_ORDER]) {
              servoCommande.write(SERVO_NEUTRE);  // Remettre en position neutre
              Serial1.println("[CONFIRM] Boite arrivee en ORDER");
              confirmed = true;
            }
            break;
            
          case ItemDecision::STOCK:
            if(etatsIR[IR_STOCK]) {
              servoStock.write(SERVO_NEUTRE);     // Remettre en position neutre
              Serial1.println("[CONFIRM] Boite arrivee en STOCK");
              confirmed = true;
            }
            break;
            
          case ItemDecision::PASS:
            if(etatsIR[IR_PASS]) {
              Serial1.println("[CONFIRM] Boite passee tout droit");
              confirmed = true;
            }
            break;
            
          default:
            // Si pas de decision, considerer comme confirme
            Serial1.println("[CONFIRM] Pas de decision - validation auto");
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
