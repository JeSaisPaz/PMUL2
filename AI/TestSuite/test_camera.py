import time
import sys
import cv2
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2

# ---------------------------------------------------------------------------
# 1. CAMERA STREAM INITIALISATION
# ---------------------------------------------------------------------------
print("[CAM] Initialising Native Stream...")
cam = Picamera2()

# Request direct standard BGR format through safe config parameters
config = cam.create_preview_configuration({"main": {"size": (640, 480), "format": "BGR888"}})
cam.configure(config)
cam.start()

# Let the sensor gain settle down 
time.sleep(1.5)
frame = cam.capture_array()
cam.stop()

if frame is None or frame.size == 0:
    print("[ERROR] Failed to fetch image frame array.")
    sys.exit(1)

# Ensure data structure is contiguous for downstream matrix math
frame = np.ascontiguousarray(frame[:, :, :3])
h, w = frame.shape[:2]

# ---------------------------------------------------------------------------
# 2. SEAMLESS COLOR IDENTIFICATION
# ---------------------------------------------------------------------------
def detect_hue_name(h_val, s_val, v_val):
    """
    Direct, fast evaluation of the Hue channel matching your specific objects.
    Low saturation defaults to Unknown to prevent gray/white shadows from reading.
    """
    if s_val < 50 or v_val < 35:
        return "Unknown"
        
    # Check Hue boundaries seamlessly
    if 8 <= h_val <= 19:
        return "Orange" if v_val >= 140 else "Brown"
    elif 20 <= h_val <= 38:
        return "Yellow"
    elif 40 <= h_val <= 88:
        return "Green"
    elif 95 <= h_val <= 132:
        return "Blue"
    elif 133 <= h_val <= 175:
        return "Magenta"
        
    return "Unknown"

# ---------------------------------------------------------------------------
# 3. QR LOGIC & CALIBRATED BLOCK SAMPLING
# ---------------------------------------------------------------------------
qr_results = decode(frame)
if not qr_results:
    print("[SCAN] No QR Code found in frame.")
    cv2.imwrite("failed_frame.jpg", frame)
    sys.exit(0)

# Convert image to HSV space for uniform color filtering
hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
annotated = frame.copy()

for obj in qr_results:
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception:
        continue

    # Identify exact boundaries of the QR code box
    rx, ry, rw, rh = obj.rect.left, obj.rect.top, obj.rect.width, obj.rect.height
    cx = rx + (rw // 2)
    cy = ry + (rh // 2)

    # DISPOSITION RULE: Look on the left and right colored wings of the block face.
    # Set the offset distance dynamically to 1.25x the width of the QR code itself.
    offset_distance = int(rw * 1.25)
    sample_spots = [
        (cx - offset_distance, cy), # Left side block space
        (cx + offset_distance, cy)  # Right side block space
    ]

    votes = []
    patch_size = 12

    for sx, sy in sample_spots:
        # Prevent picking noise past the boundaries of the image
        if (0 <= sx <= w - patch_size) and (0 <= sy <= h - patch_size):
            patch = hsv[sy : sy + patch_size, sx : sx + patch_size]
            
            # Simple average value across the patch area
            avg_h = int(np.mean(patch[:, :, 0]))
            avg_s = int(np.mean(patch[:, :, 1]))
            avg_v = int(np.mean(patch[:, :, 2]))
            
            detected_name = detect_hue_name(avg_h, avg_s, avg_v)
            
            # Draw sample target box on visual report card
            color_indicator = (0, 255, 0) if detected_name != "Unknown" else (0, 0, 255)
            cv2.rectangle(annotated, (sx, sy), (sx + patch_size, sy + patch_size), color_indicator, 2)
            cv2.putText(annotated, f"H:{avg_h} {detected_name}", (sx - 15, sy - 6), 
                        cv2.FONT_HERSHEY_SIMPLEX, 0.35, color_indicator, 1)
            
            if detected_name != "Unknown":
                votes.append(detected_name)

    # Determine final matching color
    final_color = max(set(votes), key=votes.count) if votes else "Unknown"

    # Print log diagnostics
    print(f"QR Target: {qr_text}")
    print(f"Position:  Center X:{cx} Y:{cy}")
    print(f"Block Color Identified: {final_color}\n")

    # Render graphics markers on output image
    pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32).reshape((-1, 1, 2))
    cv2.polylines(annotated, [pts], True, (255, 255, 0), 2)
    cv2.drawMarker(annotated, (cx, cy), (0, 255, 255), cv2.MARKER_CROSS, 12, 2)
    
    label = f"{qr_text} : {final_color}"
    cv2.putText(annotated, label, (rx, max(ry - 10, 15)), 
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)

# Save result out to disk
output_name = "scan_result.jpg"
cv2.imwrite(output_name, annotated)
print(f"[SUCCESS] Scan summary exported to {output_name}")