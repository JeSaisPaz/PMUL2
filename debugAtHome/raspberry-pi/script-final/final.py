# Driver Raspberry Pi - pont ultra simple entre l'Arduino, la caméra et le backend

import serial
import signal
import sys
import time
import os
import requests
import cv2
import numpy as np
from picamera2 import Picamera2
from serial_transfer import SerialTransfer
import socketio

# config
BAUD        = 9600
BACKEND_URL = "http://localhost:3000"

def log(msg):
    """Print + envoi au backend via API REST pour affichage web."""
    print(msg)
    try:
        requests.post(f"{BACKEND_URL}/api/python/logs",
                      json={"msg": msg, "time": time.strftime("%H:%M:%S")},
                      timeout=1)
    except Exception:
        pass

# Configuration de la détection USB
ARDUINO_VID = "2341"  # Arduino SA
ARDUINO_PID = "0042"  # Mega 2560

def find_arduino_port():
    """Cherche le port serie de l'Arduino Mega via son VID/PID USB."""
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            if p.vid is not None and p.pid is not None:
                vid = format(p.vid, "04x")
                pid = format(p.pid, "04x")
                if vid == ARDUINO_VID and pid == ARDUINO_PID:
                    print(f"[SERIAL] Arduino Mega detecte sur {p.device}")
                    return p.device
        fallback = ["/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1"]
        for c in fallback:
            if os.path.exists(c): return c
    except Exception as e:
        print(f"[SERIAL] Erreur detection: {e}")
    return None

# [NOUVEAU] Fonction compacte de reconnexion / démarrage matériel
def connect_arduino():
    global s, st
    while running:
        port = find_arduino_port()
        if port:
            try:
                s = serial.Serial(port, BAUD, timeout=0.5)
                st = SerialTransfer(s)
                log("[BOOT] Attente du signal 'R' de l'Arduino...")
                t0 = time.time()
                while time.time() - t0 < 10:
                    if s.in_waiting and s.read(1) == b'R':
                        log("[BOOT] Arduino synchronisé et prêt !")
                        if sio.connected: fetchAndSendColors()
                        return True
                    time.sleep(0.1)
                s.close()
            except Exception:
                pass
        log("[!] Arduino introuvable ou indisponible. Nouvelle tentative dans 3s...")
        time.sleep(3)
    return False

# init camera (Une seule fois au démarrage global)
print("[CAM] Initialisation...")
cam = Picamera2()
config = cam.create_preview_configuration()
config["main"]["size"], config["main"]["format"] = (640, 480), "RGB888"
cam.configure(config)
cam.start()
time.sleep(0.5)
cam.set_controls({"AwbEnable": True, "AwbMode": 1, "Saturation": 1.0, "Sharpness": 1.5})
time.sleep(1.0)
log("[CAM] Prete")

MAX_SCAN_RETRIES = 4
running = True

def cleanup(signum=None, frame=None):
    global running
    print("\n[!] Shutdown...")
    running = False
    cam.stop()
    try: s.close()
    except: pass
    if sio.connected: sio.disconnect()
    sys.exit(0)

signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

# --- [Garde tes fonctions existantes telles quelles] ---
def _decode_qr(frame, w, h):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    detector = cv2.QRCodeDetector()
    def _try(img, scale=1.0):
        data, pts, _ = detector.detectAndDecode(img)
        if data and pts is not None: return data, pts if scale == 1.0 else pts / scale
        return None, None
    qr, pts = _try(gray)
    if qr: return qr, pts
    qr, pts = _try(clahe.apply(gray))
    if qr: return qr, pts
    zoomed = cv2.resize(gray, (w*2, h*2), interpolation=cv2.INTER_CUBIC)
    qr, pts = _try(clahe.apply(zoomed), scale=2.0)
    if qr: return qr, pts
    binary = cv2.adaptiveThreshold(clahe.apply(gray), 255, cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 11, 2)
    qr, pts = _try(binary)
    return qr, pts

