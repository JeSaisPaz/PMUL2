/*
 * TEST 06 — Machine d'etat (State Machine)
 * Teste les 4 etapes du cycle de tri (0=Attente, 1=Scan, 2=Liberation, 3=Confirmation)
 * en simulant les capteurs IR et le backend via le Serial Monitor.
 *
 * Branchements : aucun materiel requis sauf l'Arduino.
 * Les capteurs sont simules par des commandes Serial.
 *
 * Commandes Serial (115200 bauds) :
 *   s = simuler IR_SCAN ON (bloc arrive)
 *   x = simuler IR_SCAN OFF (bloc parti)
 *   o = simuler IR_ORDER ON (confirmation commande)
 *   k = simuler IR_STOCK ON (confirmation stock)
 *   p = simuler IR_PASS ON (confirmation passe)
 *   r = reset (forcer etape 0)
 *   i = injecter reponse ITEM_INFO (simule le Pi)
 *
 * Log : Serial1 (pins 18/19 TX1/RX1) a 115200 bauds
 */

// capteurs simules
bool simIR[5] = {false, false, false, false, false};

#define IR_SCAN  0
#define IR_NEXT  1
#define IR_STOCK 2
#define IR_ORDER 3
#define IR_PASS  4

// simule les decisions
enum Decision { PASS = 0, ORDER = 1, STOCK = 2 };
Decision decisionSimulee = PASS;
bool decisionRecue = false;

byte etape = 0;
unsigned long tempsEtape = 0;

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  Serial.println(F("\n=== TEST 06 — State Machine ==="));
  Serial.println(F("Commandes : s=bloc arrive, x=bloc parti,"));
  Serial.println(F("  o=ORDER IR, k=STOCK IR, p=PASS IR,"));
  Serial.println(F("  r=reset, 1/2/3=forcer decision PASS/ORDER/STOCK\n"));

  Serial1.println(F("[TEST06] State machine test start"));
}

void loop() {
  // lecture des commandes
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 's': simIR[IR_SCAN] = true;  Serial.println(F("-> IR_SCAN ON")); break;
      case 'x': simIR[IR_SCAN] = false; Serial.println(F("-> IR_SCAN OFF")); break;
      case 'o': simIR[IR_ORDER] = true; Serial.println(F("-> IR_ORDER ON")); break;
      case 'k': simIR[IR_STOCK] = true; Serial.println(F("-> IR_STOCK ON")); break;
      case 'p': simIR[IR_PASS] = true;  Serial.println(F("-> IR_PASS ON")); break;
      case 'r':
        etape = 0;
        for (byte i = 0; i < 5; i++) simIR[i] = false;
        decisionRecue = false;
        Serial.println(F("-> RESET"));
        break;
      case '1': decisionSimulee = PASS;  decisionRecue = true; Serial.println(F("-> Decision=PASS")); break;
      case '2': decisionSimulee = ORDER; decisionRecue = true; Serial.println(F("-> Decision=ORDER")); break;
      case '3': decisionSimulee = STOCK; decisionRecue = true; Serial.println(F("-> Decision=STOCK")); break;
    }
  }

  // log changement d'etape
  static byte lastEtape = 0xFF;
  if (etape != lastEtape) {
    const char* noms[] = {"Attente", "Scan", "Liberation", "Confirmation"};
    Serial.print(F("[ETAPE] "));
    Serial.println(noms[etape]);
    Serial1.print(F("[ETAPE] "));
    Serial1.println(noms[etape]);
    lastEtape = etape;
  }

  unsigned long now = millis();

  switch (etape) {

    case 0: // Attente
      if (simIR[IR_SCAN]) {
        Serial.println(F("[SCAN] bloc detecte, passage en etape 1..."));
        etape = 1;
        tempsEtape = now;
      }
      break;

    case 1: { // Scan — demande info au Pi (simule ici)
      if (!decisionRecue) {
        if (now - tempsEtape > 1500) {
          Serial.println(F("[SCAN] timeout — on simule PASS par defaut"));
          decisionSimulee = PASS;
          decisionRecue = true;
        }
      }

      if (decisionRecue) {
        Serial.print(F("[SCAN] decision="));
        Serial.println(decisionSimulee == ORDER ? "ORDER" : (decisionSimulee == STOCK ? "STOCK" : "PASS"));
        decisionRecue = false;
        tempsEtape = now;
        etape = 2;
      }
      break;
    }

    case 2: // Liberation
      if (now - tempsEtape >= 500) {
        // relache le servo
        Serial.println(F("[RELACHE] servo scan -> 0"));

        if (!simIR[IR_SCAN]) {
          Serial.println(F("[RELACHE] bloc parti, passage en etape 3"));
          tempsEtape = now;
          etape = 3;
        } else if (now - tempsEtape > 3500) {
          Serial.println(F("[WARN] bloc coince apres relachement, retour attente"));
          etape = 0;
        }
      }
      break;

    case 3: { // Confirmation
      if (now - tempsEtape > 5000) {
        Serial.println(F("[WARN] timeout confirmation, retour attente"));
        etape = 0;
        break;
      }

      bool confirmed = false;
      const char* label = "";
      switch (decisionSimulee) {
        case ORDER: confirmed = simIR[IR_ORDER]; label = "ORDER"; break;
        case STOCK: confirmed = simIR[IR_STOCK]; label = "STOCK"; break;
        default:    confirmed = simIR[IR_PASS];  label = "PASS";  break;
      }

      if (confirmed) {
        Serial.print(F("[SORTIE] confirme par IR_"));
        Serial.println(label);
        // reset les IR de confirmation
        for (byte i = 2; i < 5; i++) simIR[i] = false;
        etape = 0;
      }
      break;
    }
  }

  delay(10);
}
