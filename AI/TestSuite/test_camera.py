import time
import sys
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

# ---------------------------------------------------------------------------
# Camera calibration
# Measure: capture a frame with a ruler, count pixels per cm.
# Typical Pi-cam values:  ~20 cm away → ~30 px/cm
#                         ~30 cm away → ~20 px/cm
# ---------------------------------------------------------------------------
PIXELS_PER_CM   = 25        # ← starting value, tune for your setup
SAMPLE_OFFSET_CM = 5        # nominal distance from QR centre (cm)
RETRY_STEP_PX   = 8         # pixels added to offset on each retry
MAX_RETRIES     = 10        # give up after this many unsatisfying samples
PATCH           = 10        # side length of each sample patch (px)

# ---------------------------------------------------------------------------
# Colour palette — only these six are accepted as valid results.
# Brown is a dark orange: same hue band, lower Value.
# Order matters: Orange is checked before Brown so the higher-V band wins
# when both could match.
# ---------------------------------------------------------------------------
#            name       hue range(s)    saturation      value          is_neutral
COLOR_RANGES = [
    ("Yellow",  [(20, 35)],   (80, 255),  (80,  255), False),
    ("Orange",  [(8,  20)],   (150, 255), (150, 255), False),
    ("Brown",   [(8,  20)],   (60,  220), (20,  149), False),
    ("Green",   [(40, 85)],   (40,  255), (40,  255), False),
    ("Blue",    [(100, 130)], (80,  255), (40,  255), False),
    ("Magenta", [(131, 170)], (80,  255), (40,  255), False),
]

TARGET_COLORS = {r[0] for r in COLOR_RANGES}   # {"Yellow","Orange","Brown","Green","Blue","Magenta"}

def classify_color(h, s, v):
    for name, hue_ranges, (s_lo, s_hi), (v_lo, v_hi), _ in COLOR_RANGES:
        if not (s_lo <= s <= s_hi and v_lo <= v <= v_hi):
            continue
        for (h_lo, h_hi) in hue_ranges:
            if h_lo <= h <= h_hi:
                return name
    return None

def sample_patch(hsv, x, y):
    patch = hsv[y:y + PATCH, x:x + PATCH]
    if patch.size == 0:
        return 0, 0, 0
    return (
        int(np.mean(patch[:, :, 0])),
        int(np.mean(patch[:, :, 1])),
        int(np.mean(patch[:, :, 2])),
    )

def sample_at_offset(hsv, cx, cy, offset_px, frame_h, frame_w):
    """Sample the two horizontal points at ±offset_px from (cx, cy).
    Returns (samples, votes) where samples is a list of
    (px, py, h, s, v, name, in_bounds)."""
    positions = [
        (cx - offset_px, cy),
        (cx + offset_px, cy),
    ]
    samples = []
    votes   = {}
    for px, py in positions:
        in_bounds = (0 <= px <= frame_w - PATCH and 0 <= py <= frame_h - PATCH)
        if not in_bounds:
            samples.append((px, py, 0, 0, 0, None, False))
            continue
        h_val, s_val, v_val = sample_patch(hsv, px, py)
        name = classify_color(h_val, s_val, v_val)
        samples.append((px, py, h_val, s_val, v_val, name, True))
        if name in TARGET_COLORS:
            votes[name] = votes.get(name, 0) + 1
    return samples, votes

def detect_block_color(hsv, obj, frame_h, frame_w):
    rect = obj.rect
    cx = rect.left + rect.width  // 2
    cy = rect.top  + rect.height // 2

    color       = None
    avg_h = avg_s = avg_v = 0
    samples     = []
    final_offset = int(SAMPLE_OFFSET_CM * PIXELS_PER_CM)
    attempt     = 0

    for attempt in range(MAX_RETRIES):
        offset_px = int(SAMPLE_OFFSET_CM * PIXELS_PER_CM) + attempt * RETRY_STEP_PX
        samples, votes = sample_at_offset(hsv, cx, cy, offset_px, frame_h, frame_w)
        final_offset = offset_px

        if votes:
            color = max(votes, key=votes.get)
            # grab HSV from the winning sample
            for _, _, h_val, s_val, v_val, name, _ in samples:
                if name == color:
                    avg_h, avg_s, avg_v = h_val, s_val, v_val
                    break
            break   # satisfying colour found → stop retrying
        else:
            # log what we actually saw so the caller can report it
            valid = [(h, s, v) for _, _, h, s, v, n, ib in samples if ib]
            if valid:
                avg_h = int(np.mean([x[0] for x in valid]))
                avg_s = int(np.mean([x[1] for x in valid]))
                avg_v = int(np.mean([x[2] for x in valid]))

    if color is None:
        color = "Unknown"

    return color, avg_h, avg_s, avg_v, samples, cx, cy, final_offset, attempt

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
h, w  = frame.shape[:2]
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

    color, avg_h, avg_s, avg_v, samples, cx, cy, final_offset, attempts = \
        detect_block_color(hsv, obj, h, w)

    print(f"QR:       {qr_text}")
    print(f"Centre:   ({cx}, {cy})")
    print(f"Offset:   {final_offset} px  (base {int(SAMPLE_OFFSET_CM * PIXELS_PER_CM)} px, "
          f"{attempts} retr{'y' if attempts == 1 else 'ies'})")
    print(f"H S V:    {avg_h} {avg_s} {avg_v}")
    print(f"Couleur:  {color}")
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
    cv2.drawMarker(annotated, (cx, cy), (0, 255, 255), cv2.MARKER_CROSS, 16, 2)

    # Sample patch overlays
    for sx, sy, sh, ss, sv, sname, in_bounds in samples:
        if not in_bounds or sx + PATCH >= w or sy + PATCH >= h:
            continue

        # Semi-transparent purple fill — exact zone sampled
        overlay = annotated.copy()
        cv2.rectangle(overlay, (sx, sy), (sx + PATCH, sy + PATCH), PURPLE, -1)
        cv2.addWeighted(overlay, 0.45, annotated, 0.55, 0, annotated)

        # Border colour: green = target hit, cyan = unclassified
        rc = (0, 255, 0) if sname in TARGET_COLORS else (255, 255, 0)
        cv2.rectangle(annotated, (sx, sy), (sx + PATCH, sy + PATCH), rc, 2)
        cv2.putText(annotated, f"H{sh}S{ss}V{sv}",
                    (sx, sy - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.35, rc, 1)

    # Retry count badge (shown only when retries happened)
    if attempts > 0:
        badge = f"retries: {attempts}"
        cv2.putText(annotated, badge,
                    (obj.rect.left, max(obj.rect.top - 28, 30)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 128, 255), 1)

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