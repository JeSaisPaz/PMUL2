# Agent: OpenCode (Claude) - AI/TestSuite
# Test: Camera preview live avec QR + detection couleur
#        Utilise GET /api/colors pour les ranges HSV (backend DB)
#        Ctrl+C pour quitter
#
# Usage: python test_camera.py [--host localhost:3000]

import time, signal, sys, requests
import numpy as np
import cv2
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
BACKEND = f"http://{HOST}/api"

running = True

def cleanup(sig, frame):
    global running
    running = False

signal.signal(signal.SIGINT, cleanup)

# charge les couleurs actives depuis la DB
print(f"\n=== CAMERA PREVIEW ===\n  Backend: {BACKEND}\n  Ctrl+C pour quitter\n")
print("  Chargement des couleurs depuis la DB...")

db_colors = []
try:
    r = requests.get(f"{BACKEND}/colors", timeout=5)
    if r.status_code == 200:
        for c in r.json():
            # ne garde que les actives (status: true) avec tous les champs HSV
            if (c.get("status") and
                None not in (c.get("hueMin"), c.get("hueMax"),
                             c.get("saturationMin"), c.get("saturationMax"),
                             c.get("valueMin"), c.get("valueMax"))):
                db_colors.append({
                    "name": c["name"],
                    "hueMin": c["hueMin"], "hueMax": c["hueMax"],
                    "satMin": c["saturationMin"], "satMax": c["saturationMax"],
                    "valMin": c["valueMin"], "valMax": c["valueMax"],
                })
    print(f"  {len(db_colors)} couleurs actives chargees: {[c['name'] for c in db_colors]}")
except Exception as e:
    print(f"  [!] Erreur chargement couleurs: {e}")
    sys.exit(1)

if not db_colors:
    print("  [!] Aucune couleur active en DB")
    sys.exit(1)

def matchColor(hue, sat, val):
    """retourne le nom de la couleur qui matche les ranges de la DB, ou None."""
    for c in db_colors:
        if (c["hueMin"] <= hue <= c["hueMax"] and
            c["satMin"] <= sat <= c["satMax"] and
            c["valMin"] <= val <= c["valMax"]):
            return c["name"]
    return None

cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start_preview(Preview.NULL)
cam.start()
time.sleep(2)

last_qr = None

while running:
    frame = cam.capture_array()
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    qr_results = decode(frame)

    if qr_results:
        obj = qr_results[0]
        qr_text = obj.data.decode("utf-8")
        h, w = frame.shape[:2]

        px = min(obj.rect.left + obj.rect.width + 3, w - 15)
        py = min(obj.rect.top + (obj.rect.height // 2), h - 15)
        patch = hsv[py:py+10, px:px+10]
        avgHue = int(np.mean(patch[:, :, 0]))
        avgSat = int(np.mean(patch[:, :, 1]))
        avgVal = int(np.mean(patch[:, :, 2]))

        color = matchColor(avgHue, avgSat, avgVal)
        color_name = color if color else f"? (H={avgHue})"

        key = f"{qr_text}:{color_name}"
        if key != last_qr:
            print(f"  [DETECT] QR={qr_text} Couleur={color_name} H={avgHue} S={avgSat} V={avgVal}")
            last_qr = key

    time.sleep(0.1)

cam.stop()
cam.stop_preview()
print("Fin.")
