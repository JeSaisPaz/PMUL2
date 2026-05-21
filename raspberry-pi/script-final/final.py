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
import re
import socketio

# config
BAUD        = 9600
BACKEND_URL = "http://localhost:3000"

# init serie (auto-detect)
PORT_CANDIDATES = ["/dev/ttyUSB0", "/dev/ttyUSB1"]
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

# init camera (RGB888 = BGR en sortie, on corrige dans decodeFrame)
print("[CAM] Initialising Sensor...")
cam = Picamera2()

config = cam.create_preview_configuration()
config["main"]["size"] = (640, 480)
config["main"]["format"] = "RGB888"

cam.configure(config)
cam.start()

# delay pour que le capteur demarre
time.sleep(0.5)

# desactive l'auto white balance pour eviter que le canal bleu soit
# detruit par la camera. On lock les gains rouge/bleu manuellement
cam.set_controls({
    "AwbEnable": False,
    "ExposureTime": 9000,
    "AnalogueGain": 1.0,
    "ColourGains": (1.3, 1.7),
    "Saturation": 0.9
})

# laisse l'auto-exposure se stabiliser avec nos parametres
time.sleep(1.0)

# etat minimal
running = True

def cleanup(signum=None, frame=None):
    global running
    print("\n[!] Shutdown...")
    running = False
    cam.stop()
    s.close()
    if sio.connected:
        sio.disconnect()
    sys.exit(0)

signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

# detection QR + echantillonnage couleur avec mediane (ignore le bruit)

