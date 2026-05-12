# ping-test.py — teste UNIQUEMENT le ping/pong Arduino via SerialTransfer
# Aucun backend, aucune camera. Juste le cable USB.
#
# Usage: sudo python ping-test.py

import serial, time, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from serial_transfer import SerialTransfer

PORT = None
for c in ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1"]:
    if os.path.exists(c):
        PORT = c
        break

if not PORT:
    print("PAS DE PORT — branche l'Arduino en USB")
    sys.exit(1)

print(f"Port: {PORT}")
print("Ouverture... (attend 4s pour boot Arduino)")
s = serial.Serial(PORT, 9600, timeout=0.5)
time.sleep(4)
st = SerialTransfer(s)

print("Envoi ping...")
st.send(SerialTransfer.PID_PING, b"\x01")

print("Attente pong...")
for i in range(60):  # 3 sec
    result = st.available()
    if result:
        pid, payload = result
        print(f"  Recu: PID=0x{pid:02X} payload={payload.hex()} ({len(payload)} bytes)")
        if pid == SerialTransfer.PID_PING:
            print("PONG ! Connexion Arduino OK.")
            s.close()
            sys.exit(0)
    time.sleep(0.05)

print("AUCUNE REPONSE — verifie:")
print("  1. final.ino bien flashé ?")
print("  2. Bon port ? (ls /dev/ttyUSB*)")
print("  3. Arduino allumé ? (LED verte ON)")
s.close()
