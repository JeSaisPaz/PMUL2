# ---------------------------------------------------------------------------
# Main Execution Loop
# ---------------------------------------------------------------------------
print("[CAM] Initialisation...")
cam = Picamera2()

# Keep the configuration that stopped the digital barcode noise
cam.configure(cam.create_preview_configuration(
    main={"size": (640, 480), "format": "RGB888"}
))
cam.start()
time.sleep(2)

# Warmup frames
for _ in range(10):
    cam.capture_array()
    time.sleep(0.1)

raw = cam.capture_array()
cam.stop()

if raw is None:
    print("[CAM] Pas de frame, abandon.")
    sys.exit(1)

# FIX: Treat the incoming raw array directly as the base frame.
# This corrects the Red/Blue flip so Magenta stays Magenta, and Blue stays Blue.
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

    # Draw QR polygon outline
    pts = np.array([(p.x, p.y) for p in obj.polygon], np.int32).reshape((-1, 1, 2))
    cv2.polylines(annotated, [pts], True, (0, 255, 0), 2)

    # Draw QR bounding box
    cv2.rectangle(annotated,
                  (obj.rect.left, obj.rect.top),
                  (obj.rect.left + obj.rect.width, obj.rect.top + obj.rect.height),
                  (255, 255, 0), 1)

    # Cross-hair at QR centre
    cv2.drawMarker(annotated, (cx, cy), (0, 255, 255), cv2.MARKER_CROSS, 16, 2)

    # Draw horizontal guide line to visually track the side-to-side scan axis
    cv2.line(annotated, (0, cy), (w, cy), (0, 140, 255), 1, cv2.LINE_AA)

    # Sample patch overlays
    for sx, sy, sh, ss, sv, sname, in_bounds in samples:
        if not in_bounds or sx + PATCH >= w or sy + PATCH >= h:
            continue

        overlay = annotated.copy()
        cv2.rectangle(overlay, (sx, sy), (sx + PATCH, sy + PATCH), PURPLE, -1)
        cv2.addWeighted(overlay, 0.45, annotated, 0.55, 0, annotated)

        rc = (0, 255, 0) if sname in TARGET_COLORS else (255, 255, 0)
        cv2.rectangle(annotated, (sx, sy), (sx + PATCH, sy + PATCH), rc, 2)
        cv2.putText(annotated, f"H{sh}S{ss}V{sv}",
                    (sx, sy - 5),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.35, rc, 1)

    if attempts > 0:
        badge = f"retries: {attempts}"
        cv2.putText(annotated, badge,
                    (obj.rect.left, max(obj.rect.top - 28, 30)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.45, (0, 128, 255), 1)

    label = f"{qr_text} | {color} H:{avg_h} S:{avg_s} V:{avg_v}"
    cv2.putText(annotated, label,
                (obj.rect.left, max(obj.rect.top - 10, 15)),
                cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 255), 2)

    fname = "scan_result.jpg"
    cv2.imwrite(fname, annotated)
    print(f"Image saved: {fname}")

print("[CAM] Termine.")