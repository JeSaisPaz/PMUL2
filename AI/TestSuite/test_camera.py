import time
import sys
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

# Hard-coded HSV ranges for all detectable colours
COLOR_RANGES = [
    ("Red",      [(0, 10),   (170, 179)],  (80, 255),  (50, 255),  False),
    ("Orange",   [(11, 20)],               (80, 255),  (80, 255),  False),
    ("Yellow",   [(16, 38)],               (14, 255),  (80, 255),  False),
    ("Green",    [(40, 85)],               (40, 255),  (40, 255),  False),
    ("Cyan",     [(86, 105)],              (40, 255),  (50, 255),  False),
    ("Blue",     [(106, 130)],             (40, 255),  (40, 255),  False),
    ("Magenta",  [(131, 170)],             (40, 255),  (40, 255),  False),
    ("Pink",     [(131, 170)],             (40, 255),  (40, 255),  False),
    ("White",    [(0, 179)],   (0, 25),    (200, 255), True),
    ("Gray",     [(0, 179)],   (0, 25),    (80, 199),  True),
    ("DarkGray", [(0, 179)],   (0, 25),    (50, 79),   True),
    ("Black",    [(0, 179)],   (0, 255),   (0, 49),    True),
]

def classify_color(h, s, v):
    for name, hue_ranges, (s_lo, s_hi), (v_lo, v_hi), is_border in COLOR_RANGES:
        if not (s_lo <= s <= s_hi and v_lo <= v <= v_hi):
            continue
        for (h_lo, h_hi) in hue_ranges:
            if h_lo <= h <= h_hi:
                return name, is_border
    return None, False

def sample_patch(hsv, x, y):
    patch = hsv[y:y + 10, x:x + 10]
    if patch.size == 0:
        return 0, 0, 0
    return (
        int(np.mean(patch[:, :, 0])),
        int(np.mean(patch[:, :, 1])),
        int(np.mean(patch[:, :, 2])),
    )

def detect_block_color(hsv, obj, frame_h, frame_w):
    rect = obj.rect
    cy = rect.top + rect.height // 2
    offset = max(50, rect.width // 3)

    positions = [
        (rect.left - offset, cy),
        (rect.left + rect.width + offset, cy),
    ]

    samples = []
    votes = {}
    avg_h = avg_s = avg_v = 0
    color = "?"

    for px, py in positions:
        if px < 0 or py < 0 or px + 10 >= frame_w or py + 10 >= frame_h:
            samples.append((px, py, 0, 0, 0, None, False))
            continue
        h_val, s_val, v_val = sample_patch(hsv, px, py)
        name, is_border = classify_color(h_val, s_val, v_val)
        samples.append((px, py, h_val, s_val, v_val, name, is_border))
        if name and not is_border:
            votes[name] = votes.get(name, 0) + 1

    if votes:
        color = max(votes, key=votes.get)
        for _, _, h_val, s_val, v_val, name, _ in samples:
            if name == color:
                avg_h, avg_s, avg_v = h_val, s_val, v_val
                break
    else:
        for extra in [80, 120]:
            px = min(rect.left + rect.width + extra, frame_w - 15)
            py = min(cy, frame_h - 15)
            h_val, s_val, v_val = sample_patch(hsv, px, py)
            name, is_border = classify_color(h_val, s_val, v_val)
            samples.append((px, py, h_val, s_val, v_val, name, is_border))
            if name and not is_border:
                color = name
                avg_h, avg_s, avg_v = h_val, s_val, v_val
                break
            if not is_border:
                break
        else:
            name, _ = classify_color(avg_h, avg_s, avg_v)
            color = f"Border({name})" if name else "Border"

    return color, avg_h, avg_s, avg_v, samples

print("[CAM] Initialisation...")
cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start()
time.sleep(2)

for _ in range(10):
    cam.capture_array()
    time.sleep(0.1)

raw = cam.capture_array()
cam.stop()

if raw is None:
    print("[CAM] Pas de frame, abandon.")
    sys.exit(1)

frame = np.ascontiguousarray(raw[:, :, :3]).copy()
h, w = frame.shape[:2]
hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
qr_results = decode(frame)

if not qr_results:
    print("[SCAN] Aucun QR detecte")
    sys.exit(0)

for obj in qr_results:
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        continue

    color, avg_h, avg_s, avg_v, samples = detect_block_color(hsv, obj, h, w)

    print(f"QR:     {qr_text}")
    print(f"H S V:  {avg_h} {avg_s} {avg_v}")
    print(f"Couleur: {color}")
    print()

    annotated = frame.copy()

    pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32)
    pts = pts.reshape((-1, 1, 2))
    cv2.polylines(annotated, [pts], True, (0, 255, 0), 2)

    for sx, sy, sh, ss, sv, sname, sborder in samples:
        if sx < 0 or sy < 0 or sx + 10 >= w or sy + 10 >= h:
            continue
        if sname and not sborder:
            rc = (0, 255, 0)
        elif sborder:
            rc = (0, 0, 255)
        else:
            rc = (255, 255, 0)
        cv2.rectangle(annotated, (sx, sy), (sx + 10, sy + 10), rc, 2)
        cv2.putText(annotated, f"H{sh}S{ss}V{sv}",
                    (sx, sy - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.35, rc, 1)

    cv2.rectangle(annotated,
                  (obj.rect.left, obj.rect.top),
                  (obj.rect.left + obj.rect.width,
                   obj.rect.top + obj.rect.height),
                  (255, 255, 0), 1)

    label = f"{qr_text} | {color} H:{avg_h} S:{avg_s} V:{avg_v}"
    cv2.putText(annotated, label,
                (obj.rect.left, max(obj.rect.top - 10, 15)),
                cv2.FONT_HERSHEY_SIMPLEX,
                0.55, (0, 255, 255), 2)

    fname = "scan_result.jpg"
    cv2.imwrite(fname, annotated)
    print(f"Image: {fname}")

print("[CAM] Termine.")
