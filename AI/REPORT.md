# PMUL2 — Rapport de tests

**Branche:** `lite-pretest`
**Date:** 14/05/2026
**Agent:** OpenCode (Claude)

---

## 1. SerialTransfer (COBS + CRC8)

**Fichier:** `AI/TestSuite/test_serial_transfer.py`
**Statut:** 9/9 OK

| Test | Resultat |
|------|----------|
| COBS encode/decode | OK |
| CRC8 table + calcul | OK (CRC(0x01,0x02,0x03)=0x44) |
| send + available (payload propre) | OK |
| send + available (payload avec START_BYTE) | OK |
| available() buffer vide | OK |
| available() donnees partielles | OK |
| packets multiples dans le buffer | OK |
| rejet payload zero-length | OK |
| rejet STOP_BYTE invalide | OK |

**Conclusion:** L'implementation Python de SerialTransfer est identique a la version C++ (Packet.cpp/PacketCRC.h). COBS stuffing/unstuffing et CRC8 polynome 0x9B sont conformes.

---

## 2. API REST

**Fichier:** `AI/TestSuite/test_api.py`
**Statut:** NON EXECUTE (backend down)

Ce test valide tous les endpoints: health, orders CRUD, neworder (validation + happy path), items, colors, scans, 404.

**A executer sur le Pi avec le backend actif:**
```bash
cd ~/PMUL2 && python3 AI/TestSuite/test_api.py
```

**Endpoints testes:**
- `GET /api/health` (200)
- `GET /api/orders` (200)
- `DELETE /api/orders/:id/delete` (404 inexistant)
- `POST /api/neworder` (400 validation: body vide, lines vide, mauvais type)
- `POST /api/neworder` (204 valide, si couleurs en DB)
- `GET /api/orders/:id/details` (200 / 404)
- `PATCH /api/orders/:id/cancel` (204)
- `DELETE /api/orders/:id/delete` (204)
- `GET /api/items` (200)
- `DELETE /api/items/:id/delete` (404 inexistant)
- `GET /api/colors` (200)
- `GET /api/scans` (200)
- `DELETE /api/scans/:id/delete` (404 inexistant)
- `POST /api/scans` (400 validation)
- `GET /api/rien_du_tout` (404)

---

## 3. Base de donnees

**Fichier:** `AI/TestSuite/test_database.js`
**Statut:** NON EXECUTE (Prisma / MariaDB non accessible)

Ce test verifie:
- Connexion Prisma + comptage de lignes par table
- Contraintes UNIQUE sur COLOR.name et COLOR.hex
- Statuts ORDER valides (IN_PROCESS, COMPLETED, CANCELLED)
- Foreign keys ITEM.COLOR_id, ORDER_LINE.ORDER_id, ORDER_LINE.COLOR_id
- Foreign keys ITEM_HISTORY.ITEM_id, SELECTION_HISTORY.ITEM_id

**A executer sur le Pi:**
```bash
cd ~/PMUL2/web/PMUL2/pmul2-team01-app && node ../../AI/TestSuite/test_database.js
```

---

## 4. End-to-End (scan complet)

**Fichier:** `AI/TestSuite/test_e2e.py`
**Statut:** NON EXECUTE (backend down)

Ce test simule un scan complet:
1. `POST /api/scans` (qrValue=TEAM01, HSV valide)
2. Verifie la reponse (itemId, decision, orderId)
3. Simule l'envoi `PID_ITEM_INFO` vers l'Arduino
4. Simule la reponse `PID_SCAN_RESULT` de l'Arduino
5. `PATCH /api/items/:id/status` (CONFIRMED)

**A executer sur le Pi:**
```bash
cd ~/PMUL2 && python3 AI/TestSuite/test_e2e.py
```

---

## 5. Architecture actuelle

