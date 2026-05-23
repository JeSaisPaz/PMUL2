#!/usr/bin/env python3
"""
TEST SCAN + COMMUNICATION + API
================================
Script de test minimal côté Raspberry Pi pour valider:
  1. La communication SerialTransfer Pi <-> Arduino
  2. Le scan QR + couleur (avec option mode simulé sans caméra)
  3. Les appels API vers le backend

UTILISATION:
  python3 test_scan.py                  # avec vraie caméra
  python3 test_scan.py --no-camera      # simule un QR code fixe
  python3 test_scan.py --no-camera --qr "QR_CODE_VALUE"  # QR custom

DEPENDANCES:
  pip install pyserial requests opencv-python pyzbar picamera2
"""

import serial
import time
import sys
import os
import argparse
import requests

# --- Arguments ---
parser = argparse.ArgumentParser(description="Test communication Arduino <-> Pi <-> Backend")
parser.add_argument("--no-camera",  action="store_true", help="Simule la caméra (pas besoin de Pi Camera)")
parser.add_argument("--qr",         default="BLOC_TEST_001", help="Valeur QR simulée (avec --no-camera)")
parser.add_argument("--port",       default=None,            help="Port série (ex: /dev/ttyUSB0)")
parser.add_argument("--baud",       type=int, default=9600,  help="Baudrate (défaut: 9600)")
parser.add_argument("--backend",    default="http://localhost:3000", help="URL du backend")
parser.add_argument("--no-backend", action="store_true",     help="Simule la réponse API (pas besoin de backend)")
args = parser.parse_args()

BACKEND_URL = args.backend

# ─────────────────────────────────────────────
# SERIAL TRANSFER (copie minimale de serial_transfer.py)
# ─────────────────────────────────────────────

class SerialTransfer:
    START_BYTE      = 0x7E
    STOP_BYTE       = 0x81
    MAX_PACKET_SIZE = 0xFE

    PID_PING            = 0x00
    PID_LOCAL_ORDER     = 0x04
    PID_COLOR_LIST      = 0x05
    PID_COMPLETED_COUNT = 0x06
    PID_ITEM_INFO       = 0x10
    PID_SCAN_RESULT     = 0x11
    PID_SENSOR_STATUS   = 0x12
    PID_STATUS          = 0xFE

    STATUS_READY       = 0x00
    STATUS_BUSY        = 0x01
    STATUS_DONE        = 0x02
    STATUS_SCAN_NEEDED = 0x03

    def __init__(self, port):
        self.port = port
        self._buf = bytearray()
        self._generate_crc_table()

    def _generate_crc_table(self):
        table = [0] * 256
        poly  = 0x9B
        for i in range(256):
            curr = i
            for _ in range(8):
                curr = ((curr << 1) ^ poly) if (curr & 0x80) else (curr << 1)
            table[i] = curr & 0xFF
        self._crc_table = table

    def _crc8(self, data):
        crc = 0
        for b in data:
            crc = self._crc_table[crc ^ b]
        return crc

    def _cobs_encode(self, data):
        overhead = 0xFF
        for i, b in enumerate(data):
            if b == self.START_BYTE:
                overhead = i
                break
        ref_byte = -1
        for i in range(len(data) - 1, -1, -1):
            if data[i] == self.START_BYTE:
                ref_byte = i
                break
        if ref_byte != -1:
            for i in range(len(data) - 1, -1, -1):
                if data[i] == self.START_BYTE:
                    data[i]  = ref_byte - i
                    ref_byte = i
        return overhead

    def _cobs_decode(self, data, overhead):
        if overhead <= self.MAX_PACKET_SIZE:
            idx = overhead
            while data[idx]:
                delta    = data[idx]
                data[idx] = self.START_BYTE
                idx      += delta
            data[idx] = self.START_BYTE

    def send(self, packet_id, payload):
        payload  = list(payload)
        if len(payload) > self.MAX_PACKET_SIZE:
            payload = payload[:self.MAX_PACKET_SIZE]
        overhead = self._cobs_encode(payload)
        crc_val  = self._crc8(payload)
        packet   = bytes([self.START_BYTE, packet_id, overhead, len(payload)])
        packet  += bytes(payload)
        packet  += bytes([crc_val, self.STOP_BYTE])
        self.port.write(packet)
        self.port.flush()

    def available(self):
        while self.port.in_waiting:
            b = self.port.read(1)
            if b:
                self._buf.append(b[0])

        if len(self._buf) < 6:
            return None

        # Resync sur START_BYTE
        start_idx = next((i for i, b in enumerate(self._buf) if b == self.START_BYTE), -1)
        if start_idx == -1:
            self._buf.clear()
            return None
        if start_idx > 0:
            print(f"  [WARN] Desync: on jette {start_idx} bytes parasites")
            del self._buf[:start_idx]

        if len(self._buf) < 6:
            return None

        packet_id   = self._buf[1]
        overhead    = self._buf[2]
        payload_len = self._buf[3]

        if payload_len == 0 or payload_len > self.MAX_PACKET_SIZE:
            del self._buf[0]
            return None

        total_len = 4 + payload_len + 2
        if len(self._buf) < total_len:
            return None

        payload   = list(self._buf[4:4 + payload_len])
        crc_rx    = self._buf[4 + payload_len]
        stop_byte = self._buf[4 + payload_len + 1]

        del self._buf[:total_len]

        if stop_byte != self.STOP_BYTE:
            print("  [WARN] Stop byte incorrect, trame ignoree")
            return None

        if self._crc8(bytes(payload)) != crc_rx:
            print("  [WARN] CRC invalide, trame ignoree")
            return None

        self._cobs_decode(payload, overhead)
        return (packet_id, bytes(payload))


