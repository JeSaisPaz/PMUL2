/*
 * TEST SCAN + COMMUNICATION
 * ========================
 * Script de test minimal pour valider:
 *   1. La communication SerialTransfer Arduino <-> Pi
 *   2. La simulation d'un scan de bloc
 *   3. La réception de la réponse du backend (via Pi)
 *
 * UTILISATION:
 *   - Ouvre le Serial Monitor à 9600 baud
 *   - Appuie sur '1' pour simuler un bloc détecté
 *   - Appuie sur '2' pour simuler un ping
 *   - Appuie sur '3' pour afficher l'état des capteurs
 *   - La réponse du backend s'affiche automatiquement
 *
 * CABLAGE:
 *   - Arduino connecté au Pi via USB (Serial)
 *   - Pas besoin de capteurs IR ni servos pour ce test
 */

#include "pmul2-lib.h"

// --- Lib de com vers le Pi via USB ---
Pmul2Lib com(Serial);

// --- Variables du dernier item reçu ---
uint16_t     lastItemId    = 0;
ItemDecision lastDecision  = ItemDecision::NO_DECISION;
uint8_t      lastOrderId   = 0;
uint8_t      lastHue       = 0;
uint8_t      lastSat       = 0;
uint8_t      lastVal       = 0;
uint8_t      lastTeam      = 0;

// --- Compteur de blocs simulés ---
uint16_t scanCount = 0;

// --- Timer pour la réponse ---
unsigned long scanSentAt   = 0;
bool          waitingReply = false;
const unsigned long REPLY_TIMEOUT = 6000; // 6s pour que le Pi scan + backend réponde

void setup() {
  Serial.begin(9600);

  // Le Pi attend ce caractère pour savoir que l'Arduino est prêt
  Serial.write('R');

  // Petit délai pour laisser le Pi démarrer
  delay(500);

  Serial.println("=== TEST SCAN COMMUNICATION ===");
  Serial.println("Commandes disponibles dans ce moniteur serie:");
  Serial.println("  1 -> Simuler detection de bloc (sendScanNeeded)");
  Serial.println("  2 -> Envoyer un ping");
  Serial.println("  3 -> Envoyer etat capteurs IR (tous HIGH)");
  Serial.println("  4 -> Confirmer le dernier item comme CONFIRMED");
  Serial.println("  5 -> Confirmer le dernier item comme FAILED");
  Serial.println("================================");
}

void loop() {

  // --- Lecture des commandes depuis le Serial Monitor ---
  // IMPORTANT: on lit avec peek() pour pas interférer avec SerialTransfer
  // SerialTransfer lit aussi sur Serial, donc on doit être prudent.
  // Ici on utilise un simple check du buffer côté Arduino monitor.
  // En vrai projet, utiliser Serial1 ou Serial2 pour le debug.
  //
  // Pour ce test: les commandes '1'-'5' sont tapées AVANT que le Pi
  // commence à écouter (ou via un terminal séparé sur /dev/ttyUSB0).

  // --- Réception réponse Pi -> Arduino ---
  if (waitingReply) {
    uint16_t     itemId;
    ItemDecision decision;
    uint8_t      orderId, hue, sat, val, team;

    if (com.readItemInfo(itemId, decision, orderId, hue, sat, val, team)) {
      waitingReply = false;
      lastItemId   = itemId;
      lastDecision = decision;
      lastOrderId  = orderId;
      lastHue      = hue;
      lastSat      = sat;
      lastVal      = val;
      lastTeam     = team;

      Serial.println("\n>>> REPONSE RECUE DU BACKEND <<<");
      Serial.print("  Item ID  : #"); Serial.println(itemId);
      Serial.print("  Decision : ");
      switch(decision) {
        case ItemDecision::ORDER: Serial.println("ORDER"); break;
        case ItemDecision::STOCK: Serial.println("STOCK"); break;
        case ItemDecision::PASS:  Serial.println("PASS");  break;
        default:                  Serial.println("???");   break;
      }
      Serial.print("  Order ID : "); Serial.println(orderId);
      Serial.print("  HSV      : H="); Serial.print(hue);
      Serial.print(" S="); Serial.print(sat);
      Serial.print(" V="); Serial.println(val);
      Serial.print("  Team ID  : "); Serial.println(team);
      Serial.println(">>> FIN REPONSE <<<\n");
    }

    // Timeout si pas de réponse
    if (millis() - scanSentAt > REPLY_TIMEOUT) {
      waitingReply = false;
      Serial.println("[TIMEOUT] Pas de reponse du backend apres 6s");
      Serial.println("  -> Verifier que le Pi tourne et que le backend repond");
    }
  }

  // --- Ping automatique ---
  if (com.handlePing()) {
    Serial.println("[PING] Pong envoye au Pi");
  }

  // --- Lecture commandes clavier via Serial Monitor ---
  // NOTE: ceci ne fonctionne que si le Pi n'est pas connecté au même port.
  // En test réel, commenter ce bloc et utiliser uniquement les envois automatiques.
  if (Serial.available()) {
    char cmd = Serial.read();

    switch(cmd) {
      case '1': {
        scanCount++;
        Serial.print("\n[TEST] Simulation detection bloc #");
        Serial.println(scanCount);
        Serial.println("  -> Envoi sendScanNeeded() au Pi...");
        com.sendScanNeeded();
        scanSentAt   = millis();
        waitingReply = true;
        break;
      }

      case '2': {
        Serial.println("\n[TEST] Envoi Ping au Pi...");
        com.sendPong(); // On envoie un pong spontané pour tester la trame
        Serial.println("  -> Pong envoye (trame PID_PING)");
        break;
      }

      case '3': {
        Serial.println("\n[TEST] Envoi etat capteurs IR (tous actifs)...");
        // Simule 5 capteurs tous déclenchés
        com.sendSensorStatus(1, 1, 1, 1, 1);
        Serial.println("  -> SensorStatus envoye (mask=0x1F)");
        delay(500);
        // Puis tous éteints
        com.sendSensorStatus(0, 0, 0, 0, 0);
        Serial.println("  -> SensorStatus envoye (mask=0x00)");
        break;
      }

      case '4': {
        if (lastItemId == 0) {
          Serial.println("[ERREUR] Pas d'item en cours (fais d'abord '1')");
        } else {
          Serial.print("\n[TEST] Confirmation CONFIRMED pour item #");
          Serial.println(lastItemId);
          com.sendScanResult(lastItemId, ItemStatus::CONFIRMED);
          Serial.println("  -> ScanResult CONFIRMED envoye");
          lastItemId = 0; // reset
        }
        break;
      }

      case '5': {
        if (lastItemId == 0) {
          Serial.println("[ERREUR] Pas d'item en cours (fais d'abord '1')");
        } else {
          Serial.print("\n[TEST] Confirmation FAILED pour item #");
          Serial.println(lastItemId);
          com.sendScanResult(lastItemId, ItemStatus::FAILED);
          Serial.println("  -> ScanResult FAILED envoye");
          lastItemId = 0;
        }
        break;
      }
    }
  }

  delay(10); // Laisser respirer le loop
}
