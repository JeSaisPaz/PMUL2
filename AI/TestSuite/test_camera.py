# Agent: OpenCode (Claude) - AI/TestSuite
# Test: Camera preview live avec QR + detection couleur
#        Affiche le flux camera via DRM (pas besoin de X11)
#        et log les detections dans le terminal
#        Ctrl+C pour quitter
#
# Usage: python test_camera.py

import time, signal, sys
import numpy as np
import cv2
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview

running = True

def cleanup(sig, frame):
    global running
    running = False

signal.signal(signal.SIGINT, cleanup)

print("\n=== CAMERA PREVIEW (DRM) ===\n")
print("  Ctrl+C pour quitter\n")

cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start_preview(Preview.DRM)
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

        # patch a droite du QR
        px = min(obj.rect.left + obj.rect.width + 3, w - 15)
        py = min(obj.rect.top + (obj.rect.height // 2), h - 15)
        patch = hsv[py:py+10, px:px+10]
        avgHue = int(np.mean(patch[:, :, 0]))
        avgSat = int(np.mean(patch[:, :, 1]))
        avgVal = int(np.mean(patch[:, :, 2]))

        # determine la couleur
        if 25 <= avgHue < 35:       color = "Jaune"
        elif 85 <= avgHue < 105:    color = "Bleu"
        elif 140 <= avgHue < 160:   color = "Magenta"
        elif 35 <= avgHue < 85:     color = "Vert"
        elif 0 <= avgHue < 15 or 160 <= avgHue <= 180: color = "Rouge"
        elif 10 <= avgHue < 25:     color = "Orange"
        else:                        color = f"? (H={avgHue})"

        key = f"{qr_text}:{color}"
        if key != last_qr:
            print(f"  [DETECT] QR={qr_text} Couleur={color} H={avgHue} S={avgSat} V={avgVal}")
            last_qr = key

    time.sleep(0.1)

cam.stop()
cam.stop_preview()
print("Fin.")