# ─────────────────────────────────────────────
# CAMERA (réelle ou simulée)
# ─────────────────────────────────────────────

def init_camera():
    """Initialise la PiCamera2. Retourne None si --no-camera."""
    if args.no_camera:
        print("[CAM] Mode simulation (pas de camera physique)")
        return None
    try:
        from picamera2 import Picamera2
        cam = Picamera2()
        config = cam.create_preview_configuration()
        config["main"]["size"]   = (640, 480)
        config["main"]["format"] = "RGB888"
        cam.configure(config)
        cam.start()
        time.sleep(0.5)
        cam.set_controls({
            "AwbEnable":    False,
            "ExposureTime": 9000,
            "AnalogueGain": 1.0,
            "ColourGains":  (1.3, 1.7),
            "Saturation":   0.9
        })
        time.sleep(1.0)
        print("[CAM] Camera initialisee")
        return cam
    except Exception as e:
        print(f"[CAM] Erreur init camera: {e}")
        print("[CAM] Passage en mode simulation automatique")
        return None


def scan_frame(cam):
    """
    Capture + décode un frame.
    Retourne (qr_text, hue, sat, val) ou (None, 0, 0, 0).
    En mode simulé, retourne des valeurs fixes.
    """
    if cam is None:
        # --- MODE SIMULÉ ---
        qr   = args.qr
        hue  = 30   # teinte orange-ish
        sat  = 200
        val  = 180
        print(f"  [CAM SIM] QR={qr} H={hue} S={sat} V={val}")
        return qr, hue, sat, val

    # --- MODE RÉEL ---
    try:
        import cv2
        import numpy as np
        from pyzbar.pyzbar import decode

        frame_rgb = cam.capture_array()
        bgr  = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2BGR)
        hsv  = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2HSV)
        h, w = frame_rgb.shape[:2]

        results = decode(frame_rgb)
        if not results:
            return None, 0, 0, 0

        obj    = results[0]
        qr_text = obj.data.decode("utf-8")

        rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
        cy          = ry + rh // 2
        patch_size  = 16

        test_points = [
            (rx - 15 - patch_size, cy - patch_size // 2),
            (rx + rw + 15,         cy - patch_size // 2),
        ]

        hues, sats, vals = [], [], []
        for sx, sy in test_points:
            if (0 <= sx <= w - patch_size) and (0 <= sy <= h - patch_size):
                patch = hsv[sy:sy + patch_size, sx:sx + patch_size]
                hues.append(int(np.median(patch[:, :, 0])))
                sats.append(int(np.median(patch[:, :, 1])))
                vals.append(int(np.median(patch[:, :, 2])))

        if not hues:
            return qr_text, 0, 0, 0

        return (
            qr_text,
            int(sum(hues) / len(hues)),
            int(sum(sats) / len(sats)),
            int(sum(vals) / len(vals)),
        )
    except Exception as e:
        print(f"  [CAM ERR] {e}")
        return None, 0, 0, 0


# ─────────────────────────────────────────────
# API BACKEND
# ─────────────────────────────────────────────

def api_post_scan(qr_text, hue, sat, val):
    """
    POST /api/scans → retourne (itemId, decision, orderId, color, team) ou None.
    En mode --no-backend, simule une réponse ORDER.
    """
    if args.no_backend:
        # Simulation : on retourne une réponse fictive
        fake = {
            "itemId":   42,
            "decision": "ORDER",
            "orderId":  7,
            "color":    "blue",
            "team":     {"id": 1, "name": "Equipe A"}
        }
        print(f"  [API SIM] Reponse simulee: {fake}")
        return fake

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
            print(f"  [API ERR] POST /scans -> HTTP {r.status_code}: {r.text}")
            return None

        data = r.json()
        print(f"  [API OK]  Reponse: {data}")
        return data

    except requests.exceptions.ConnectionError:
        print(f"  [API ERR] Impossible de joindre {BACKEND_URL}")
        print(f"            Utilise --no-backend pour simuler")
        return None
    except Exception as e:
        print(f"  [API ERR] {e}")
        return None


def api_patch_status(item_id, status_str):
    """
    PATCH /api/items/{id}/status → retourne les données ou None.
    """
    if args.no_backend:
        fake = {"completedOrdersCount": 3}
        print(f"  [API SIM] PATCH status simule: {fake}")
        return fake

    try:
        r = requests.patch(
            f"{BACKEND_URL}/api/items/{item_id}/status",
            json={"status": {"status": status_str}},
            timeout=5
        )
        data = r.json()
        print(f"  [API OK]  PATCH /items/{item_id}/status -> {data}")
        return data
    except Exception as e:
        print(f"  [API ERR] PATCH status: {e}")
        return None


# ─────────────────────────────────────────────
# HANDLERS TRAMES ARDUINO
# ─────────────────────────────────────────────

def handle_scan_needed(st, cam):
    """Bloc détecté par l'Arduino -> scan -> API -> réponse à l'Arduino."""
    print("\n" + "="*50)
    print("[ARDUINO] SCAN NEEDED -> debut du traitement")

    # 1. Scan
    qr_text, hue, sat, val = scan_frame(cam)
    if qr_text is None:
        print("  [!] QR non detecte - on abandonne ce bloc")
        # En prod: il faudrait renvoyer un signal à l'Arduino pour relancer
        return

    print(f"  [SCAN]   QR={qr_text}  H={hue}  S={sat}  V={val}")

    # 2. API
    data = api_post_scan(qr_text, hue, sat, val)
    if data is None:
        print("  [!] Pas de reponse backend - bloc ignore")
        return

    # 3. Extraction des données
    item_id      = data.get("itemId", 0)
    decision_str = data.get("decision", "PASS")
    order_id     = data.get("orderId") or 0
    color_raw    = (data.get("color") or "").lower()
    team_raw     = data.get("team")
    team_id      = team_raw.get("id", 0) if isinstance(team_raw, dict) else 0

    decision_byte = {"ORDER": 0x01, "STOCK": 0x02}.get(decision_str, 0x00)

    # 4. Envoi à l'Arduino
    payload = bytes([
        (item_id >> 8) & 0xFF,
        item_id & 0xFF,
        decision_byte,
        order_id & 0xFF,
        hue & 0xFF,
        sat & 0xFF,
        val & 0xFF,
        team_id & 0xFF,
    ])
    st.send(SerialTransfer.PID_ITEM_INFO, payload)

    print(f"  [ENVOYE] PID_ITEM_INFO -> Item #{item_id}  Decision={decision_str}  Order={order_id}  Team={team_id}")
    print("="*50 + "\n")


def handle_scan_result(st, payload):
    """Arduino confirme le tri -> API PATCH."""
    if len(payload) < 3:
        print("[WARN] ScanResult: payload trop court")
        return

    item_id    = (payload[0] << 8) | payload[1]
    status_str = "CONFIRMED" if payload[2] == 0x00 else "FAILED"

    print(f"\n[ARDUINO] SCAN RESULT: Item #{item_id} -> {status_str}")

    data = api_patch_status(item_id, status_str)
    if data and "completedOrdersCount" in data:
        count   = data["completedOrdersCount"]
        payload = bytes([(count >> 8) & 0xFF, count & 0xFF])
        st.send(SerialTransfer.PID_COMPLETED_COUNT, payload)
        print(f"  [ENVOYE] PID_COMPLETED_COUNT -> {count} commandes completes")


def handle_sensor_status(payload):
    """Affiche l'état des capteurs IR reçu de l'Arduino."""
    if len(payload) < 1:
        return
    mask = payload[0]
    states = ["IR_SCAN", "IR_NEXT", "IR_STOCK", "IR_ORDER", "IR_PASS"]
    active = [states[i] for i in range(5) if mask & (1 << i)]
    print(f"[ARDUINO] Capteurs IR actifs: {active if active else 'aucun'} (mask={hex(mask)})")


def handle_ping(st):
    """Répond à un ping Arduino."""
    print("[ARDUINO] Ping recu -> Pong")
    st.send(SerialTransfer.PID_PING, bytes([0x01]))


# ─────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────

def find_port():
    """Détecte automatiquement le port série de l'Arduino."""
    if args.port:
        return args.port
    candidates = ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1"]
    for c in candidates:
        if os.path.exists(c):
            print(f"[SERIAL] Port auto-detecte: {c}")
            return c
    return None


def main():
    print("=" * 50)
    print("  TEST COMMUNICATION ARDUINO <-> PI <-> BACKEND")
    print("=" * 50)
    print(f"  Backend   : {'SIMULE' if args.no_backend else BACKEND_URL}")
    print(f"  Camera    : {'SIMULEE' if args.no_camera else 'REELLE'}")
    if args.no_camera:
        print(f"  QR simule : {args.qr}")
    print("=" * 50 + "\n")

    # --- Port série ---
    port_path = find_port()
    if port_path is None:
        print("[!] Aucun port serie trouve.")
        print("    Branche l'Arduino ou utilise --port /dev/ttyXXX")
        sys.exit(1)

    try:
        ser = serial.Serial(port_path, args.baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"[!] Impossible d'ouvrir {port_path}: {e}")
        sys.exit(1)

    # --- Attente Arduino prêt (caractère 'R') ---
    print("[BOOT] Attente de l'Arduino (caractere 'R')...")
    t0 = time.time()
    ready = False
    while time.time() - t0 < 15:
        if ser.in_waiting and ser.read(1) == b'R':
            print("[BOOT] Arduino pret !")
            ready = True
            break
        time.sleep(0.1)

    if not ready:
        print("[!] Arduino pas pret apres 15s - verifie le cable et le flash")
        print("    Tu peux aussi taper manuellement 'R' dans le Serial Monitor")
        # On continue quand même pour le test

    st  = SerialTransfer(ser)
    cam = init_camera()

    print("\n[PI] En attente de trames Arduino...\n")
    print("  (Dans le Serial Monitor Arduino: tape '1' pour simuler un bloc)\n")

    # --- Boucle principale ---
    try:
        while True:
            result = st.available()

            if result is not None:
                pid, payload = result

                if pid == SerialTransfer.PID_STATUS:
                    if len(payload) < 1:
                        continue
                    code = payload[0]
                    if code == SerialTransfer.STATUS_SCAN_NEEDED:
                        handle_scan_needed(st, cam)
                    elif code == SerialTransfer.STATUS_DONE:
                        print("[ARDUINO] STATUS: Done")
                    elif code == SerialTransfer.STATUS_BUSY:
                        print("[ARDUINO] STATUS: Busy")
                    elif code == SerialTransfer.STATUS_READY:
                        print("[ARDUINO] STATUS: Ready")

                elif pid == SerialTransfer.PID_SCAN_RESULT:
                    handle_scan_result(st, payload)

                elif pid == SerialTransfer.PID_SENSOR_STATUS:
                    handle_sensor_status(payload)

                elif pid == SerialTransfer.PID_PING:
                    handle_ping(st)

                elif pid == SerialTransfer.PID_LOCAL_ORDER:
                    count = payload[0] if payload else 0
                    print(f"[ARDUINO] LOCAL ORDER recu ({count} lignes) - ignoré dans ce test")

                else:
                    print(f"[WARN] PID inconnu: {hex(pid)}")

            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\n[!] Arret demande")
    finally:
        if cam:
            cam.stop()
        ser.close()
        print("[!] Port serie ferme. Fin du test.")


if __name__ == "__main__":
    main()
