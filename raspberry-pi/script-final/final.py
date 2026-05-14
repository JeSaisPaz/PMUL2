# Driver Raspberry Pi - pont ultra simple entre:
#   - l'Arduino (SerialTransfer via USB)
#   - la camera (QR + detection couleur)
#   - le backend (API REST)
#
# Aucune logique metier ici. Le backend decide tout.
# Le Pi fait juste passer les trames et scanner des blocs.

import serial
import signal
import sys
import time
import os
import requests
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview
from serial_transfer import SerialTransfer

# config
BAUD        = 9600
BACKEND_URL = "http://localhost:3000"

# init serie (auto-detect)
PORT_CANDIDATES = ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1",
                   "/dev/serial0", "/dev/ttyAMA0"]
port = None
for c in PORT_CANDIDATES:
    if os.path.exists(c):
        port = c
        break

if port is None:
    print("[!] Aucun port serie trouve")
    sys.exit(1)

print(f"[SERIAL] {port}")
s = serial.Serial(port, BAUD, timeout=0.5)

# attend le 'R' de l'Arduino (evite de parler au bootloader)
print("[BOOT] Attente de l'Arduino...")
t0 = time.time()
while time.time() - t0 < 15:
    if s.in_waiting and s.read(1) == b'R':
        print("[BOOT] Arduino pret !")
        break
    time.sleep(0.1)
else:
    print("[!] Arduino pas pret - verifie le flash et le cable")
    sys.exit(1)

st = SerialTransfer(s)

# init camera
cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start()
time.sleep(2)

# etat minimal
running = True

def cleanup(signum=None, frame=None):
    global running
    print("\n[!] Shutdown...")
    running = False
    cam.stop()
    s.close()
    sys.exit(0)

signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

# detection QR + couleur depuis un frame

def decodeFrame(frame_bgr):
    hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)
    qr_results = decode(frame_bgr)
    if not qr_results:
        return None, None, None, None

    obj = qr_results[0]
    h, w = frame_bgr.shape[:2]

    # patch a droite du QR
    px = min(obj.rect.left + obj.rect.width + 3, w - 15)
    py = min(obj.rect.top + (obj.rect.height // 2), h - 15)
    patch = hsv[py:py + 10, px:px + 10]

    avgHue = np.mean(patch[:, :, 0])
    avgSat = np.mean(patch[:, :, 1])
    avgVal = np.mean(patch[:, :, 2])

    # pas de match couleur local - le backend fait tout via la DB
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        return None, None, None, None

    return qr_text, int(avgHue), int(avgSat), int(avgVal)

# handlers des trames Arduino

def handleScanNeeded():
    """Un bloc est bloque par l'Arduino - on scanne et on demande l'info au backend."""
    print("[ARDUINO] Bloc en position - scan...")

    frame = cam.capture_array()
    if frame is None:
        print("  [!] Camera: pas de frame")
        return

    qr_text, hue, sat, val = decodeFrame(frame)
    if qr_text is None:
        print("  [!] Scan: QR pas detecte")
        return

    print(f"  [SCAN] QR={qr_text} H={hue} S={sat} V={val}")

    # etape 1: POST /api/scans - le backend cree l'item (retourne 204, pas de JSON)
    try:
        r = requests.post(f"{BACKEND_URL}/api/scans", json={
            "scan": {
                "qrValue": qr_text,
                "hue": hue,
                "saturation": sat,
                "value": val,
            }
        }, timeout=5)

        if r.status_code != 204:
            print(f"  [!] POST /scans a repondu HTTP {r.status_code}: {r.text}")
            return

        # etape 2: GET /api/scans - recupere le dernier scan avec son ITEM lie
        time.sleep(0.2)  # laisse le temps a la DB de commit
        r2 = requests.get(f"{BACKEND_URL}/api/scans", timeout=5)
        if r2.status_code != 200:
            print(f"  [!] GET /scans a repondu HTTP {r2.status_code}")
            return

        scans = r2.json()
        if not scans:
            print("  [!] Aucun scan dans la DB")
            return

        latest = scans[0]
        item = latest.get("ITEM")
        if not item:
            print("  [!] Dernier scan sans ITEM lie")
            return

        itemId   = item["id"]
        decision = item["decision"]  # "ORDER", "STOCK", "PASS"
        orderId  = 0
        if decision == "ORDER" and item.get("ORDER_LINE_id"):
            try:
                r3 = requests.get(f"{BACKEND_URL}/api/orders/{item['ORDER_LINE_id']}/details", timeout=5)
                # l'order_line_id n'est pas le order_id, on cherche dans les orders
                # en fait on prend le champ ORDER_id depuis l'ORDER_LINE du backend
                # pas directement accessible via l'API publique, donc on laisse 0
                # (l'Arduino peut afficher orderId=0 pour "commande inconnue")
            except:
                pass

        print(f"  [BACKEND] Item #{itemId} decision={decision} orderId={orderId}")

        # etape 3: envoie l'info a l'Arduino pour l'aiguillage
        decisionByte = {"ORDER": 0x01, "STOCK": 0x02}.get(decision, 0x00)
        payload = bytes([
            (itemId >> 8) & 0xFF,
            itemId & 0xFF,
            decisionByte,
            orderId & 0xFF,
        ])
        st.send(SerialTransfer.PID_ITEM_INFO, payload)

    except Exception as e:
        print(f"  [!] Backend injoignable: {e}")

def handleScanResult(payload):
    """L'Arduino a confirme le tri du bloc - on forward au backend."""
    if len(payload) < 3:
        return

    itemId = (payload[0] << 8) | payload[1]
    decisionStatus = "CONFIRMED" if payload[2] == 0x00 else "FAILED"

    print(f"[ARDUINO] Resultat: Item #{itemId} {decisionStatus}")

    try:
        # utilise le endpoint existant PATCH /api/items/:id/status
        r = requests.patch(f"{BACKEND_URL}/api/items/{itemId}/status", json={
            "status": {"status": decisionStatus}
        }, timeout=5)
        if r.status_code != 204:
            print(f"  [!] Backend a repondu HTTP {r.status_code}: {r.text}")
    except Exception as e:
        print(f"  [!] Backend injoignable: {e}")

def handleArduinoFrame():
    """Lit et dispatche une trame entrante de l'Arduino."""
    result = st.available()
    if result is None:
        return

    pid, payload = result

    if pid == SerialTransfer.PID_STATUS:
        if len(payload) < 1:
            return
        code = payload[0]
        if code == SerialTransfer.STATUS_SCAN_NEEDED:
            handleScanNeeded()
        elif code == SerialTransfer.STATUS_DONE:
            print("[ARDUINO] Commande terminee")
        elif code == SerialTransfer.STATUS_BUSY:
            print("[ARDUINO] Occupe")
        elif code == SerialTransfer.STATUS_READY:
            print("[ARDUINO] Dispo")

    elif pid == SerialTransfer.PID_SCAN_RESULT:
        handleScanResult(payload)

    elif pid == SerialTransfer.PID_ORDER_UPDATE:
        if len(payload) >= 4:
            print(f"[ARDUINO] Progres: team={payload[0]} B={payload[1]} Y={payload[2]} M={payload[3]}")

    elif pid == SerialTransfer.PID_PING:
        print("[ARDUINO] Ping recu (diag)")

# boucle principale

def main():
    global running
    print("[PI_DRIVER] Pret. En attente de blocs...")

    while running:
        handleArduinoFrame()
        time.sleep(0.05)


if __name__ == "__main__":
    main()
