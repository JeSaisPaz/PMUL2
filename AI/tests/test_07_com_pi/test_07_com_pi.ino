/*
 * TEST 07 — Communication Serie (protocole Pi <-> Arduino)
 * Teste l'envoi et la reception de tous les types de paquets.
 *
 * Necessite la librairie pmul2-lib installee.
 *
 * Branchements :
 *   USB vers PC/Pi pour le protocole (Serial)
 *   Serial1 (TX1=18, RX1=19) pour les logs a 115200 bauds
 *
 * Commandes via Serial Monitor (9600 bauds) :
 *   r = sendReady
 *   b = sendBusy
 *   d = sendOrderDone
 *   n = sendScanNeeded
 *   t = sendScanResult (itemId=42, CONFIRMED)
 *   f = sendScanResult (itemId=42, FAILED)
 *   s = sendSensorStatus (IR mask 0x15 = tous sauf NEXT)
 *   c = sendLocalOrder (3 lignes test)
 *   p = handlePing (repond PONG si ping recu)
 *   l = readColorList
 *   m = readCompletedCount
 *   i = readItemInfo
 *   v = version
 *
 * Les paquets entrants sont automatiquement affiches sur Serial1.
 */

#include "pmul2-lib.h"

Pmul2Lib objetPmul(Serial);

void setup() {
  Serial.begin(9600);
  Serial1.begin(115200);
  Serial1.println(F("\n=== TEST 07 — Communication Serie ==="));
  Serial1.println(F("En attente de commandes sur USB..."));
  Serial1.println(F("Commandes : r b d n t f s c p l m i v"));

  objetPmul.sendReady();
}

void loop() {
  // repond aux pings
  objetPmul.handlePing();

  // recoit item info
  {
    uint16_t itemId;
    ItemDecision decision;
    uint8_t orderId, hue, sat, val, team;
    if (objetPmul.readItemInfo(itemId, decision, orderId, hue, sat, val, team)) {
      Serial1.print(F("[RX ITEM_INFO] id="));
      Serial1.print(itemId);
      Serial1.print(F(" dec="));
      Serial1.print(static_cast<int>(decision));
      Serial1.print(F(" order="));
      Serial1.print(orderId);
      Serial1.print(F(" H="));
      Serial1.print(hue);
      Serial1.print(F(" S="));
      Serial1.print(sat);
      Serial1.print(F(" V="));
      Serial1.print(val);
      Serial1.print(F(" T="));
      Serial1.println(team);
    }
  }

  // recoit liste couleurs
  {
    uint8_t colors[4];
    uint8_t count;
    if (objetPmul.readColorList(colors, count)) {
      Serial1.print(F("[RX COLOR_LIST] count="));
      Serial1.print(count);
      Serial1.print(F(" colors=["));
      for (uint8_t i = 0; i < count; i++) {
        if (i > 0) Serial1.print(',');
        Serial1.print(colors[i], HEX);
      }
      Serial1.println(']');
    }
  }

  // recoit completed count
  {
    uint16_t count;
    if (objetPmul.readCompletedCount(count)) {
      Serial1.print(F("[RX COMPLETED_COUNT] "));
      Serial1.println(count);
    }
  }

  // commandes clavier
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'r':
        objetPmul.sendReady();
        Serial1.println(F("[TX] sendReady"));
        break;
      case 'b':
        objetPmul.sendBusy();
        Serial1.println(F("[TX] sendBusy"));
        break;
      case 'd':
        objetPmul.sendOrderDone();
        Serial1.println(F("[TX] sendOrderDone"));
        break;
      case 'n':
        objetPmul.sendScanNeeded();
        Serial1.println(F("[TX] sendScanNeeded"));
        break;
      case 't':
        objetPmul.sendScanResult(42, ItemStatus::CONFIRMED);
        Serial1.println(F("[TX] sendScanResult id=42 CONFIRMED"));
        break;
      case 'f':
        objetPmul.sendScanResult(42, ItemStatus::FAILED);
        Serial1.println(F("[TX] sendScanResult id=42 FAILED"));
        break;
      case 's': {
        objetPmul.sendSensorStatus(1, 0, 1, 1, 1);
        Serial1.println(F("[TX] sendSensorStatus mask=0x1D"));
        break;
      }
      case 'c': {
        uint8_t colors[] = {0x02, 0x01, 0x03};  // BLUE, YELLOW, MAGENTA
        uint8_t qtys[]   = {2, 5, 1};
        objetPmul.sendLocalOrder(3, colors, qtys);
        Serial1.println(F("[TX] sendLocalOrder B2 J5 M1"));
        break;
      }
      case 'p':
        if (objetPmul.handlePing()) {
          Serial1.println(F("[PING] recu + PONG envoye"));
        } else {
          Serial1.println(F("[PING] rien en attente"));
        }
        break;
      case 'l':
        Serial1.println(F("[INFO] readColorList — en attente de paquet PID_COLOR_LIST..."));
        break;
      case 'm':
        Serial1.println(F("[INFO] readCompletedCount — en attente de paquet PID_COMPLETED_COUNT..."));
        break;
      case 'i':
        Serial1.println(F("[INFO] readItemInfo — en attente de paquet PID_ITEM_INFO..."));
        break;
      case 'v':
        objetPmul.version();
        break;
      default:
        break;
    }
  }
}
