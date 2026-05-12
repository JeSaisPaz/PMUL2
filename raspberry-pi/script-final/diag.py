# diag.py — verifie toute la chaine de com en une commande
#   Arduino <-> Pi (SerialTransfer ping/pong, meme pattern que final.py)
#   Pi <-> Backend (HTTP + Socket.IO)
#
# Usage: sudo python diag.py

import os, sys, time, serial, socketio, requests
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from serial_transfer import SerialTransfer

BACKEND_URL = "http://localhost:3000"

# --- 1. port serie ---

PORT_CANDIDATES = ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1",
                   "/dev/serial0", "/dev/ttyAMA0"]

port = None
for c in PORT_CANDIDATES:
    if os.path.exists(c):
        port = c
        break

if port is None:
    print("[DIAG] SERIAL  : PAS TROUVE")
    print("        Essaie : ls /dev/tty*  |  dmesg | tail -10")
else:
    print(f"[DIAG] SERIAL  : port detecte -> {port}")

# --- 2. ping Arduino via SerialTransfer (meme logique que final.py) ---

arduino_ok = False

if port:
    try:
        s = serial.Serial(port, 9600, timeout=0.5)

        # attend le 'R' de ready (l'Arduino l'envoie a la fin de setup())
        # comme ca on parle pas au bootloader
        print("[DIAG] ARDUINO : attente du signal READY...")
        t0 = time.time()
        ready = False
        while time.time() - t0 < 10:
            if s.in_waiting:
                b = s.read(1)
                if b == b'R':
                    ready = True
                    break
            time.sleep(0.1)

        if not ready:
            print("[DIAG] ARDUINO : pas de 'R' — branche ? flashe ?")
        else:
            st = SerialTransfer(s)
            st.send(SerialTransfer.PID_PING, b"\x01")

            t0 = time.time()
            while time.time() - t0 < 3:
                result = st.available()
                if result and result[0] == SerialTransfer.PID_PING:
                    arduino_ok = True
                    print("[DIAG] ARDUINO : pong recu !")
                    break
                time.sleep(0.05)

            if not arduino_ok:
                print("[DIAG] ARDUINO : pas de reponse au ping")
                print("        Verifie : final.ino flashe ? Bon port ? Bon baud ?")

        s.close()
    except Exception as e:
        print(f"[DIAG] SERIAL  : erreur -> {e}")

# --- 3. backend HTTP ---

try:
    r = requests.get(f"{BACKEND_URL}/api/health", timeout=3)
    if r.status_code == 200:
        data = r.json()
        print(f"[DIAG] BACKEND : UP ({data.get('status', '?')}, {data.get('timestamp', '?')})")
    else:
        print(f"[DIAG] BACKEND : HTTP {r.status_code}")
except Exception as e:
    print(f"[DIAG] BACKEND : injoignable — {e}")
    print("        Lance : cd ~/PMUL2/web/PMUL2/pmul2-team01-app && npm start")

# --- 4. backend Socket.IO ---

sio = socketio.Client()

@sio.on('connect')
def on_connect():
    pass

try:
    sio.connect(BACKEND_URL, wait_timeout=3)
    sio.disconnect()
    print("[DIAG] SOCKET  : connecte au backend")
except Exception as e:
    print(f"[DIAG] SOCKET  : echec — {e}")
    print("        Verifie que le backend tourne (npm start)")

# --- resume ---
print()
ok = all([
    port is not None,
    arduino_ok,
])
if ok:
    print("[DIAG] Tout est pret — lance sudo python final.py")
else:
    print("[DIAG] Des checks ont echoue, relis les erreurs ci-dessus")
