# Agent: OpenCode (Claude) - AI/TestSuite
# Test: Camera preview via page web avec QR + detection couleur
#        Demarre un serveur HTTP (port 8081) avec flux MJPEG live
#        Les overlays de detection sont dessines directement sur l'image
#
# Usage: python test_camera.py [--host localhost:3000]
#        Puis ouvre http://<ip-pi>:8081 dans un navigateur

import time, signal, sys, requests, threading, io
import numpy as np
import cv2
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview
from http.server import HTTPServer, BaseHTTPRequestHandler

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
BACKEND = f"http://{HOST}/api"
PORT = 8081

running = True
latest_frame = None  # bytes JPEG du dernier frame avec overlays
frame_lock = threading.Lock()

def cleanup(sig, frame):
    global running
    running = False

signal.signal(signal.SIGINT, cleanup)

# charge les couleurs actives depuis la DB
print(f"\n=== CAMERA WEB ===\n  Backend: {BACKEND}\n  Page web: http://<ip>:8081\n  Ctrl+C pour quitter\n")
print("  Chargement des couleurs depuis la DB...")

db_colors = []
try:
    r = requests.get(f"{BACKEND}/colors", timeout=5)
    if r.status_code == 200:
        for c in r.json():
            if (c.get("status") and
                None not in (c.get("hueMin"), c.get("hueMax"),
                             c.get("saturationMin"), c.get("saturationMax"),
                             c.get("valueMin"), c.get("valueMax"))):
                db_colors.append({
                    "name": c["name"],
                    "hueMin": c["hueMin"], "hueMax": c["hueMax"],
                    "satMin": c["saturationMin"], "satMax": c["saturationMax"],
                    "valMin": c["valueMin"], "valMax": c["valueMax"],
                })
    print(f"  {len(db_colors)} couleurs actives: {[c['name'] for c in db_colors]}")
except Exception as e:
    print(f"  [!] Erreur chargement couleurs: {e}")
    sys.exit(1)

def matchColor(hue, sat, val):
    for c in db_colors:
        if (c["hueMin"] <= hue <= c["hueMax"] and
            c["satMin"] <= sat <= c["satMax"] and
            c["valMin"] <= val <= c["valMax"]):
            return c["name"]
    return None

# thread camera: capture + detection + overlays
def camera_loop():
    global latest_frame, running

    cam = Picamera2()
    cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
    cam.start_preview(Preview.NULL)
    cam.start()
    time.sleep(2)

    while running:
        frame = cam.capture_array()

        # conversion BGR pour OpenCV
        if frame.shape[2] == 4:
            frame = frame[:, :, :3]

        hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
        qr_results = decode(frame)

        for obj in qr_results:
            # rectangle autour du QR
            pts = obj.polygon
            if len(pts) >= 4:
                pts_np = np.array([(p.x, p.y) for p in pts], np.int32)
                cv2.polylines(frame, [pts_np], True, (0, 255, 0), 2)

            # texte du QR
            qr_text = obj.data.decode("utf-8")
            cx, cy = obj.rect.left, obj.rect.top - 10
            cv2.putText(frame, qr_text, (cx, cy), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

            # detection couleur: patch a droite du QR
            h, w = frame.shape[:2]
            px = min(obj.rect.left + obj.rect.width + 3, w - 15)
            py = min(obj.rect.top + (obj.rect.height // 2), h - 15)

            patch = hsv[py:py+10, px:px+10]
            if patch.size > 0:
                avgHue = int(np.mean(patch[:, :, 0]))
                avgSat = int(np.mean(patch[:, :, 1]))
                avgVal = int(np.mean(patch[:, :, 2]))

                color = matchColor(avgHue, avgSat, avgVal)
                label = f"{color} (H:{avgHue})" if color else f"? H={avgHue}"

                cv2.rectangle(frame, (px, py), (px+10, py+10), (255, 0, 0), 2)
                cv2.putText(frame, label, (px+15, py+10), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 0), 1)

        # encode en JPEG
        _, jpeg = cv2.imencode('.jpg', frame, [cv2.IMWRITE_JPEG_QUALITY, 70])
        with frame_lock:
            latest_frame = jpeg.tobytes()

        time.sleep(0.05)

    cam.stop()
    cam.stop_preview()

# handler HTTP
class CamHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/stream':
            self.send_response(200)
            self.send_header('Content-Type', 'multipart/x-mixed-replace; boundary=frame')
            self.end_headers()
            while running:
                with frame_lock:
                    if latest_frame:
                        self.wfile.write(b'--frame\r\n')
                        self.wfile.write(b'Content-Type: image/jpeg\r\n\r\n')
                        self.wfile.write(latest_frame)
                        self.wfile.write(b'\r\n')
                time.sleep(0.08)
        elif self.path == '/':
            html = """<!DOCTYPE html>
<html><head><title>PMUL2 Camera</title>
<style>body{background:#111;text-align:center;margin:0}
h1{color:#fff;font-family:sans-serif;margin:10px 0}
img{max-width:100%;height:auto;border:2px solid #333}
</style></head><body>
<h1>PMUL2 - Camera Live</h1>
<img src="/stream" />
</body></html>""".encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html')
            self.end_headers()
            self.wfile.write(html)
        else:
            self.send_response(404)
            self.end_headers()

# demarre le thread camera
cam_thread = threading.Thread(target=camera_loop, daemon=True)
cam_thread.start()

# demarre le serveur HTTP
server = HTTPServer(('0.0.0.0', PORT), CamHandler)
print(f"  Serveur demarre sur le port {PORT}")

try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
finally:
    running = False
    server.shutdown()
    print("Fin.")