### Flux scan
```
IR1 ON → Arduino bloque → PID_STATUS(SCAN_NEEDED) → Pi
  → camera: qrValue + HSV → POST /api/scans → DB cree ITEM
  → GET /api/scans → extrait {itemId, decision, orderId}
  → PID_ITEM_INFO → Arduino: decision + shunt
  → LCD "Commande #N" si ORDER
  → confirmation IR → PID_SCAN_RESULT(itemId, CONFIRMED)
  → PATCH /api/items/:id/status
```

### Flux commande locale (keypad + encodeur)
```
* → ORDER mode
  → encodeur: cycle couleurs actives (poll GET /api/colors)
  → keypad 0-9: saisie quantite
  → pressez encodeur: confirme
  → PID_LOCAL_ORDER → Pi → POST /api/neworder
```

### Protocole SerialTransfer
| PID | Direction | Nom | Payload |
|-----|-----------|-----|---------|
| 0x00 | Pi↔Arduino | PING | 1 byte |
| 0x04 | Arduino→Pi | LOCAL_ORDER | teamId + lineCount + [color+qty]*N |
| 0x05 | Pi→Arduino | COLOR_LIST | count + [colorId]*N |
| 0x10 | Pi→Arduino | ITEM_INFO | itemId(2B) + decision(1B) + orderId(1B) |
| 0x11 | Arduino→Pi | SCAN_RESULT | itemId(2B) + status(1B) |
| 0xFE | Arduino→Pi | STATUS | code (0=READY, 1=BUSY, 2=DONE, 3=SCAN_NEEDED) |

### Couleurs (IDs)
| ID | Nom | Status |
|----|-----|--------|
| 01 | Jaune | actif si HSV defini en DB |
| 02 | Bleu | idem |
| 03 | Magenta (rose) | idem |
| 04 | Vert | idem |
| 05 | Rouge | idem |
| 06 | Orange | idem |

---

## 6. Fichiers modifies (vs main)

### Arduino
- `final/final.ino` — dual-mode SCAN/ORDER, encodeur, keypad, LCD
- `pmul2-lib/src/pmul2-colors.h` — table 6 couleurs (IDs 01-06)
- `pmul2-lib/src/pmul2-com.h/cpp` — PID_LOCAL_ORDER, PID_COLOR_LIST, readColorList, sendLocalOrder
- `pmul2-lib/src/pmul2-lib.h/cpp` — expose nouvelles methodes
- `pmul2-lib/src/pmul2-keypad.h/cpp` — driver clavier 4x4
- `pmul2-lib/src/pmul2-encoder.h/cpp` — driver encodeur rotatoire

### Raspberry Pi
- `script-final/final.py` — driver: scan camera, POST /api/scans, poll colors, handle local orders
- `script-final/serial_transfer.py` — COBS+CRC8, packet IDs 0x00-0xFE
- `script-final/api.py` — exerciser endpoints API
- `script-final/diag.py` — diagnostic ping Arduino + backend

### Backend (web)
- Aucune modification

### IA (ce dossier)
- `AI/TestSuite/test_serial_transfer.py` — 9/9 OK
- `AI/TestSuite/test_api.py` — endpoints REST
- `AI/TestSuite/test_database.js` — Prisma schema FK
- `AI/TestSuite/test_e2e.py` — scan end-to-end

---

## 7. A faire sur le Pi

```bash
# 1. pull la branche
cd ~/PMUL2 && git pull origin lite-pretest

# 2. compiler + flasher l'Arduino
cp -r ~/PMUL2/arduino/pmul2-lib/src ~/PMUL2/arduino/final/
rm -f ~/PMUL2/arduino/final/src/pmul2-*.cpp
arduino-cli compile --fqbn arduino:avr:mega ~/PMUL2/arduino/final
arduino-cli upload -p /dev/ttyUSB0 --fqbn arduino:avr:mega ~/PMUL2/arduino/final

# 3. lancer les tests (backend doit tourner)
cd ~/PMUL2
python3 AI/TestSuite/test_api.py
python3 AI/TestSuite/test_e2e.py

# 4. lancer le driver
sudo python3 raspberry-pi/script-final/final.py
```
