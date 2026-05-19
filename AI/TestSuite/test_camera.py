import time
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview

COLOR_RANGES = [
    ("Red",      [(0, 10),   (170, 179)],  (100, 255), (50, 255)),
    ("Yellow",   [(21, 35)],               (100, 255), (80, 255)),
    ("Green",    [(40, 85)],               (80, 255),  (40, 255)),
    ("Cyan",     [(86, 105)],              (100, 255), (50, 255)),
    ("Blue",     [(106, 135)],             (100, 255), (40, 255)),
    ("Magenta",  [(140, 170)],             (80, 255),  (40, 255)),
]

def match_color(h, s, v):
    for name, hue_ranges, (s_lo, s_hi), (v_lo, v_hi) in COLOR_RANGES:
        if not (s_lo <= s <= s_hi and v_lo <= v <= v_hi):
            continue
        for (h_lo, h_hi) in hue_ranges:
            if h_lo <= h <= h_hi:
                return name
    return None

print("[CAM] Initialisation...")
cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start_preview(Preview.DRM)
cam.start()
time.sleep(2)

print("[CAM] Prete — Ctrl+C pour quitter")
print(f"{'QR':<16} {'H':>4} {'S':>4} {'V':>4}  Couleur")
print("-" * 40)

last_qr = None

try:
    while True:
        raw = cam.capture_array()
        if raw is None:
            time.sleep(0.05)
            continue

        frame = np.ascontiguousarray(raw[:, :, :3]).copy()
        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        qr_results = decode(frame)

        for obj in qr_results:
            try:
                qr_text = obj.data.decode("utf-8")
            except Exception:
                continue

            h, w = frame.shape[:2]
            px = min(obj.rect.left + obj.rect.width + 3, w - 15)
            py = min(obj.rect.top + (obj.rect.height // 2), h - 15)

            patch = hsv[py:py + 10, px:px + 10]
            if patch.size == 0:
                continue

            avg_h = int(np.mean(patch[:, :, 0]))
            avg_s = int(np.mean(patch[:, :, 1]))
            avg_v = int(np.mean(patch[:, :, 2]))
            color = match_color(avg_h, avg_s, avg_v) or "?"

            if qr_text != last_qr:
                print(f"{qr_text:<16} {avg_h:>4} {avg_s:>4} {avg_v:>4}  {color}")
                last_qr = qr_text

        if not qr_results:
            last_qr = None

        time.sleep(0.1)

except KeyboardInterrupt:
    pass

cam.stop_preview()
cam.stop()
print("[CAM] Arret.")
