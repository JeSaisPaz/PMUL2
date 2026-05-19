import time
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

COLOR_RANGES = {
    "Red":      ((0, 15),     (100, 255), (50, 255)),
    "Yellow":   ((20, 35),    (100, 255), (100, 255)),
    "Green":    ((40, 80),    (100, 255), (50, 255)),
    "Cyan":     ((85, 105),   (100, 255), (50, 255)),
    "Blue":     ((100, 130),  (100, 255), (50, 255)),
    "Magenta":  ((140, 165),  (100, 255), (50, 255)),
}

def match_color(h, s, v):
    for name, ((h_lo, h_hi), (s_lo, s_hi), (v_lo, v_hi)) in COLOR_RANGES.items():
        if h_lo <= h <= h_hi and s_lo <= s <= s_hi and v_lo <= v <= v_hi:
            return name
    return None

print("[CAM] Initialisation...")
cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start()
time.sleep(2)

print("[CAM] Pret — 'q' pour quitter")
print(f"{'QR':<16} {'H':>4} {'S':>4} {'V':>4}  Couleur")
print("-" * 40)

last_qr = None

while True:
    raw = cam.capture_array()
    if raw is None:
        time.sleep(0.05)
        continue

    frame = np.ascontiguousarray(raw[:, :, :3]).copy()
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    qr_results = decode(frame)

    for obj in qr_results:
        pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32).reshape((-1, 1, 2))
        cv2.polylines(frame, [pts], True, (0, 255, 0), 2)

        try:
            qr_text = obj.data.decode("utf-8")
        except Exception:
            continue

        h, w = frame.shape[:2]
        px = min(obj.rect.left + obj.rect.width + 3, w - 15)
        py = min(obj.rect.top + (obj.rect.height // 2), h - 15)
        cv2.rectangle(frame, (px, py), (px + 10, py + 10), (255, 0, 0), 2)

        patch = hsv[py:py + 10, px:px + 10]
        if patch.size == 0:
            continue

        avg_h = int(np.mean(patch[:, :, 0]))
        avg_s = int(np.mean(patch[:, :, 1]))
        avg_v = int(np.mean(patch[:, :, 2]))
        color = match_color(avg_h, avg_s, avg_v) or "?"

        label = f"{qr_text} | {color}"
        cv2.putText(frame, label, (obj.rect.left, obj.rect.top - 10),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)

        if qr_text != last_qr:
            print(f"{qr_text:<16} {avg_h:>4} {avg_s:>4} {avg_v:>4}  {color}")
            last_qr = qr_text

    if not qr_results:
        last_qr = None

    cv2.imshow("PMUL2 Camera Test", frame)
    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

cv2.destroyAllWindows()
cam.stop()
print("[CAM] Arret.")
