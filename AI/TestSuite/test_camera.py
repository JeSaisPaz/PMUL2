import time
import sys
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

# ---------------------------------------------------------------------------
# Camera calibration
# How many pixels correspond to 1 cm at your typical scanning distance.
# Quick way to measure: place a ruler in frame, count pixels per cm in the
# captured image.  Typical Pi-cam values:
#   ~20 cm distance → ~30 px/cm
#   ~30 cm distance → ~20 px/cm
# Adjust until the purple zones land ~5 cm left/right of the QR centre.
# ---------------------------------------------------------------------------
PIXELS_PER_CM = 25          # ← tune this for your setup
SAMPLE_OFFSET_CM = 5        # horizontal distance from QR centre (cm)
SAMPLE_OFFSET_PX = int(SAMPLE_OFFSET_CM * PIXELS_PER_CM)
PATCH = 10                  # side length of each sample patch (px)

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
    patch = hsv[y:y + PATCH, x:x + PATCH]
    if patch.size == 0:
        return 0, 0, 0
    return (
        int(np.mean(patch[:, :, 0])),
        int(np.mean(patch[:, :, 1])),
        int(np.mean(patch[:, :, 2])),
    )

def detect_block_color(hsv, obj, frame_h, frame_w):
    rect = obj.rect

    # Exact centre of the QR code
    cx = rect.left + rect.width  // 2
    cy = rect.top  + rect.height // 2

    # Two sample points: 5 cm left and 5 cm right of centre, same row
    positions = [
        (cx - SAMPLE_OFFSET_PX, cy),
        (cx + SAMPLE_OFFSET_PX, cy),
    ]

    samples = []
    votes   = {}
    avg_h = avg_s = avg_v = 0
    color = "?"

    for px, py in positions:
        in_bounds = (0 <= px <= frame_w - PATCH and 0 <= py <= frame_h - PATCH)

        if not in_bounds:
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
        # Fallback: average of whatever was sampled
        valid = [(h_val, s_val, v_val)
                 for _, _, h_val, s_val, v_val, name, _ in samples
                 if name is not None]
        if valid:
            avg_h = int(np.mean([v[0] for v in valid]))
            avg_s = int(np.mean([v[1] for v in valid]))
            avg_v = int(np.mean([v[2] for v in valid]))
            name, _ = classify_color(avg_h, avg_s, avg_v)
            color = f"Border({name})" if name else "Border"

    return color, avg_h, avg_s, avg_v, samples, cx, cy

# ---------------------------------------------------------------------------
print("[CAM] Initialisation...")
cam = Picamera2()
cam.configure(cam.create_preview_configuration(
    main={"size": (640, 480), "format": "BGR888"}))
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
hsv   = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
qr_results = decode(frame)

if not qr_results:
    print("[SCAN] Aucun QR detecte")
    sys.exit(0)

PURPLE = (180, 0, 180)  # BGR

for obj in qr_results:
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        continue

    color, avg_h, avg_s, avg_v, samples, cx, cy = detect_block_color(
        hsv, obj, h, w)

    print(f"QR:      {qr_text}")
    print(f"Centre:  ({cx}, {cy})")
    print(f"Offset:  {SAMPLE_OFFSET_PX} px  ({SAMPLE_OFFSET_CM} cm @ {PIXELS_PER_CM} px/cm)")
    print(f"H S V:   {avg_h} {avg_s} {avg_v}")
    print(f"Couleur: {color}")
    print()

    annotated = frame.copy()

    # QR polygon outline
    pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32).reshape((-1, 1, 2))
    cv2.polylines(annotated, [pts], True, (0, 255, 0), 2)

    # QR bounding box
    cv2.rectangle(annotated,
                  (obj.rect.left, obj.rect.top),
                  (obj.rect.left + obj.rect.width, obj.rect.top + obj.rect.height),
                  (255, 255, 0), 1)

    # Cross-hair at QR centre
    cv2.drawMarker(annotated, (cx, cy), (0, 255, 255),
                   cv2.MARKER_CROSS, 16, 2)

    # Sample patch overlays
    for sx, sy, sh, ss, sv, sname, sborder in samples:
        if sx < 0 or sy < 0 or sx + PATCH >= w or sy + PATCH >= h:
            continue

        # Semi-transparent purple fill for the scanned zone
        overlay = annotated.copy()
        cv2.rectangle(overlay, (sx, sy), (sx + PATCH, sy + PATCH), PURPLE, -1)
        cv2.addWeighted(overlay, 0.45, annotated, 0.55, 0, annotated)

        # Coloured border: green = valid, red = border hit, cyan = unclassified
        if sname and not sborder:
            rc = (0, 255, 0)
        elif sborder:
            rc = (0, 0, 255)
        else:
            rc = (255, 255, 0)
        cv2.rectangle(annotated, (sx, sy), (sx + PATCH, sy + PATCH), rc, 2)
        cv2.putText(annotated, f"H{sh}S{ss}V{sv}",
                    (sx, sy - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.35, rc, 1)

    # Main label
    label = f"{qr_text} | {color} H:{avg_h} S:{avg_s} V:{avg_v}"
    cv2.putText(annotated, label,
                (obj.rect.left, max(obj.rect.top - 10, 15)),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)

    fname = "scan_result.jpg"
    annotated = cv2.cvtColor(annotated, cv2.COLOR_BGR2RGB)
    cv2.imwrite(fname, annotated)
    print(f"Image: {fname}")

print("[CAM] Termine.")