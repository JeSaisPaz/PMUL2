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

# Request a standard preview configuration setup
config = cam.create_preview_configuration()
# Explicitly apply configuration parameters through keyword assignments
config["main"]["size"] = (640, 480)
config["main"]["format"] = "RGB888"  # Native picamera2 array output format

cam.configure(config)
cam.start()

# Allow camera AGC and gains to settle
time.sleep(1.5)
frame = cam.capture_array()
cam.stop()

if frame is None or frame.size == 0:
    print("[ERROR] Camera stream frame is empty.")
    sys.exit(1)

# Ensure data structure is memory-contiguous
frame = np.ascontiguousarray(frame[:, :, :3])
h, w = frame.shape[:2]

# Convert the RGB frame correctly to HSV format
hsv_frame = cv2.cvtColor(frame, cv2.COLOR_RGB2HSV)

# Convert the frame to BGR strictly for saving standard color images with cv2.imwrite
bgr_display = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)

# ---------------------------------------------------------------------------
# 2. SIMPLIFIED COLOR DISTANCE MATCHING
# ---------------------------------------------------------------------------
# Calibrated HSV target reference centers for your specific objects
COLOR_TARGETS = {
    "Magenta": [150, 180, 160],
    "Yellow":  [ 28, 200, 200],
    "Orange":  [ 13, 220, 220],
    "Blue":    [115, 200, 160],
    "Green":   [ 60, 180, 140],
    "Brown":   [ 12, 130,  80]
}

def identify_closest_color(h_avg, s_avg, v_avg):
    """
    Finds the correct color name by selecting the closest mathematical distance
    to our target color palette anchors.
    """
    if s_avg < 45 or v_avg < 30:
        return "Unknown"
        
    closest_name = "Unknown"
    min_distance = float('inf')
    
    for name, target_hsv in COLOR_TARGETS.items():
        # Calculate Hue difference taking into account the cylindrical 180-degree wrap
        dh = abs(h_avg - target_hsv[0])
        if dh > 90:
            dh = 180 - dh
            
        ds = s_avg - target_hsv[1]
        dv = v_avg - target_hsv[2]
        
        # Standard Euclidean distance calculation (Hue given extra weight for stability)
        distance = np.sqrt((dh * 2.0) ** 2 + ds ** 2 + dv ** 2)
        
        if distance < min_distance:
            min_distance = distance
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
patch_size = 12

for obj in qr_codes:
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        continue

    # Extract coordinates of the bounding box
    rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
    cx = rx + (rw // 2)
    cy = ry + (rh // 2)

    # DISPOSITION LOOKUP: Place sample boxes safely on the left & right block faces
    # Calculated dynamically using 1.3x the width of the physical QR code
    horizontal_offset = int(rw * 1.3)
    test_points = [
        (cx - horizontal_offset, cy),
        (cx + horizontal_offset, cy)
    ]

    votes = []

    for sx, sy in test_points:
        # Keep sample coordinates securely within screen boundaries
        if (0 <= sx <= w - patch_size) and (0 <= sy <= h - patch_size):
            patch = hsv_frame[sy : sy + patch_size, sx : sx + patch_size]
            
            # Simple average value of pixels in the patch box
            mean_h = int(np.mean(patch[:, :, 0]))
            mean_s = int(np.mean(patch[:, :, 1]))
            mean_v = int(np.mean(patch[:, :, 2]))
            
            match_name = identify_closest_color(mean_h, mean_s, mean_v)
            
            # Draw visual tracking boxes onto reporting image
            box_color = (0, 255, 0) if match_name != "Unknown" else (0, 0, 255)
            cv2.rectangle(annotated, (sx, sy), (sx + patch_size, sy + patch_size), box_color, 2)
            cv2.putText(annotated, match_name, (sx - 10, sy - 6), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.38, box_color, 1)
            
            if match_name != "Unknown":
                votes.append(match_name)

    # Determine final matching color
    final_color = max(set(votes), key=votes.count) if votes else "Unknown"

    print(f"QR Content: {qr_text}")
    print(f"Block Color Detected: {final_color}\n")

    # Draw visual indicator shapes
    pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32).reshape((-1, 1, 2))
    cv2.polylines(annotated, [pts], True, (255, 255, 0), 2)
    cv2.drawMarker(annotated, (cx, cy), (0, 255, 255), cv2.MARKER_CROSS, 14, 2)
    
    cv2.putText(annotated, f"{qr_text} : {final_color}", (rx, max(ry - 12, 15)), 
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)

# Save result out to disk
output_filename = "scan_result.jpg"
cv2.imwrite(output_filename, annotated)
print(f"[SUCCESS] Processed summary frame saved to: {output_filename}")