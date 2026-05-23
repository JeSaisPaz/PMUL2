#!/usr/bin/env python3
"""
TEST COMMUNICATION + SCAN + API  —  côté Raspberry Pi
======================================================
Lance ce script sur le Pi, puis utilise le keypad de l'Arduino pour piloter.

MODES :
  python3 test_scan.py                        # tout réel
  python3 test_scan.py --no-camera            # caméra simulée (QR fixe)
  python3 test_scan.py --no-camera --no-backend  # tout simulé
  python3 test_scan.py --no-camera --qr "TON_QR"  # QR custom simulé

OPTIONS :
  --port /dev/ttyUSB0   forcer le port série
  --baud 9600           baudrate (défaut: 9600)
  --backend http://...  URL du backend (défaut: http://localhost:3000)
"""

import serial, time, sys, os, argparse, requests
import threading, io

# ── Arguments ──────────────────────────────────────────────────────────────
p = argparse.ArgumentParser()
p.add_argument("--no-camera",  action="store_true")
p.add_argument("--no-backend", action="store_true")
p.add_argument("--qr",         default="BLOC_TEST_001")
p.add_argument("--port",       default=None)
p.add_argument("--baud",       type=int, default=9600)
p.add_argument("--backend",    default="http://localhost:3000")
args = p.parse_args()

BACKEND = args.backend

# ── Serveur preview (dernière image capturée) ──────────────────────────────
# Accessible sur http://<IP_DU_PI>:5001 pendant que le test tourne
_preview_frame = None
_preview_label = "En attente du premier scan..."
_preview_lock  = threading.Lock()
PREVIEW_PORT   = 5001

def _set_preview(frame_rgb_array, label=""):
    """Appelé après chaque capture pour mettre à jour la preview."""
    global _preview_frame, _preview_label
    try:
        import cv2
        ok, buf = cv2.imencode(".jpg", frame_rgb_array)  # picamera2 RGB888 = déjà BGR pour OpenCV
        if ok:
            with _preview_lock:
                _preview_frame = buf.tobytes()
                _preview_label = label
    except Exception:
        pass

def _start_preview_server():
    try:
        from flask import Flask, Response
        flask_app = Flask("preview")
        import logging
        logging.getLogger("werkzeug").setLevel(logging.ERROR)

        @flask_app.route("/")
        def index():
            with _preview_lock:
                label = _preview_label
            return f"""<html>
            <head>
              <title>Dernier scan</title>
              <meta http-equiv="refresh" content="2">
              <style>
                body {{ margin:0; background:#111; color:#eee; font-family:monospace;
                        display:flex; flex-direction:column; align-items:center;
                        justify-content:center; min-height:100vh; }}
                img  {{ max-width:90vw; border:2px solid #444; margin-top:12px; }}
                p    {{ font-size:1.2em; margin:8px; }}
              </style>
            </head>
            <body>
              <p>{label}</p>
              <img src="/last.jpg?t={time.time()}" />
              <p style="color:#888;font-size:0.8em">Rafraichissement auto toutes les 2s</p>
            </body></html>"""

        @flask_app.route("/last.jpg")
        def last_jpg():
            with _preview_lock:
                frame = _preview_frame
            if frame is None:
                try:
                    import numpy as np, cv2
                    blank = np.full((480, 640, 3), 40, dtype="uint8")
                    cv2.putText(blank, "En attente...", (180, 250),
                                cv2.FONT_HERSHEY_SIMPLEX, 1.2, (200, 200, 200), 2)
                    _, buf = cv2.imencode(".jpg", blank)
                    frame = buf.tobytes()
                except Exception:
                    return Response(b"", mimetype="image/jpeg")
            return Response(frame, mimetype="image/jpeg",
                            headers={"Cache-Control": "no-store"})

        flask_app.run(host="0.0.0.0", port=PREVIEW_PORT, threaded=True)

    except ImportError:
        print("[PREVIEW] Flask non installe - preview desactivee")
        print("[PREVIEW] sudo apt install python3-flask")



