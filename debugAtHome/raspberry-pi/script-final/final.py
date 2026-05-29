# Driver Raspberry Pi - pont ultra simple entre :
#   - l'Arduino (SerialTransfer via USB)
#   - la camera (QR + detection couleur)
#   - le backend (API REST)
#
# Aucune logique metier ici. Le backend decide tout.
# Le Pi fait juste passer les trames et scanner des blocs.

import os
import signal
import sys
import time

import cv2
import numpy as np
import requests
import serial
import socketio
from picamera2 import Picamera2
from serial_transfer import SerialTransfer

# ── Config ────────────────────────────────────────────────────────────────────

BAUD        = 115200
BACKEND_URL = "http://localhost:3000"

PID_CURRENT_ORDER = 0x07

# Mapping centralisé : nom de couleur → byte Arduino
NAME_TO_BYTE = {
    "jaune":   0x01, "yellow":  0x01,
    "bleu":    0x02, "blue":    0x02,
    "magenta": 0x03, "pink":    0x03,
    "brun":    0x04, "brown":   0x04,
}

# Mapping centralisé : byte Arduino → index dans le tableau qtys [J, B, M, Br]
BYTE_TO_INDEX = {0x01: 0, 0x02: 1, 0x03: 2, 0x04: 3}

BYTE_LABEL = {0x01: "Jaune", 0x02: "Bleu", 0x03: "Magenta", 0x04: "Brun"}

MAX_SCAN_RETRIES = 4

# ── Logs ──────────────────────────────────────────────────────────────────────

def log(msg):
    """Print + envoi au backend via API REST pour affichage web."""
    print(msg)
    try:
        requests.post(
            f"{BACKEND_URL}/api/python/logs",
            json={"msg": msg, "time": time.strftime("%H:%M:%S")},
            timeout=1,
        )
    except Exception:
        pass

# ── Détection port Arduino ────────────────────────────────────────────────────

ARDUINO_VID = "2341"   # Arduino SA
ARDUINO_PID = "0042"   # Mega 2560

def find_arduino_port():
    """
    Cherche le port série de l'Arduino Mega via son VID/PID USB.
    Plus fiable que chercher /dev/ttyACM0 à l'aveugle.
    """
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            if p.vid is not None and p.pid is not None:
                vid = format(p.vid, "04x")
                pid = format(p.pid, "04x")
                if vid == ARDUINO_VID and pid == ARDUINO_PID:
                    print(f"[SERIAL] Arduino Mega detecte sur {p.device} "
                          f"(VID={vid} PID={pid} SN={p.serial_number})")
                    return p.device
        # Fallback: cherche ttyACM/ttyUSB si pyserial pas assez récent
        for c in ("/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1"):
            if os.path.exists(c):
                print(f"[SERIAL] Fallback: {c} (VID/PID non verifiable)")
                return c
    except Exception as e:
        print(f"[SERIAL] Erreur detection: {e}")
    return None

# ── Init série ────────────────────────────────────────────────────────────────

port = find_arduino_port()
if port is None:
    print("[!] Arduino Mega introuvable (VID=2341 PID=0042)")
    print("    Verifie le cable USB et que le sketch est flash")
    sys.exit(1)

s = serial.Serial(port, BAUD, timeout=0.5)

print("[BOOT] Attente de l'Arduino...")
t0 = time.time()
while time.time() - t0 < 15:
    if s.in_waiting and s.read(1) == b'R':
        log("[BOOT] Arduino pret !")
        break
    time.sleep(0.1)
else:
    print("[!] Arduino pas pret - verifie le flash et le cable")
    sys.exit(1)

st = SerialTransfer(s)

# ── Init caméra ───────────────────────────────────────────────────────────────

print("[CAM] Initialisation...")
cam = Picamera2()

cam_config = cam.create_preview_configuration()
cam_config["main"]["size"]   = (640, 480)
cam_config["main"]["format"] = "RGB888"
cam.configure(cam_config)
cam.start()
time.sleep(0.5)

cam.set_controls({
    "AwbEnable":  True,
    "AwbMode":    1,       # 1 = Indoor
    "Saturation": 1.0,
    "Sharpness":  1.5,
})
time.sleep(1.0)
log("[CAM] Prete")

# ── Socket.IO ─────────────────────────────────────────────────────────────────

