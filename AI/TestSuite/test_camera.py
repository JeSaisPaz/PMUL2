import time
import sys
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

# ---------------------------------------------------------------------------
# 1. CAMERA CONFIGURATION & CAPTURE
# ---------------------------------------------------------------------------
print("[CAM] Initialising Sensor...")
cam = Picamera2()

config = cam.create_preview_configuration()
config["main"]["size"] = (640, 480)
config["main"]["format"] = "RGB888"  # Native RGB output

cam.configure(config)
cam.start()

# Let the sensor turn on
time.sleep(0.5)

# ---------------------------------------------------------------------------
# HARDWARE SENSOR FIX for "Magenta showing as Red":
# We aggressively boost the Blue channel (2.5x) and suppress Red (0.9x) 
# to force the camera to "see" the blue half of the Magenta block.
# ---------------------------------------------------------------------------
cam.set_controls({
    "AwbEnable": False,
    "ColourGains": (0.9, 2.5)  # (Red Gain, Blue Gain). Tweak these if it's too intense.
})

# Let the auto-exposure settle with our locked color settings
time.sleep(1.0)
raw_frame = cam.capture_array()
cam.stop()

if raw_frame is None or raw_frame.size == 0:
    print("[ERROR] Camera stream frame is empty.")
    sys.exit(1)

# Ensure contiguous memory array (native RGB)
frame = np.ascontiguousarray(raw_frame[:, :, :3])
h, w = frame.shape[:2]

# Standard OpenCV conversions based on true RGB input
hsv_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2HSV)
bgr_display = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR) 

# ---------------------------------------------------------------------------
# 2. ROBUST HUE-RANGE COLOR IDENTIFICATION
# ---------------------------------------------------------------------------
def identify_closest_color(h_val, s_val, v_val):
    """
    Identifies color using explicit Hue threshold bands.
    This eliminates lighting intensity (Value) dependencies.
    """
    # Reject colors that are too dark/washed out
    if v_val < 40 or s_val < 40:
        return "Unknown"

    # Orange vs Brown
    if 5 <= h_val < 18:
        if v_val < 110 or s_val < 120:
            return "Brown"
        return "Orange"

    # Core Color Bands (OpenCV Hue is 0-179)
    if (0 <= h_val < 5) or (170 <= h_val <= 179):
        return "Red"  
    elif 18 <= h_val < 38:
        return "Yellow"
    elif 38 <= h_val < 85:
        return "Green"
    elif 85 <= h_val < 135:
        return "Blue"
    elif 135 <= h_val < 170:
        return "Magenta"

    return "Unknown"

# ---------------------------------------------------------------------------
# 3. QR DETECTION & DISPOSITION SAMPLING
# ---------------------------------------------------------------------------
qr_codes = decode(frame)
if not qr_codes:
    print("[SCAN] No QR Code found in active frame.")
    cv2.imwrite("failed_scan.jpg", bgr_display)
    sys.exit(0)

annotated = bgr_display.copy()
patch_size = 16  # Good sample size for taking the median

for obj in qr_codes:
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        continue

    # Extract coordinates of the bounding box
    rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
    cx = rx + (rw // 2)
    cy = ry + (rh // 2)

    # Tighten your sample zone: place patches exactly 15 pixels outside the QR borders
    test_points = [
        (rx - 15 - patch_size, cy - (patch_size // 2)),  # Left side
        (rx + rw + 15, cy - (patch_size // 2))           # Right side
    ]

    votes = []

    for sx, sy in test_points:
        # Keep sample coordinates securely within screen boundaries
        if (0 <= sx <= w - patch_size) and (0 <= sy <= h - patch_size):
            patch = hsv_frame[sy : sy + patch_size, sx : sx + patch_size]
            
            # Use MEDIAN to completely ignore noise/speckles
            median_h = int(np.median(patch[:, :, 0]))
            median_s = int(np.median(patch[:, :, 1]))
            median_v = int(np.median(patch[:, :, 2]))
            
            match_name = identify_closest_color(median_h, median_s, median_v)
            
            # Draw visual tracking boxes onto reporting image
            box_color = (0, 255, 0) if match_name != "Unknown" else (0, 0, 255)
            cv2.rectangle(annotated, (sx, sy), (sx + patch_size, sy + patch_size), box_color, 2)
            cv2.putText(annotated, match_name, (sx - 10, sy - 6), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, box_color, 1)
            
            if match_name != "Unknown":
                votes.append(match_name)

    # Determine final matching color
    final_color = max(set(votes), key=votes.count) if votes else "Unknown"

    print(f"QR Content: {qr_text}")
    print(f"Block Color Detected: {final_color}\n")

    # Draw visual indicators
    pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32).reshape((-1, 1, 2))
    cv2.polylines(annotated, [pts], True, (255, 255, 0), 2)
    cv2.drawMarker(annotated, (cx, cy), (0, 255, 255), cv2.MARKER_CROSS, 14, 2)
    
    cv2.putText(annotated, f"{qr_text} : {final_color}", (rx, max(ry - 12, 15)), 
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)

# Save result out to disk
output_filename = "scan_result.jpg"
cv2.imwrite(output_filename, annotated)
print(f"[SUCCESS] Processed summary frame saved to: {output_filename}")