def decodeFrame(frame_bgr):
    # picamera2 sort du BGR, on corrige en RGB pour pyzbar et HSV
    bgr = np.ascontiguousarray(frame_bgr[:, :, :3])
    frame = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
    h, w = frame.shape[:2]

    hsv = cv2.cvtColor(frame, cv2.COLOR_RGB2HSV)

    qr_results = decode(frame)
    if not qr_results:
        return None, None, None, None

    obj = qr_results[0]
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        return None, None, None, None

    rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
    cy = ry + (rh // 2)
    patch_size = 16

    # zones d'echantillonnage a gauche et a droite du QR
    # 15 pixels de marge pour rester dans le bloc colore
    test_points = [
        (rx - 15 - patch_size, cy - (patch_size // 2)),  # gauche
        (rx + rw + 15, cy - (patch_size // 2))           # droite
    ]

    hues, sats, vals = [], [], []
    for sx, sy in test_points:
        # securite: on reste dans les limites du frame
        if (0 <= sx <= w - patch_size) and (0 <= sy <= h - patch_size):
            patch = hsv[sy:sy + patch_size, sx:sx + patch_size]
            # mediane plutot que moyenne pour ignorer les speckles
            hues.append(int(np.median(patch[:, :, 0])))
            sats.append(int(np.median(patch[:, :, 1])))
            vals.append(int(np.median(patch[:, :, 2])))

    if not hues:
        return qr_text, 0, 0, 0

    # moyenne des medianes de gauche et droite
    avgHue = int(np.mean(hues))
    avgSat = int(np.mean(sats))
    avgVal = int(np.mean(vals))

    return qr_text, avgHue, avgSat, avgVal

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

    # POST /api/scans - le backend cree l'item et renvoie tout direct
    try:
        r = requests.post(f"{BACKEND_URL}/api/scans", json={
            "scan": {
                "qrValue": qr_text,
                "hue": hue,
                "saturation": sat,
                "value": val,
            }
        }, timeout=5)

        # le backend renvoie 201 avec {itemId, decision, orderId}
        if r.status_code != 201:
            print(f"  [!] POST /scans a repondu HTTP {r.status_code}: {r.text}")
            return

        data = r.json()
        itemId   = data["itemId"]
        decision = data["decision"]
        orderId  = data.get("orderId") or 0
        # le backend renvoie le nom de la couleur en string: "yellow"
        color_name = data.get("color", "").lower()

        team_raw = data.get("team")

        print(f"  [BACKEND] Item #{itemId} decision={decision} orderId={orderId}"
              f" team={team_raw} color={color_name}")

        # envoie l'info a l'Arduino pour l'aiguillage + affichage HSV/team
        decisionByte = {"ORDER": 0x01, "STOCK": 0x02}.get(decision, 0x00)
        payload = bytes([
            (itemId >> 8) & 0xFF,
            itemId & 0xFF,
            decisionByte,
            orderId & 0xFF,
            hue,
            sat,
            val,
            team_raw.get("id", 0) if isinstance(team_raw, dict) else 0
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
        r = requests.patch(f"{BACKEND_URL}/api/items/{itemId}/status", json={
            "status": {"status": decisionStatus}
        }, timeout=5)
        data = r.json()
        if "completedOrdersCount" in data:
            count = data["completedOrdersCount"]
            payload = bytes([(count >> 8) & 0xFF, count & 0xFF])
            st.send(SerialTransfer.PID_COMPLETED_COUNT, payload)
            print(f"  [COMPLETED] {count} orders completed")
    except Exception as e:
        print(f"  [!] Backend injoignable: {e}")

def handleLocalOrder(payload):
    """L'Arduino a saisi une commande au keypad, on l'envoie au backend."""
    if len(payload) < 2:
        return
    lineCount = payload[0]
    if lineCount == 0 or len(payload) < 1 + lineCount * 2:
        return
    color_names = {0x01: "jaune", 0x02: "bleu", 0x03: "magenta", 0x04: "brun", 0x05: "orange"}
    print(f"[ARDUINO] Commande keypad")
    try:
        r_colors = requests.get(f"{BACKEND_URL}/api/colors", timeout=10)
        if r_colors.status_code != 200:
            print("  [!] Impossible de recuperer les couleurs de la DB")
            return
        db_colors = {c["name"].lower(): c["id"] for c in r_colors.json()}
        order_lines = []
        for i in range(lineCount):
            colorByte = payload[1 + i * 2]
            qty       = payload[1 + i * 2 + 1]
            name = color_names.get(colorByte)
            cid  = db_colors.get(name) if name else None
            if cid and qty > 0:
                order_lines.append({"quantity": qty, "id": cid})
        r = requests.post(f"{BACKEND_URL}/api/neworder", json={"lines": order_lines}, timeout=5)
        if r.status_code == 204:
            print(f"  [BACKEND] Commande creee ({lineCount} lignes)")
        else:
            print(f"  [!] POST /neworder {r.status_code}")
    except Exception as e:
        print(f"  [!] Backend injoignable: {e}")

def handleSensorStatus(payload):
    """L'Arduino envoie l'etat des capteurs IR - on POST au backend."""
    if len(payload) < 1:
        return
    mask = payload[0]
    sensors = [
        {"name": "IR 1", "state": 1 if mask & 0x01 else 0},
        {"name": "IR 2", "state": 1 if mask & 0x02 else 0},
        {"name": "IR 3", "state": 1 if mask & 0x04 else 0},
        {"name": "IR 4", "state": 1 if mask & 0x08 else 0},
        {"name": "IR 5", "state": 1 if mask & 0x10 else 0},
    ]
    try:
        requests.post(f"{BACKEND_URL}/api/stats/sensors", json={"sensors": sensors}, timeout=2)
    except Exception:
        pass  # backend down, tant pis

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

    elif pid == SerialTransfer.PID_SENSOR_STATUS:
        handleSensorStatus(payload)

    elif pid == SerialTransfer.PID_LOCAL_ORDER:
        handleLocalOrder(payload)

    elif pid == SerialTransfer.PID_PING:
        print("[ARDUINO] Ping recu (diag)")

# couleurs actives via Socket.IO (le backend previent quand ca change)

sio = socketio.Client()

@sio.on('color_update')
def on_color_update():
    # le backend a modifie les couleurs, on refetch et on balance a l'Arduino
    fetchAndSendColors()

def fetchAndSendColors():
    """GET /api/colors -> PID_COLOR_LIST vers l'Arduino (appele au connect + sur event)."""
    try:
        r = requests.get(f"{BACKEND_URL}/api/colors", timeout=3)
        if r.status_code != 200:
            return
        name_to_byte = {"jaune": 0x01, "yellow": 0x01, "bleu": 0x02, "blue": 0x02,
                        "magenta": 0x03, "pink": 0x03,
                        "brun": 0x04, "brown": 0x04,
                        "orange": 0x05}
        active = []
        for c in r.json():
            # le backend filtre deja status:true, mais on double-check
            if c.get("status"):
                bid = name_to_byte.get((c.get("name") or "").lower())
                if bid:
                    active.append(bid)
        if active:
            st.send(SerialTransfer.PID_COLOR_LIST, bytes([len(active)] + active))
            names = {0x01:"Jaune",0x02:"Bleu",0x03:"Magenta",0x04:"Brun",0x05:"Orange"}
            print(f"[COLORS] {len(active)} actives envoyees: "
                  f"{[names.get(b,'?') for b in active]}")
    except Exception:
        pass  # backend down, on retentera au prochain event

@sio.on('connect')
def on_connect():
    print("[SIO] Connecte au backend")
    fetchAndSendColors()  # charge les couleurs direct au connect

@sio.on('disconnect')
def on_disconnect():
    print("[SIO] Deconnecte du backend")

# boucle principale

def main():
    global running
    print("[PI_DRIVER] Pret. En attente de blocs...")

    # connexion Socket.IO pour les updates de couleur
    sio.connect(BACKEND_URL)

    while running:
        handleArduinoFrame()
        time.sleep(0.05)


if __name__ == "__main__":
    main()
