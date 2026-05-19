import time
import sys
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

# ---------------------------------------------------------------------------
# 1. SETUP
# ---------------------------------------------------------------------------
cam = Picamera2()
config = cam.create_preview_configuration()
config["main"]["size"] = (640, 480)
config["main"]["format"] = "RGB888"
cam.configure(config)
cam.start()
time.sleep(2.0)
raw_frame = cam.capture_array()
cam.stop()

# Work with RGB as standard
frame = np.ascontiguousarray(raw_frame[:, :, :3])
hsv_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2HSV)
bgr_display = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)

# ---------------------------------------------------------------------------
# 2. IDENTIFICATION & PREVIEW
# ---------------------------------------------------------------------------
def identify_color(h, s, v):
    # This logic now accounts for the filter shift
    if v < 40: return "Unknown"
    
    # Blue under yellow filter: low S, H between 85-130
    if 85 <= h <= 130 and s < 100: return "Blue"
    # Yellow under yellow filter: high S
    if 15 <= h < 40: return "Yellow"
    # Magenta/Red
    if (h < 15 or h > 165): return "Red"
    if 40 <= h < 85: return "Green"
    return "Unknown"

# ---------------------------------------------------------------------------
# 3. QR DETECTION & SAMPLING
# ---------------------------------------------------------------------------
qr_codes = decode(frame)
annotated = bgr_display.copy()

for obj in qr_codes:
    rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
    cx, cy = rx + rw // 2, ry + rh // 2
    
    # Define a small sampling patch inside the QR code
    patch = hsv_frame[cy-10:cy+10, cx-10:cx+10]
    
    # Get range of HSV to debug
    h_min, h_max = np.min(patch[:,:,0]), np.max(patch[:,:,0])
    s_min, s_max = np.min(patch[:,:,1]), np.max(patch[:,:,1])
    v_min, v_max = np.min(patch[:,:,2]), np.max(patch[:,:,2])
    
    mh, ms, mv = np.median(patch[:,:,0]), np.median(patch[:,:,1]), np.median(patch[:,:,2])
    color = identify_color(mh, ms, mv)
    
    # Print the ranges so you can calibrate perfectly
    print(f"--- HSV Ranges for {color} ---")
    print(f"Hue: {h_min}-{h_max} (Median: {mh})")
    print(f"Sat: {s_min}-{s_max} (Median: {ms})")
    print(f"Val: {v_min}-{v_max} (Median: {mv})")
    
    # Visual Preview
    cv2.rectangle(annotated, (cx-10, cy-10), (cx+10, cy+10), (255, 0, 0), 2)
    cv2.putText(annotated, f"{color}", (rx, ry-10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (255, 255, 0), 2)

cv2.imwrite("debug_preview.jpg", annotated)
print("Saved debug_preview.jpg. Check the terminal for the exact HSV ranges.")