sio = socketio.Client()

# ── État global ───────────────────────────────────────────────────────────────

running = True

# ── Cleanup ───────────────────────────────────────────────────────────────────

def cleanup(signum=None, frame=None):
    global running
    print("\n[!] Shutdown...")
    running = False
    cam.stop()
    s.close()
    if sio.connected:
        sio.disconnect()
    sys.exit(0)

signal.signal(signal.SIGINT,  cleanup)
signal.signal(signal.SIGTERM, cleanup)

# ── Caméra : QR + couleur ────────────────────────────────────────────────────

def _decode_qr(frame, w, h):
    """
    Décode un QR code depuis un frame BGR via OpenCV.
    Essaie 4 préprocessings différents pour maximiser le taux de détection.
    Retourne (qr_str, points) ou (None, None).
    """
    gray     = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    clahe    = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
    detector = cv2.QRCodeDetector()

    def _try(img, scale=1.0):
        data, pts, _ = detector.detectAndDecode(img)
        if data and pts is not None:
            return data, pts if scale == 1.0 else pts / scale
        return None, None

    # 1. Gris direct
    qr, pts = _try(gray)
    if qr: return qr, pts

    # 2. CLAHE (contraste adaptatif)
    qr, pts = _try(clahe.apply(gray))
    if qr: return qr, pts

    # 3. Zoom x2 + CLAHE
    zoomed = cv2.resize(gray, (w * 2, h * 2), interpolation=cv2.INTER_CUBIC)
    qr, pts = _try(clahe.apply(zoomed), scale=2.0)
    if qr: return qr, pts

    # 4. Seuillage adaptatif (dernier recours)
    binary = cv2.adaptiveThreshold(
        clahe.apply(gray), 255,
        cv2.ADAPTIVE_THRESH_GAUSSIAN_C, cv2.THRESH_BINARY, 11, 2,
    )
    return _try(binary)


