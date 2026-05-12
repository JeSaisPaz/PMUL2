# diag.py — verifie toute la chaine de com en une commande
#   Arduino <-> Pi (SerialTransfer ping/pong)
#   Pi <-> Backend (HTTP + Socket.IO)

import os, sys, time, serial, socketio, requests
from serial_transfer import SerialTransfer

BACKEND_URL = "http://localhost:3000"

# 1. port serie

PORT_CANDIDATES = ["/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1",
                   "/dev/serial0", "/dev/ttyAMA0"]

port = None
for c in PORT_CANDIDATES:
    if os.path.exists(c):
        port = c
        break

if port is None:
    print("[DIAG] SERIAL  : PAS TROUVE — aucun port serie")
    print("        Essaie : ls /dev/tty*  |  dmesg | tail -10")
else:
    print(f"[DIAG] SERIAL  : port detecte -> {port}")

# 2. ping Arduino via SerialTransfer

if port:
    try:
        s = serial.Serial(port, 9600, timeout=1)
        time.sleep(3)  # le MEGA met ~2s a booter apres reset USB
        st = SerialTransfer(s)

        # envoie le ping (PID_PING, 1 byte — 0 bytes est rejete par SerialTransfer)
        st.send(SerialTransfer.PID_PING, b"\x01")

        # attend la reponse (max 4 sec, retry toutes les 1.5s)
        ok = False
        for attempt in range(3):
            st.send(SerialTransfer.PID_PING, b"")
            t0 = time.time()
            while time.time() - t0 < 1.5:
                result = st.available()
                if result and result[0] == SerialTransfer.PID_PING:
                    ok = True
                    break
                time.sleep(0.05)
            if ok:
                break

        if ok:
            print("[DIAG] ARDUINO : pong recu !")
        else:
            print("[DIAG] ARDUINO : pas de reponse (ping envoye, rien recu en 2s)")
            print("        Verifie : final.ino flashe ? Bon port ? Bon baud ?")

        s.close()
    except Exception as e:
        print(f"[DIAG] SERIAL  : erreur -> {e}")

# 3. backend HTTP

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

# 4. backend Socket.IO

sio = socketio.Client()
sio_ok = [False]

@sio.on('connect')
def on_connect():
    sio_ok[0] = True

try:
    sio.connect(BACKEND_URL, wait_timeout=3)
    sio.disconnect()
    print("[DIAG] SOCKET  : connecte au backend")
except Exception as e:
    print(f"[DIAG] SOCKET  : echec — {e}")
    print("        Verifie que le backend tourne (npm start)")

print()
print("[DIAG] TOUT EST OK, SI TU LIS CA C'EST BON TU PEUX ETRE HEUREUX")
