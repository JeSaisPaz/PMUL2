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
print("Ouverture...")
s = serial.Serial(PORT, 9600, timeout=0.5)

# attend le 'R' de ready du boot Arduino (evite de parler au bootloader)
print("Attente du signal READY de l'Arduino...")
t0 = time.time()
ready = False
while time.time() - t0 < 10:
    if s.in_waiting:
        b = s.read(1)
        if b == b'R':
            ready = True
            print("Arduino pret !")
            break
        # jette tout autre byte (bruit bootloader)
    time.sleep(0.1)

if not ready:
    print("Arduino n'a pas envoye 'R' — branche ? flash ?")
    s.close()
    sys.exit(1)

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
