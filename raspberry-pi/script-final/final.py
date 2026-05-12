# Driver pour le Raspberry Pi — fait le pont entre le backend (API + Socket.IO),
# l'Arduino (SerialTransfer via USB) et la camera (QR + detection couleur)
#
# Toute la logique metier (file d'attente, decision STOCK/ORDER, statuts) est dans le backend.
# Ici on fait juste passer les messages et on scanne des blocs.

import serial
import signal
import sys
import time
import threading
import requests
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview
import socketio
from serial_transfer import SerialTransfer

# --- config a adapter selon le setup ---
BAUD         = 9600
# essaye d'abord l'USB, puis /dev/serial0, puis /dev/ttyAMA0
PORT_CANDIDATES = ["/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/serial0", "/dev/ttyAMA0"]
BACKEND_URL  = "http://localhost:3000"

# --- les memes valeurs que dans pmul2-colors.h ---
class Color:
    YELLOW  = 0x01
    BLUE    = 0x02
    MAGENTA = 0x03

# --- les memes valeurs que dans pmul2-teams.h ---
class Team:
    TEAM01 = 0x01
    TEAM02 = 0x02
    TEAM03 = 0x03
    TEAM04 = 0x04
    TEAM05 = 0x05
    UNKNOWN = 0xFF

    @classmethod
    def from_qr_text(cls, text):
        t = text.strip().lower()
        return {
            "team01": cls.TEAM01, "team02": cls.TEAM02,
            "team03": cls.TEAM03, "team04": cls.TEAM04,
            "team05": cls.TEAM05,
        }.get(t, cls.UNKNOWN)

COLOR_NAMES = {Color.YELLOW: "Jaune", Color.BLUE: "Bleu", Color.MAGENTA: "Magenta"}

# --- init serie + SerialTransfer (auto-detect du port) ---
import os

port = None
for candidate in PORT_CANDIDATES:
    if os.path.exists(candidate):
        port = candidate
        break

if port is None:
    print("[!] Aucun port serie trouve. Essayes:")
    print("    ls /dev/ttyACM* /dev/ttyUSB* /dev/ttyAMA* /dev/serial*")
    print("    dmesg | grep -i tty | tail -10")
    sys.exit(1)

print(f"[SERIAL] Connexion sur {port}")
s = serial.Serial(port, BAUD)
time.sleep(2)  # on laisse le temps a la connexion de s'etablir
st = SerialTransfer(s)

# --- init camera ---
cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start()
time.sleep(2)  # la camera a besoin d'un peu de chauffe

# --- etat minimal (juste ce qu'il faut pour pas spammer) ---
currentOrder  = None   # la commande en cours d'execution
lastSentBlock = None   # anti-doublon (qr_text, color)
arduinoBusy   = False  # True si l'Arduino trie
running       = True   # pour le clean shutdown

# --- helpers d'envoi vers l'Arduino (SerialTransfer) ---

def sendTargetOrder(team, blue, yellow, magenta):
    # PID 0x01: 4 bytes [team, bleu, jaune, magenta]
    st.send(SerialTransfer.PID_TARGET_ORDER, bytes([team, blue, yellow, magenta]))

def sendBlockInfo(color, team):
    # PID 0x02: 2 bytes [couleur, team]
    st.send(SerialTransfer.PID_BLOCK_INFO, bytes([color, team]))

# --- lecture des trames Arduino ---

def receiveFrame():
    """
    Lit un packet SerialTransfer entrant et le dispatche selon son Packet ID.
    Retourne un dict ou None si rien recu.
    """
    result = st.available()
    if result is None:
        return None

    packet_id, payload = result

    # Status (Busy/Ready/Done/ScanNeeded)
    if packet_id == SerialTransfer.PID_STATUS:
        if len(payload) < 1:
            return None
        status = payload[0]
        return {
            "type": "status",
            "busy": (status == SerialTransfer.STATUS_BUSY),
            "done": (status == SerialTransfer.STATUS_DONE),
            "scan_needed": (status == SerialTransfer.STATUS_SCAN_NEEDED),
        }

    # OrderUpdate (progres de la commande)
    if packet_id == SerialTransfer.PID_ORDER_UPDATE:
        if len(payload) < 4:
            return None
        return {
            "type": "order_update",
            "teamId": payload[0],
            "blue": payload[1],
            "yellow": payload[2],
            "magenta": payload[3],
        }

    return None  # on ignore le reste

# --- detection QR + couleur depuis un frame camera ---
# (garde en local pour la vitesse — l'Arduino doit savoir tout de suite
#  ou envoyer le bloc, pas le temps d'attendre un round-trip backend)

