import time
import sys
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

# ---------------------------------------------------------------------------
# 1. CLEAN CAMERA CONFIGURATION (Standard Defaults)
# ---------------------------------------------------------------------------
print("[CAM] Initialising Sensor...")
cam = Picamera2()

config = cam.create_preview_configuration()
config["main"]["size"] = (640, 480)
config["main"]["format"] = "RGB888"  # Clean, native RGB array layout

cam.configure(config)
cam.start()

# IMPORTANT: Give the camera a full 2.0 seconds to let native Auto White Balance
# and Auto Exposure completely stabilize. This keeps the QR code white.
time.sleep(2.0)
raw_frame = cam.capture_array()
cam.stop()

if raw_frame is None or raw_frame.size == 0:
    print("[ERROR] Camera stream frame is empty.")
    sys.exit(1)

# Ensure data structure is a standard, memory-contiguous RGB frame
frame = np.ascontiguousarray(raw_frame[:, :, :3])
h, w = frame.shape[:2]

# Convert standard RGB to HSV and BGR configurations
hsv_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2HSV)
bgr_display = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR) 

# ---------------------------------------------------------------------------
# 2. CIRCULAR HUE DISTANCE MATCHING
# ---------------------------------------------------------------------------
# We define clear Hue anchor centers (OpenCV Hue goes from 0 to 179)
COLOR_ANCHORS = {
    "Magenta": 150,
    "Blue":    115,
    "Green":   60,
    "Yellow":  28,
    "Orange":  13
}

def identify_closest_color(h_val, s_val, v_val):
    """
    Finds the color by tracking which anchor it is mathematically closest to
    on a circular 180-degree color wheel, ignoring lighting distortions.
    """
    # Safeguard: If the patch is too dark or lacks color vibrancy, drop it
    if v_val < 35 or s_val < 35:
        return "Unknown"

    # Special Case: Brown shares the same Hue space as Orange/Yellow, 
    # but has distinct low-saturation and low-brightness properties.
    if h_val < 22 and (s_val < 115 or v_val < 100):
        return "Brown"
        
    closest_name = "Unknown"
    min_distance = float('inf')
    
    for name, target_hue in COLOR_ANCHORS.items():
        # Calculate Hue difference taking into account the cylindrical 180-degree wrap
        dh = abs(h_val - target_hue)
        if dh > 90:
            dh = 180 - dh
            
        if dh < min_distance:
            min_distance = dh
            closest_name = name
            
    return closest_name

# ---------------------------------------------------------------------------
# 3. QR DETECTION & DISPOSITION SAMPLING
# ---------------------------------------------------------------------------
qr_codes = decode(frame)
if not qr_codes:
    print("[SCAN] No QR Code found in active frame.")
    cv2.imwrite("failed_scan.jpg", bgr_display)
    sys.exit(0)

annotated = bgr_display.copy()
patch_size = 16 

for obj in qr_codes:
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        continue

    # Extract coordinates of the bounding box
    rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
    cx = rx + (rw // 2)
    cy = ry + (rh // 2)

    # Sampling patches placed exactly 15 pixels outside the left & right QR borders
    test_points = [
        (rx - 15 - patch_size, cy - (patch_size // 2)),  
        (rx + rw + 15, cy - (patch_size // 2))           
    ]

    votes = []

    for sx, sy in test_points:
        if (0 <= sx <= w - patch_size) and (0 <= sy <= h - patch_size):
            patch = hsv_frame[sy : sy + patch_size, sx : sx + patch_size]
            
            # Use MEDIAN values to skip noise and single broken pixels
            median_h = int(np.median(patch[:, :, 0]))
            median_s = int(np.median(patch[:, :, 1]))
            median_v = int(np.median(patch[:, :, 2]))
            
            match_name = identify_closest_color(median_h, median_s, median_v)
            
            # DEBUG LOGGING: See exactly what the camera sensor sees
            print(f"[DEBUG] Patch at ({sx},{sy}) -> Raw HSV: ({median_h}, {median_s}, {median_v}) -> Classified as: {match_name}")
            
            box_color = (0, 255, 0) if match_name != "Unknown" else (0, 0, 255)
            cv2.rectangle(annotated, (sx, sy), (sx + patch_size, sy + patch_size), box_color, 2)
            cv2.putText(annotated, match_name, (sx - 10, sy - 6), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, box_color, 1)
            
            if match_name != "Unknown":
                votes.append(match_name)

    # Determine final matching color based on patch votes
    final_color = max(set(votes), key=votes.count) if votes else "Unknown"

    print(f"\nResult -> QR Content: {qr_text}")
    print(f"Result -> Block Color Detected: {final_color}\n")

    # Visual overlays for reporting
    pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32).reshape((-1, 1, 2))
    cv2.polylines(annotated, [pts], True, (255, 255, 0), 2)
    cv2.drawMarker(annotated, (cx, cy), (0, 255, 255), cv2.MARKER_CROSS, 14, 2)
    cv2.putText(annotated, f"{qr_text} : {final_color}", (rx, max(ry - 12, 15)), 
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)

# Save image out to disk
output_filename = "scan_result.jpg"
cv2.imwrite(output_filename, annotated)
print(f"[SUCCESS] Processed summary frame saved to: {output_filename}")