# ── SerialTransfer Python ──────────────────────────────────────────────────
class SerialTransfer:
    START_BYTE = 0x7E
    STOP_BYTE  = 0x81
    MAX_SIZE   = 0xFE

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
        self._build_crc_table()

    def _build_crc_table(self):
        t, poly = [0]*256, 0x9B
        for i in range(256):
            c = i
            for _ in range(8):
                c = ((c << 1) ^ poly) if c & 0x80 else c << 1
            t[i] = c & 0xFF
        self._crc = t

    def _crc8(self, data):
        v = 0
        for b in data: v = self._crc[v ^ b]
        return v

    def _cobs_encode(self, data):
        overhead = 0xFF
        for i, b in enumerate(data):
            if b == self.START_BYTE: overhead = i; break
        ref = -1
        for i in range(len(data)-1, -1, -1):
            if data[i] == self.START_BYTE: ref = i; break
        if ref != -1:
            for i in range(len(data)-1, -1, -1):
                if data[i] == self.START_BYTE:
                    data[i] = ref - i; ref = i
        return overhead

    def _cobs_decode(self, data, overhead):
        if overhead <= self.MAX_SIZE:
            idx = overhead
            while data[idx]:
                d = data[idx]; data[idx] = self.START_BYTE; idx += d
            data[idx] = self.START_BYTE

    def send(self, pid, payload):
        pl = list(payload)[:self.MAX_SIZE]
        oh = self._cobs_encode(pl)
        crc = self._crc8(pl)
        pkt = bytes([self.START_BYTE, pid, oh, len(pl)]) + bytes(pl) + bytes([crc, self.STOP_BYTE])
        self.port.write(pkt); self.port.flush()

    def receive(self):
        """Retourne (pid, payload:bytes) ou None."""
        while self.port.in_waiting:
            b = self.port.read(1)
            if b: self._buf.append(b[0])

        if len(self._buf) < 6: return None

        # resync
        si = next((i for i, b in enumerate(self._buf) if b == self.START_BYTE), -1)
        if si == -1: self._buf.clear(); return None
        if si > 0:
            print(f"  [WARN] Desync: {si} bytes ignores")
            del self._buf[:si]
        if len(self._buf) < 6: return None

        pid   = self._buf[1]
        oh    = self._buf[2]
        plen  = self._buf[3]

        if plen == 0 or plen > self.MAX_SIZE:
            del self._buf[0]; return None

        total = 4 + plen + 2
        if len(self._buf) < total: return None

        pl        = list(self._buf[4:4+plen])
        crc_rx    = self._buf[4+plen]
        stop      = self._buf[4+plen+1]
        del self._buf[:total]

        if stop != self.STOP_BYTE:
            print("  [WARN] Stop byte invalide"); return None
        if self._crc8(bytes(pl)) != crc_rx:
            print("  [WARN] CRC invalide"); return None

        self._cobs_decode(pl, oh)
        return (pid, bytes(pl))

# ── Caméra ─────────────────────────────────────────────────────────────────
def init_camera():
    if args.no_camera:
        print("[CAM] Mode simulation")
        return None
    try:
        from picamera2 import Picamera2
        cam = Picamera2()
        cfg = cam.create_preview_configuration()
        cfg["main"]["size"]   = (640, 480)
        cfg["main"]["format"] = "RGB888"
        cam.configure(cfg); cam.start(); time.sleep(0.5)
        cam.set_controls({
            "AwbEnable":    True,   # AWB auto - plus fiable que gains manuels
            "AwbMode":      1,      # 1=Indoor, adapte a la lumiere artificielle
            "Saturation":   1.2,    # Leger boost couleur pour mieux distinguer
            "Sharpness":    1.5,
        })
        time.sleep(1.0)
        print("[CAM] Initialisée")
        return cam
    except Exception as e:
        print(f"[CAM] Erreur: {e} → mode simulation")
        return None