def decodeFrame(cam):
    qr, points, frame = None, None, None
    for attempt in range(1, MAX_SCAN_RETRIES + 1):
        frame = cam.capture_array()
        h, w  = frame.shape[:2]
        qr, points = _decode_qr(frame, w, h)
        if qr:
            log(f"  [QR] Decode en {attempt} tentative(s)")
            break
        log(f"  [QR] Tentative {attempt}/{MAX_SCAN_RETRIES} ratee")
        if attempt < MAX_SCAN_RETRIES: time.sleep(0.15)
    if not qr or points is None:
        log("  [QR] Echec total - envoi scan vide au backend")
        return "", 0, 0, 0
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    pts = points[0].astype(int)
    rx, ry = int(pts[:, 0].min()), int(pts[:, 1].min())
    rw, rh = int(pts[:, 0].max()) - rx, int(pts[:, 1].max()) - ry
    cy, ps = ry + rh//2, 16
    sample_pts = [(rx-15-ps, cy-ps//2), (rx+rw+15, cy-ps//2)]
    hues, sats, vals = [], [], []
    for sx, sy in sample_pts:
        if 0 <= sx <= w-ps and 0 <= sy <= h-ps:
            patch = hsv[sy:sy+ps, sx:sx+ps]
            hues.append(int(np.median(patch[:, :, 0])))
            sats.append(int(np.median(patch[:, :, 1])))
            vals.append(int(np.median(patch[:, :, 2])))
    if not hues: return qr, 0, 0, 0
    return qr, int(np.mean(hues)), int(np.mean(sats)), int(np.mean(vals))

def handleScanNeeded():
    log("[ARDUINO] Bloc en position - scan...")
    qr_text, hue, sat, val = decodeFrame(cam)
    log(f"  [SCAN] QR={qr_text} H={hue} S={sat} V={val}")
    try:
        r = requests.post(f"{BACKEND_URL}/api/scans", json={"scan": {"qrValue": qr_text, "hue": hue, "saturation": sat, "value": val}}, timeout=5)
        if r.status_code != 201: return
        data = r.json()
        itemId, decision, orderId = data["itemId"], data["decision"], data.get("orderId") or 0
        decisionByte = {"ORDER": 0x01, "STOCK": 0x02}.get(decision, 0x00)
        payload = bytes([(itemId >> 8) & 0xFF, itemId & 0xFF, decisionByte, orderId & 0xFF])
        st.send(SerialTransfer.PID_ITEM_INFO, payload)
    except Exception as e: log(f"  [!] Backend injoignable: {e}")

def handleScanResult(payload):
    if len(payload) < 3: return
    itemId = (payload[0] << 8) | payload[1]
    decisionStatus = "CONFIRMED" if payload[2] == 0x00 else "FAILED"
    log(f"[ARDUINO] Resultat: Item #{itemId} {decisionStatus}")
    try:
        r = requests.patch(f"{BACKEND_URL}/api/items/{itemId}/status", json={"status": {"status": decisionStatus}}, timeout=5)
        if r.status_code not in (200, 201, 204) or not r.content: return
        data = r.json()
        if "completedOrdersCount" in data:
            count = data["completedOrdersCount"]
            st.send(SerialTransfer.PID_COMPLETED_COUNT, bytes([(count >> 8) & 0xFF, count & 0xFF]))
    except Exception as e: log(f"  [!] Backend injoignable: {e}")

def handleLocalOrder(payload):
    if len(payload) < 2: return
    lineCount = payload[0]
    if lineCount == 0 or len(payload) < 1 + lineCount * 2: return
    name_to_byte = {"jaune": 0x01, "yellow": 0x01, "bleu":  0x02, "blue":   0x02, "magenta": 0x03, "pink": 0x03, "brun":  0x04, "brown":  0x04, "orange": 0x05}
    try:
        r_colors = requests.get(f"{BACKEND_URL}/api/colors", timeout=10)
        if r_colors.status_code != 200: return
        byte_to_color = {}
        for c in r_colors.json():
            name = (c.get("name") or "").lower()
            bid = name_to_byte.get(name)
            if bid: byte_to_color[bid] = {"name": name, "id": c["id"]}
        order_lines = []
        for i in range(lineCount):
            colorByte, qty = payload[1 + i * 2], payload[1 + i * 2 + 1]
            color = byte_to_color.get(colorByte)
            if color and qty > 0: order_lines.append({"quantity": qty, "id": color["id"]})
        if not order_lines: return
        requests.post(f"{BACKEND_URL}/api/neworder", json={"lines": order_lines}, timeout=5)
    except Exception as e: log(f"  [!] Backend injoignable: {e}")

def handleSensorStatus(payload):
    if len(payload) < 1: return
    mask = payload[0]
    sensors = [
        {"name": "IR SCAN", "state": 1 if mask & 0x01 else 0},
        {"name": "IR NEXT", "state": 1 if mask & 0x02 else 0},
        {"name": "IR STOCK", "state": 1 if mask & 0x04 else 0},
        {"name": "IR ORDER", "state": 1 if mask & 0x08 else 0},
        {"name": "IR PASS", "state": 1 if mask & 0x10 else 0},
    ]
    try: requests.post(f"{BACKEND_URL}/api/sensors", json={"sensors": sensors}, timeout=2)
    except Exception: pass

def handleArduinoFrame():
    result = st.available()
    if result is None: return
    pid, payload = result
    if pid == SerialTransfer.PID_STATUS and len(payload) >= 1:
        code = payload[0]
        if code == SerialTransfer.STATUS_SCAN_NEEDED: handleScanNeeded()
        elif code == SerialTransfer.STATUS_DONE: log("[ARDUINO] Commande terminee")
    elif pid == SerialTransfer.PID_SCAN_RESULT: handleScanResult(payload)
    elif pid == SerialTransfer.PID_SENSOR_STATUS: handleSensorStatus(payload)
    elif pid == SerialTransfer.PID_LOCAL_ORDER: handleLocalOrder(payload)
    elif pid == SerialTransfer.PID_PING: log("[ARDUINO] Ping recu (diag)")

# Socket.IO
sio = socketio.Client()

@sio.on('color_update')
def on_color_update(): fetchAndSendColors()

@sio.on('color_event')
def on_color_event(): fetchAndSendColors()

def fetchAndSendColors():
    try:
        r = requests.get(f"{BACKEND_URL}/api/colors", timeout=3)
        if r.status_code != 200: return
        name_to_byte = {"jaune": 0x01, "yellow": 0x01, "bleu": 0x02, "blue": 0x02, "magenta": 0x03, "pink": 0x03, "brun": 0x04, "brown": 0x04, "orange": 0x05}
        active = [name_to_byte[c["name"].lower()] for c in r.json() if c.get("status") and c["name"].lower() in name_to_byte]
        if active:
            st.send(SerialTransfer.PID_COLOR_LIST, bytes([len(active)] + active))
            log(f"[COLORS] Envoi liste couleurs actives")
    except Exception: pass

@sio.on('connect')
def on_connect():
    log("[SIO] Connecte au backend")
    try: fetchAndSendColors()
    except: pass

@sio.on('disconnect')
def on_disconnect(): log("[SIO] Deconnecte du backend")

# --- Boucle principale modifiée ---
def main():
    global running
    log("[PI_DRIVER] Pret. En attente de blocs...")

    # 1. Connexion Socket.IO initiale
    connected = False
    while running and not connected:
        try:
            sio.connect(BACKEND_URL, transports=['websocket'])
            connected = True
        except Exception:
            time.sleep(5)

    # 2. Premier démarrage matériel de l'Arduino
    connect_arduino()

    # 3. Écoute permanente des trames
    while running:
        try:
            # On laisse SerialTransfer lire le port série normalement
            handleArduinoFrame()
            
            # [ASTUCE RESET] Si st.available() a détecté une erreur critique ou 
            # si l'Arduino a envoyé un octet "fantôme" (comme le 'R' du reset),
            # SerialTransfer passe son statut en erreur (valeur négative).
            if st.status < 0:
                # On vérifie si le 'R' traîne dans le buffer pour confirmer le reset
                if s.in_waiting > 0 and b'R' in s.read(s.in_waiting):
                    raise serial.SerialException("Reset matériel détecté (Signal 'R')")

        except (OSError, serial.SerialException) as e:
            log(f"[!] Liaison perdue ou Reset Arduino : {e}")
            try: s.close()
            except: pass
            connect_arduino() # Reconnecte proprement et renvoie les couleurs !
            
        time.sleep(0.05) # On remet le délai initial pour pas surcharger le CPU

if __name__ == "__main__":
    main()