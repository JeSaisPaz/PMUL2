#!/usr/bin/env python3
"""
PREVIEW CAMÉRA - Flux MJPEG via navigateur
===========================================
Lance ce script sur le Pi, puis ouvre dans ton navigateur :
  http://<IP_DU_PI>:5000

Arrêt : Ctrl+C
"""

from picamera2 import Picamera2
from flask import Flask, Response
import io, threading, time

# ── Config ─────────────────────────────────────────────────
PORT        = 5000
WIDTH, HEIGHT = 640, 480
FRAMERATE   = 15  # fps (baisse si lag sur le réseau)

# ── État global ────────────────────────────────────────────
app          = Flask(__name__)
frame_lock   = threading.Lock()
latest_frame = None

# ── Capture en arrière-plan ────────────────────────────────
def capture_loop():
    global latest_frame

    cam = Picamera2()
    cfg = cam.create_preview_configuration(
        main={"size": (WIDTH, HEIGHT), "format": "RGB888"}
    )
    cam.configure(cfg)
    cam.set_controls({
        "AwbEnable":    False,
        "ExposureTime": 9000,
        "AnalogueGain": 1.0,
        "ColourGains":  (1.3, 1.7),
        "Saturation":   0.9
    })
    cam.start()
    time.sleep(1.0)  # laisser l'AE se stabiliser
    print(f"[CAM] Démarrée ({WIDTH}x{HEIGHT} @ {FRAMERATE}fps)")

    interval = 1.0 / FRAMERATE
    while True:
        buf = io.BytesIO()
        cam.capture_file(buf, format="jpeg")
        with frame_lock:
            latest_frame = buf.getvalue()
        time.sleep(interval)

# ── Route MJPEG ────────────────────────────────────────────
def generate():
    while True:
        with frame_lock:
            frame = latest_frame
        if frame is None:
            time.sleep(0.05)
            continue
        yield (
            b"--frame\r\n"
            b"Content-Type: image/jpeg\r\n\r\n"
            + frame +
            b"\r\n"
        )
        time.sleep(1.0 / FRAMERATE)

@app.route("/")
def index():
    return """
    <html>
    <head>
      <title>Pi Camera Preview</title>
      <style>
        body { margin:0; background:#111; display:flex;
               justify-content:center; align-items:center; height:100vh; }
        img  { max-width:100%; border: 2px solid #444; }
      </style>
    </head>
    <body>
      <img src="/video" />
    </body>
    </html>
    """

@app.route("/video")
def video():
    return Response(
        generate(),
        mimetype="multipart/x-mixed-replace; boundary=frame"
    )

# ── Main ───────────────────────────────────────────────────
if __name__ == "__main__":
    t = threading.Thread(target=capture_loop, daemon=True)
    t.start()

    # Attendre le premier frame avant d'ouvrir le serveur
    print("[SERVER] Attente du premier frame...")
    while latest_frame is None:
        time.sleep(0.1)

    print(f"[SERVER] Prêt → ouvre http://<IP_DU_PI>:{PORT} dans ton navigateur")
    app.run(host="0.0.0.0", port=PORT, threaded=True)