def scan_frame(cam):
    """Retourne (qr, hue, sat, val) ou (None,0,0,0)."""
    if cam is None:
        print(f"  [CAM SIM] QR={args.qr} H=30 S=200 V=180")
        return args.qr, 30, 200, 180
    try:
        import cv2, numpy as np
        from pyzbar.pyzbar import decode
        frame = cam.capture_array()
        hsv   = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)  # picamera2 RGB888 = BGR pour OpenCV
        h, w  = frame.shape[:2]
        res   = decode(frame)
        if not res:
            _set_preview(frame, "RATE - QR non detecte")
            return None, 0, 0, 0
        obj  = res[0]
        qr   = obj.data.decode("utf-8")
        rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
        cy, ps = ry + rh//2, 16
        pts = [(rx-15-ps, cy-ps//2), (rx+rw+15, cy-ps//2)]
        hs, ss, vs = [], [], []
        for sx, sy in pts:
            if 0<=sx<=w-ps and 0<=sy<=h-ps:
                patch = hsv[sy:sy+ps, sx:sx+ps]
                hs.append(int(np.median(patch[:,:,0])))
                ss.append(int(np.median(patch[:,:,1])))
                vs.append(int(np.median(patch[:,:,2])))
        if not hs:
            _set_preview(frame, f"OK QR={qr} | HSV introuvable")
            return qr, 0, 0, 0
        avg_h = int(sum(hs)/len(hs))
        avg_s = int(sum(ss)/len(ss))
        avg_v = int(sum(vs)/len(vs))
        _set_preview(frame, f"OK  QR={qr}  H={avg_h} S={avg_s} V={avg_v}")
        return qr, avg_h, avg_s, avg_v
    except Exception as e:
        print(f"  [CAM ERR] {e}"); return None, 0, 0, 0

# ── API ────────────────────────────────────────────────────────────────────
def api_post_scan(qr, hue, sat, val):
    if args.no_backend:
        fake = {"itemId": 42, "decision": "ORDER", "orderId": 7,
                "color": "blue", "team": {"id": 1}}
        print(f"  [API SIM] {fake}"); return fake
    try:
        r = requests.post(f"{BACKEND}/api/scans",
                          json={"scan": {"qrValue": qr, "hue": hue,
                                         "saturation": sat, "value": val}}, timeout=5)
        if r.status_code != 201:
            print(f"  [API ERR] POST /scans → {r.status_code}: {r.text}"); return None
        data = r.json(); print(f"  [API OK] {data}"); return data
    except requests.exceptions.ConnectionError:
        print(f"  [API ERR] Impossible de joindre {BACKEND}")
        print(f"            Relance avec --no-backend pour simuler"); return None
    except Exception as e:
        print(f"  [API ERR] {e}"); return None

def api_patch_status(item_id, status_str):
    if args.no_backend:
        fake = {"completedOrdersCount": 3}
        print(f"  [API SIM] PATCH → {fake}"); return fake
    try:
        r = requests.patch(f"{BACKEND}/api/items/{item_id}/status",
                           json={"status": {"status": status_str}}, timeout=5)
        data = r.json(); print(f"  [API OK] PATCH → {data}"); return data
    except Exception as e:
        print(f"  [API ERR] PATCH: {e}"); return None

# ── Handlers ───────────────────────────────────────────────────────────────
def handle_scan_needed(st, cam):
    print("\n" + "="*50)
    print("[ARDUINO] SCAN NEEDED")

    qr, hue, sat, val = scan_frame(cam)
    if qr is None:
        print("  [!] QR non détecté"); return

    print(f"  [SCAN] QR={qr}  H={hue}  S={sat}  V={val}")

    data = api_post_scan(qr, hue, sat, val)
    if data is None:
        print("  [!] Pas de réponse backend"); return

    item_id  = data.get("itemId", 0)
    decision = data.get("decision", "PASS")
    order_id = data.get("orderId") or 0
    team_raw = data.get("team")
    team_id  = team_raw.get("id", 0) if isinstance(team_raw, dict) else 0
    dec_byte = {"ORDER": 0x01, "STOCK": 0x02}.get(decision, 0x00)

    payload = bytes([
        (item_id >> 8) & 0xFF, item_id & 0xFF,
        dec_byte, order_id & 0xFF,
        hue & 0xFF, sat & 0xFF, val & 0xFF, team_id & 0xFF,
    ])
    st.send(SerialTransfer.PID_ITEM_INFO, payload)
    print(f"  [ENVOYE] Item #{item_id}  Decision={decision}  Order={order_id}")
    print("="*50 + "\n")

def handle_scan_result(st, payload):
    if len(payload) < 3: return
    item_id = (payload[0] << 8) | payload[1]
    status  = "CONFIRMED" if payload[2] == 0x00 else "FAILED"
    print(f"\n[ARDUINO] SCAN RESULT: Item #{item_id} → {status}")
    data = api_patch_status(item_id, status)
    if data and "completedOrdersCount" in data:
        count = data["completedOrdersCount"]
        st.send(SerialTransfer.PID_COMPLETED_COUNT,
                bytes([(count>>8)&0xFF, count&0xFF]))
        print(f"  [ENVOYE] COMPLETED_COUNT={count}")

def handle_sensor_status(payload):
    if not payload: return
    mask   = payload[0]
    labels = ["IR_SCAN","IR_NEXT","IR_STOCK","IR_ORDER","IR_PASS"]
    active = [labels[i] for i in range(5) if mask & (1<<i)]
    print(f"[ARDUINO] Capteurs: {active or 'aucun'} (mask={hex(mask)})")

# ── Main ───────────────────────────────────────────────────────────────────
def find_port():
    if args.port: return args.port
    for c in ["/dev/ttyUSB0","/dev/ttyUSB1","/dev/ttyACM0","/dev/ttyACM1"]:
        if os.path.exists(c):
            print(f"[SERIAL] Port: {c}"); return c
    return None

def main():
    print("="*50)
    print("  TEST COMMUNICATION ARDUINO <-> PI <-> BACKEND")
    print("="*50)
    print(f"  Backend  : {'SIMULE' if args.no_backend else BACKEND}")
    print(f"  Camera   : {'SIMULEE (QR='+args.qr+')' if args.no_camera else 'REELLE'}")
    print("="*50)
    print()
    print("  Sur l'Arduino (keypad) :")
    print("    1  → Simuler bloc détecté")
    print("    2  → Confirmer CONFIRMED")
    print("    3  → Confirmer FAILED")
    print("    4  → Envoyer état capteurs IR")
    print("    A  → Afficher dernier item sur LCD")
    print("    B  → Retour accueil LCD")
    print("    #  → Ping")
    print()

    port_path = find_port()
    if port_path is None:
        print("[!] Aucun port série trouvé. Utilise --port /dev/ttyXXX"); sys.exit(1)

    try:
        ser = serial.Serial(port_path, args.baud, timeout=0.5)
    except serial.SerialException as e:
        print(f"[!] Impossible d'ouvrir {port_path}: {e}"); sys.exit(1)

    print("[BOOT] Attente Arduino (caractère 'R')...")
    t0 = time.time()
    while time.time() - t0 < 15:
        if ser.in_waiting and ser.read(1) == b'R':
            print("[BOOT] Arduino prêt !"); break
        time.sleep(0.1)
    else:
        print("[WARN] Pas de 'R' reçu - on continue quand même")

    # Démarrage du serveur preview en arrière-plan
    t_preview = threading.Thread(target=_start_preview_server, daemon=True)
    t_preview.start()
    print(f"[PREVIEW] http://<IP_DU_PI>:{PREVIEW_PORT}  (derniere image capturee)")

    st  = SerialTransfer(ser)
    cam = init_camera()

    print("\n[PI] En attente de trames Arduino...\n")

    try:
        while True:
            result = st.receive()
            if result is not None:
                pid, payload = result

                if pid == SerialTransfer.PID_STATUS:
                    if not payload: continue
                    code = payload[0]
                    if   code == SerialTransfer.STATUS_SCAN_NEEDED: handle_scan_needed(st, cam)
                    elif code == SerialTransfer.STATUS_DONE:         print("[ARDUINO] STATUS: Done")
                    elif code == SerialTransfer.STATUS_BUSY:         print("[ARDUINO] STATUS: Busy")
                    elif code == SerialTransfer.STATUS_READY:        print("[ARDUINO] STATUS: Ready")

                elif pid == SerialTransfer.PID_SCAN_RESULT:
                    handle_scan_result(st, payload)

                elif pid == SerialTransfer.PID_SENSOR_STATUS:
                    handle_sensor_status(payload)

                elif pid == SerialTransfer.PID_PING:
                    st.send(SerialTransfer.PID_PING, bytes([0x01]))
                    print("[ARDUINO] Ping → Pong")

                elif pid == SerialTransfer.PID_LOCAL_ORDER:
                    count = payload[0] if payload else 0
                    print(f"[ARDUINO] LOCAL_ORDER reçu ({count} lignes) — ignoré en mode test")

                else:
                    print(f"[WARN] PID inconnu: {hex(pid)}")

            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\n[!] Arrêt")
    finally:
        if cam: cam.stop()
        ser.close()
        print("[!] Port fermé.")

if __name__ == "__main__":
    main()