def decodeFrame(cam):
    """
    Capture jusqu'à MAX_SCAN_RETRIES frames et tente de décoder le QR.
    Retourne (qr_text, hue, sat, val).
    Si le QR est introuvable, retourne ("", 0, 0, 0).
    """
    qr, points, frame = None, None, None

    for attempt in range(1, MAX_SCAN_RETRIES + 1):
        frame = cam.capture_array()
        h, w  = frame.shape[:2]
        qr, points = _decode_qr(frame, w, h)

        if qr:
            log(f"  [QR] Decode en {attempt} tentative(s)")
            break

        log(f"  [QR] Tentative {attempt}/{MAX_SCAN_RETRIES} ratee")
        if attempt < MAX_SCAN_RETRIES:
            time.sleep(0.15)

    if not qr or points is None:
        log("  [QR] Echec total - envoi scan vide au backend")
        return "", 0, 0, 0

    # Calcul HSV sur les zones colorées à gauche et droite du QR
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    pts = points[0].astype(int)
    rx  = int(pts[:, 0].min())
    ry  = int(pts[:, 1].min())
    rw  = int(pts[:, 0].max()) - rx
    rh  = int(pts[:, 1].max()) - ry
    cy, ps = ry + rh // 2, 16

    hues, sats, vals = [], [], []
    for sx, sy in [(rx - 15 - ps, cy - ps // 2), (rx + rw + 15, cy - ps // 2)]:
        if 0 <= sx <= w - ps and 0 <= sy <= h - ps:
            patch = hsv[sy:sy + ps, sx:sx + ps]
            hues.append(int(np.median(patch[:, :, 0])))
            sats.append(int(np.median(patch[:, :, 1])))
            vals.append(int(np.median(patch[:, :, 2])))

    if not hues:
        return qr, 0, 0, 0

    return qr, int(np.mean(hues)), int(np.mean(sats)), int(np.mean(vals))

# ── Handlers Arduino ──────────────────────────────────────────────────────────

def handleScanNeeded():
    """Un bloc est bloqué par l'Arduino - on scanne et on demande l'info au backend."""
    log("[ARDUINO] Bloc en position - scan...")

    qr_text, hue, sat, val = decodeFrame(cam)
    log(f"  [SCAN] QR={qr_text} H={hue} S={sat} V={val}")

    try:
        r = requests.post(f"{BACKEND_URL}/api/scans", json={
            "scan": {
                "qrValue":    qr_text,
                "hue":        hue,
                "saturation": sat,
                "value":      val,
            }
        }, timeout=5)

        if r.status_code != 201:
            log(f"  [!] POST /scans -> HTTP {r.status_code}: {r.text}")
            return

        data       = r.json()
        itemId     = data["itemId"]
        decision   = data["decision"]
        orderId    = data.get("orderId") or 0
        color_name = (data.get("color") or "").lower()
        team_raw   = data.get("team")

        log(f"  [BACKEND] Item #{itemId} decision={decision} orderId={orderId} "
            f"team={team_raw} color={color_name}")

        decisionByte = {"ORDER": 0x01, "STOCK": 0x02}.get(decision, 0x00)
        payload = bytes([
            (itemId >> 8) & 0xFF,
            itemId & 0xFF,
            decisionByte,
            orderId & 0xFF,
        ])
        st.send(SerialTransfer.PID_ITEM_INFO, payload)

    except Exception as e:
        log(f"  [!] Backend injoignable: {e}")


def handleScanResult(payload):
    """L'Arduino a confirmé le tri du bloc - on forward au backend."""
    if len(payload) < 3:
        return

    itemId         = (payload[0] << 8) | payload[1]
    decisionStatus = "CONFIRMED" if payload[2] == 0x00 else "FAILED"

    log(f"[ARDUINO] Resultat: Item #{itemId} {decisionStatus}")

    try:
        r = requests.patch(f"{BACKEND_URL}/api/items/{itemId}/status", json={
            "status": {"status": decisionStatus}
        }, timeout=5)

        if r.status_code not in (200, 201, 204):
            log(f"  [!] PATCH /items/{itemId}/status -> HTTP {r.status_code}")
            return

        if r.status_code == 204 or not r.content:
            log("  [OK] Status mis a jour")
            return

        data = r.json()
        if "completedOrdersCount" in data:
            count   = data["completedOrdersCount"]
            payload = bytes([(count >> 8) & 0xFF, count & 0xFF])
            st.send(SerialTransfer.PID_COMPLETED_COUNT, payload)
            log(f"  [COMPLETED] {count} commandes completes")

    except Exception as e:
        log(f"  [!] Backend injoignable: {e}")


def handleLocalOrder(payload):
    """Commande saisie sur le keypad Arduino - on crée la commande via le backend."""
    if len(payload) < 2:
        return
    lineCount = payload[0]
    if lineCount == 0 or len(payload) < 1 + lineCount * 2:
        return

    try:
        r_colors = requests.get(f"{BACKEND_URL}/api/colors", timeout=10)
        if r_colors.status_code != 200:
            return

        # byte → {name, id} depuis la réponse DB
        byte_to_color = {}
        for c in r_colors.json():
            name = (c.get("name") or "").lower()
            bid  = NAME_TO_BYTE.get(name)
            if bid:
                byte_to_color[bid] = {"name": name, "id": c["id"]}

        order_lines = []
        for i in range(lineCount):
            colorByte = payload[1 + i * 2]
            qty       = payload[2 + i * 2]
            color = byte_to_color.get(colorByte)
            if color and qty > 0:
                order_lines.append({"quantity": qty, "id": color["id"]})

        if not order_lines:
            log("  [!] Aucune ligne valide dans la commande keypad")
            return

        r = requests.post(f"{BACKEND_URL}/api/neworder",
                          json={"lines": order_lines}, timeout=5)
        if r.status_code == 204:
            log(f"  [BACKEND] Commande creee ({len(order_lines)} lignes)")
        else:
            log(f"  [!] POST /neworder {r.status_code}: {r.text}")

    except Exception as e:
        log(f"  [!] Backend injoignable: {e}")


def handleSensorStatus(payload):
    """L'Arduino envoie l'état des capteurs IR - on POST au backend."""
    if len(payload) < 1:
        return
    mask = payload[0]
    sensors = [
        {"name": "IR SCAN",  "state": 1 if mask & 0x01 else 0},
        {"name": "IR NEXT",  "state": 1 if mask & 0x02 else 0},
        {"name": "IR STOCK", "state": 1 if mask & 0x04 else 0},
        {"name": "IR ORDER", "state": 1 if mask & 0x08 else 0},
        {"name": "IR PASS",  "state": 1 if mask & 0x10 else 0},
    ]
    try:
        requests.post(f"{BACKEND_URL}/api/sensors",
                      json={"sensors": sensors}, timeout=2)
    except Exception:
        pass


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
            log("[ARDUINO] Commande terminee")
        elif code == SerialTransfer.STATUS_BUSY:
            log("[ARDUINO] Occupe")
        elif code == SerialTransfer.STATUS_READY:
            log("[ARDUINO] Dispo")

    elif pid == SerialTransfer.PID_SCAN_RESULT:
        handleScanResult(payload)

    elif pid == SerialTransfer.PID_SENSOR_STATUS:
        handleSensorStatus(payload)

    elif pid == SerialTransfer.PID_LOCAL_ORDER:
        handleLocalOrder(payload)

    elif pid == SerialTransfer.PID_PING:
        log("[ARDUINO] Ping recu (diag)")

# ── Socket.IO : couleurs & commande courante ──────────────────────────────────

def fetchAndSendColors():
    """GET /api/colors -> PID_COLOR_LIST vers l'Arduino."""
    try:
        r = requests.get(f"{BACKEND_URL}/api/colors", timeout=3)
        if r.status_code != 200:
            return

        active = [
            NAME_TO_BYTE[name]
            for c in r.json()
            if c.get("status")
            for name in [(c.get("name") or "").lower()]
            if name in NAME_TO_BYTE
        ]

        if active:
            st.send(SerialTransfer.PID_COLOR_LIST, bytes([len(active)] + active))
            log(f"[COLORS] {len(active)} actives envoyees: "
                f"{[BYTE_LABEL.get(b, '?') for b in active]}")
    except Exception:
        pass


def fetchAndSendCurrentOrder():
    """GET /api/orders/current -> PID_CURRENT_ORDER vers l'Arduino."""
    try:
        r = requests.get(f"{BACKEND_URL}/api/orders/current", timeout=3)

        if r.status_code == 404:
            st.send(PID_CURRENT_ORDER, bytes([0, 0, 0, 0, 0]))
            log("[ORDER] Aucune commande active (effacement sur l'Arduino).")
            return

        if r.status_code != 200:
            return

        order_data = r.json()
        order_id   = order_data.get("id", 0)
        qtys       = [0, 0, 0, 0]

        for line in order_data.get("ORDER_LINE", []):
            color_info = line.get("COLOR", {})
            c_name     = (color_info.get("name") or "").lower()
            qty        = line.get("quantity", 0)
            bid        = NAME_TO_BYTE.get(c_name)
            if bid is not None:
                qtys[BYTE_TO_INDEX[bid]] += qty

        payload = bytes([order_id & 0xFF] + qtys)
        st.send(PID_CURRENT_ORDER, payload)
        log(f"[ORDER] Commande #{order_id} active envoyee "
            f"(J:{qtys[0]} B:{qtys[1]} M:{qtys[2]} Br:{qtys[3]})")

    except Exception as e:
        log(f"[!] Erreur recuperation commande courante: {e}")


@sio.on('connect')
def on_connect():
    log("[SIO] Connecte au backend")
    fetchAndSendColors()
    fetchAndSendCurrentOrder()


@sio.on('disconnect')
def on_disconnect():
    log("[SIO] Deconnecte du backend")


@sio.on('color_event')
def on_color_event(data=None):
    log("[SIO] Evenement 'color_event' recu, mise a jour des couleurs...")
    fetchAndSendColors()


@sio.on('order_event')
def on_order_event(data=None):
    log("[SIO] Evenement 'order_event' recu, mise a jour de la commande courante...")
    fetchAndSendCurrentOrder()

# ── Boucle principale ─────────────────────────────────────────────────────────

def main():
    global running
    log("[PI_DRIVER] Pret. En attente de blocs...")

    while running:
        try:
            sio.connect(BACKEND_URL, transports=['websocket'])
            break
        except Exception as e:
            print(f"[SIO] Connexion echouee ({e}). Nouvelle tentative dans 5 secondes...")
            time.sleep(5)

    while running:
        try:
            handleArduinoFrame()
        except OSError as e:
            log(f"[!] Arduino debranche ou port perdu: {e}")
            log("[!] Reconnecte l'Arduino et relance le script")
            cleanup()
        time.sleep(0.05)


if __name__ == "__main__":
    main()