def decodeFrame(frame_bgr):
    hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)
    qr_results = decode(frame_bgr)

    if not qr_results:
        return None, None, None, None, None

    obj = qr_results[0]  # premier QR code trouve
    h, w = frame_bgr.shape[:2]

    # patch 10x10 a droite du QR pour chopper la couleur du bloc
    px = min(obj.rect.left + obj.rect.width + 3, w - 15)
    py = min(obj.rect.top + (obj.rect.height // 2), h - 15)
    patch = hsv[py:py + 10, px:px + 10]

    avgHue = np.mean(patch[:, :, 0])
    avgSat = np.mean(patch[:, :, 1])
    avgVal = np.mean(patch[:, :, 2])

    # choix de la couleur selon la teinte (pour l'Arduino en local)
    if 25 <= avgHue < 35:
        color = Color.YELLOW
    elif 85 <= avgHue < 105:
        color = Color.BLUE
    elif 140 <= avgHue < 160:
        color = Color.MAGENTA
    else:
        return None, None, None, None, None  # couleur inconnue, on jette

    # on essaie de decoder le texte du QR
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        return None, None, None, None, None

    return qr_text, color, int(avgHue), int(avgSat), int(avgVal)

# --- reporting vers le backend ---

def reportScan(qr_text, hue, sat, val):
    """
    Envoie un scan au backend (POST /api/scans) pour qu'il fasse le matching
    couleur et cree l'item dans la DB.
    """
    try:
        requests.post(f"{BACKEND_URL}/api/scans", json={
            "scan": {
                "qrValue": qr_text,
                "hue": hue,
                "saturation": sat,
                "value": val,
            }
        }, timeout=2)
    except Exception as e:
        print(f"  [!] Echec report scan au backend: {e}")

# --- scan declenche par l'Arduino (IR1) ---

def scanBlockAndSend():
    """Capture un frame, detecte le QR + couleur, envoie a l'Arduino et au backend."""
    global lastSentBlock

    frame = cam.capture_array()
    if frame is None:
        print("  [!] Camera: pas de frame")
        return

    qr_text, color, hue, sat, val = decodeFrame(frame)
    if qr_text is None or color is None:
        print("  [!] Scan: QR ou couleur pas detecte")
        return

    boxHash = (qr_text, color)
    if boxHash == lastSentBlock:
        print("  [SCAN] Bloc deja traite (anti-doublon), on ressaye...")
        return

    team = Team.from_qr_text(qr_text)
    print(f"  [SCAN] Team {qr_text} | {COLOR_NAMES[color]} -> Arduino")

    # on envoie les infos a l'Arduino pour l'aiguillage
    sendBlockInfo(color, team)

    # on reporte au backend pour la DB (async, on attend pas la reponse)
    reportScan(qr_text, hue, sat, val)

    lastSentBlock = boxHash

# --- Socket.IO vers le backend ---

sio = socketio.Client()

@sio.on('connect')
def on_connect():
    print("[SIO] Connecte au backend")
    sio.emit('register_pi')
    # on dit au backend si on est dispo ou pas (important pour les reconnexions
    # en plein milieu d'une commande, comme ca le backend nous renvoie pas
    # une deuxieme commande par dessus)
    if not arduinoBusy:
        sio.emit('arduino_ready')
    else:
        print("[SIO] Reconnexion en cours de commande — on attend que l'Arduino finisse")

@sio.on('start_order')
def on_start_order(data):
    """
    Le backend nous dit de lancer une commande.
    data: { teamId, blue, yellow, magenta, orderId }
    """
    global currentOrder, lastSentBlock, arduinoBusy

    currentOrder = data
    lastSentBlock = None

    sendTargetOrder(data['teamId'], data['blue'], data['yellow'], data['magenta'])
    arduinoBusy = True

    print(f"[ORDER] Commande recue du backend: Team {data['teamId']} "
          f"| B={data['blue']} Y={data['yellow']} M={data['magenta']} "
          f"(orderId={data['orderId']})")

@sio.on('disconnect')
def on_disconnect():
    print("[SIO] Deconnecte du backend")

# --- cleanup propre ---

def cleanup(signum=None, frame=None):
    global running
    print("\n[!] Shutting down...")
    running = False
    cam.stop()
    s.close()
    if sio.connected:
        sio.disconnect()
    sys.exit(0)

signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

# --- boucle principale ---

def main():
    global currentOrder, lastSentBlock, arduinoBusy, running

    print("[PI_DRIVER] Demarrage... Connexion au backend...")
    sio.connect(BACKEND_URL)

    print("[PI_DRIVER] Pret. En attente de commandes du backend.")
    arduinoBusy = False

    while running:
        # 1) toujours ecouter ce que l'Arduino nous envoie
        frame_in = receiveFrame()

        # 2) Status Arduino (Busy/Done/ScanNeeded)
        if frame_in and frame_in["type"] == "status":
            if frame_in.get("done"):
                print("[ARDUINO] Commande terminee (status DONE)")
                if currentOrder and sio.connected:
                    sio.emit('order_done', {
                        'teamId': currentOrder.get('teamId'),
                        'orderId': currentOrder.get('orderId'),
                    })
                currentOrder = None
                arduinoBusy = False
                lastSentBlock = None

            elif frame_in.get("scan_needed"):
                # l'Arduino a bloque un bloc, la camera doit scanner maintenant
                print("[ARDUINO] Bloc en position — scan en cours...")
                scanBlockAndSend()

            elif frame_in.get("busy") and not arduinoBusy:
                print("[ARDUINO] L Arduino signale : OCCUPE")
                arduinoBusy = True

            elif not frame_in.get("busy") and arduinoBusy:
                print("[ARDUINO] L Arduino signale : DISPONIBLE")
                arduinoBusy = False
                if sio.connected:
                    sio.emit('arduino_ready')

        # 3) Progres de la commande (OrderUpdate)
        if frame_in and frame_in["type"] == "order_update":
            print(f"  <-- Progres: teamId={frame_in['teamId']} "
                  f"B={frame_in['blue']} Y={frame_in['yellow']} M={frame_in['magenta']}")
            if sio.connected:
                sio.emit('order_progress', frame_in)

        # 4) rien a faire en continu — tout est declenche par les evenements
        # (scan_needed de l'Arduino, start_order du backend)

        time.sleep(0.05)


if __name__ == "__main__":
    main()
