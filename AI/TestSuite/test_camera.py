# Agent: OpenCode (Claude) - AI/TestSuite
# Test: Camera preview live avec QR + detection couleur
#        Affiche le flux camera avec overlay de detection
#        Appuie sur 'q' pour quitter
#
# Usage: python test_camera.py

import time
import numpy as np
import cv2
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview

print("\n=== CAMERA PREVIEW ===\n")
print("  q = quitter\n")

cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start()
time.sleep(2)

while True:
    frame = cam.capture_array()

    # OpenCV est en BGR, pyzbar veut du BGR
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    qr_results = decode(frame)

    for obj in qr_results:
        # dessine un rectangle autour du QR
        pts = obj.polygon
        if len(pts) >= 4:
            pts_np = np.array([(p.x, p.y) for p in pts], np.int32)
            cv2.polylines(frame, [pts_np], True, (0, 255, 0), 2)

        # texte du QR
        qr_text = obj.data.decode("utf-8")
        cx, cy = obj.rect.left, obj.rect.top - 10
        cv2.putText(frame, qr_text, (cx, cy), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

        # detection couleur: patch a droite du QR
        h, w = frame.shape[:2]
        px = min(obj.rect.left + obj.rect.width + 3, w - 15)
        py = min(obj.rect.top + (obj.rect.height // 2), h - 15)

        # dessine le rectangle du patch de detection
        cv2.rectangle(frame, (px, py), (px+10, py+10), (255, 0, 0), 2)

        patch = hsv[py:py+10, px:px+10]
        avgHue   = int(np.mean(patch[:, :, 0]))
        avgSat   = int(np.mean(patch[:, :, 1]))
        avgVal   = int(np.mean(patch[:, :, 2]))

        # determine la couleur
        if 25 <= avgHue < 35:
            color = "Jaune"
        elif 85 <= avgHue < 105:
            color = "Bleu"
        elif 140 <= avgHue < 160:
            color = "Magenta"
        elif 35 <= avgHue < 85:
            color = "Vert"
        elif 0 <= avgHue < 15 or 160 <= avgHue <= 180:
            color = "Rouge"
        elif 10 <= avgHue < 25:
            color = "Orange"
        else:
            color = f"H={avgHue}"

        # affiche la couleur detectee
        cv2.putText(frame, f"{color} (H:{avgHue} S:{avgSat} V:{avgVal})",
                    (px, py-5), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 0), 1)

    cv2.imshow("Camera - QR + Couleur (q=quitter)", frame)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cam.stop()
cv2.destroyAllWindows()
print("Fin.")
