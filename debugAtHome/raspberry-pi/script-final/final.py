# Connecte l'Arduino (Série), la Caméra (QR/Couleurs) et le Serveur Web (API/SocketIO)
# Driver Raspberry Pi - pont ultra simple entre:
#   - l'Arduino (SerialTransfer via USB)
#   - la camera (QR + detection couleur)
#   - le backend (API REST)
#
# Aucune logique metier ici. Le backend decide tout.
# Le Pi fait juste passer les trames et scanner des blocs.

import os
import sys
import time
import signal
import serial
import requests
import cv2
import socketio
import numpy as np
from picamera2 import Picamera2
from serial_transfer import SerialTransfer

# ==========================================
# CONFIGURATION GLOBALE
# ==========================================
BAUD = 115200
BACKEND_URL = "http://localhost:3000"
PID_CURRENT_ORDER = 0x07

# Correspondance des couleurs (Nom en base de données -> ID octet pour Arduino)
COLOR_MAP = {
    "jaune": 0x01, "yellow": 0x01,
    "bleu":  0x02, "blue":   0x02,
    "magenta": 0x03, "pink": 0x03,
    "brun":  0x04, "brown":  0x04
}

running = True
sio = socketio.Client()

# ==========================================
# FONCTIONS UTILITAIRES
# ==========================================
def log(msg):
    """Affiche un message dans la console et l'envoie au serveur Web pour les logs en direct."""
    print(msg)
    try:
        log_data = {"msg": msg, "time": time.strftime("%H:%M:%S")}
        requests.post(f"{BACKEND_URL}/api/python/logs", json=log_data, timeout=1)
    except Exception:
        pass # On ignore l'erreur si le serveur est indisponible

def cleanup(signum=None, frame=None):
    """Ferme proprement toutes les connexions (Caméra, Série, Socket) lors de l'arrêt du script."""
    global running
    print("\n[!] Arrêt du système...")
    running = False
    
    try: cam.stop()
    except: pass
    
    try: s.close()
    except: pass
    
    if sio.connected:
        sio.disconnect()
        
    sys.exit(0)

# Capture les signaux d'arrêt (Ctrl+C, etc.)
signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

# ==========================================
# INITIALISATION ARDUINO
# ==========================================
def find_arduino_port():
    """Recherche automatiquement le port de l'Arduino Mega."""
    try:
        import serial.tools.list_ports
        for p in serial.tools.list_ports.comports():
            # Vérifie l'identifiant matériel de l'Arduino Mega (VID: 2341, PID: 0042)
            if p.vid and p.pid and format(p.vid, "04x") == "2341" and format(p.pid, "04x") == "0042":
                return p.device
                
        # En cas d'échec, on tente les ports par défaut classiques
        for fallback_port in ["/dev/ttyACM0", "/dev/ttyACM1", "/dev/ttyUSB0", "/dev/ttyUSB1"]:
            if os.path.exists(fallback_port): 
                return fallback_port
    except Exception:
        pass
    return None

port = find_arduino_port()
if not port:
    print("[!] Arduino introuvable. Vérifiez le câble USB.")
    sys.exit(1)

# Connexion série
s = serial.Serial(port, BAUD, timeout=0.5)
print("[BOOT] Attente de la synchronisation avec l'Arduino...")

# Attente du caractère 'R' envoyé par l'Arduino au démarrage
t0 = time.time()
while time.time() - t0 < 15:
    if s.in_waiting and s.read(1) == b'R':
        log("[BOOT] Arduino prêt et synchronisé !")
        break
    time.sleep(0.1)
else:
    print("[!] L'Arduino ne répond pas. Vérifiez le téléversement du code.")
    sys.exit(1)

st = SerialTransfer(s)

# ==========================================
# INITIALISATION CAMÉRA
# ==========================================
print("[CAM] Initialisation de la caméra...")
cam = Picamera2()
config = cam.create_preview_configuration()
config["main"]["size"] = (640, 480)
config["main"]["format"] = "RGB888"

cam.configure(config)
cam.start()
time.sleep(0.5)

# Fixation des paramètres d'image pour avoir des couleurs stables
cam.set_controls({
    "AwbEnable": True, 
    "AwbMode": 1,        # Mode lumière artificielle
    "Saturation": 1.0, 
    "Sharpness": 1.5
})
time.sleep(1.0) # Laisse le temps à la balance des blancs de se stabiliser
log("[CAM] Caméra prête")

# ==========================================
# TRAITEMENT D'IMAGE (QR & COULEURS)
# ==========================================
def decodeFrame(cam):
    """
    Prend une photo, cherche un QR code et analyse la couleur autour.
    Retourne : (Texte du QR, Teinte, Saturation, Valeur lumineuse).
    """
    detector = cv2.QRCodeDetector()
    qr_text = ""
    points = None
    frame = None
    
    # 1. Tenter de lire le QR code (jusqu'à 4 essais)
    for attempt in range(1, 5):
        frame = cam.capture_array()
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        
        # On améliore le contraste de l'image pour aider la détection
        clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))
        img_contraste = clahe.apply(gray)
        
        qr_text, points, _ = detector.detectAndDecode(img_contraste)
        if qr_text: 
            break # Succès, on sort de la boucle
            
        time.sleep(0.15) # Pause avant le prochain essai

    # Si aucun QR code n'est trouvé, on renvoie des valeurs vides
    if not qr_text or points is None:
        return "", 0, 0, 0

    # 2. Analyser la couleur autour du QR code trouvé
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    pts = points[0].astype(int)
    
    # Calcul de la boîte englobante du QR code
    rx, ry = int(pts[:, 0].min()), int(pts[:, 1].min())
    rw, rh = int(pts[:, 0].max()) - rx, int(pts[:, 1].max()) - ry
    
    # On va regarder deux petites zones (16x16 pixels) à gauche et à droite du QR
    cy = ry + (rh // 2)
    patch_size = 16
    zones_a_analyser = [
        (rx - 15 - patch_size, cy - patch_size // 2), # Zone à gauche
        (rx + rw + 15,         cy - patch_size // 2)  # Zone à droite
    ]

    hues, sats, vals = [], [], []
    hauteur_img, largeur_img = frame.shape[:2]

    for sx, sy in zones_a_analyser:
        # Vérifier que la zone ne déborde pas de l'image
        if 0 <= sx <= largeur_img - patch_size and 0 <= sy <= hauteur_img - patch_size:
            patch = hsv[sy : sy+patch_size, sx : sx+patch_size]
            
            # On prend la médiane des pixels pour éviter les reflets aberrants
            hues.append(np.median(patch[:, :, 0]))
            sats.append(np.median(patch[:, :, 1]))
            vals.append(np.median(patch[:, :, 2]))

    # Si on n'a pas pu analyser les zones, on renvoie juste le QR
    if not hues: 
        return qr_text, 0, 0, 0
        
    # On fait la moyenne des zones analysées
    return qr_text, int(np.mean(hues)), int(np.mean(sats)), int(np.mean(vals))

# ==========================================
# GESTION DES REQUÊTES VERS LE WEB
# ==========================================
def fetchAndSendColors():
    """Demande la liste des couleurs actives au Web et l'envoie à l'Arduino."""
    try:
        r = requests.get(f"{BACKEND_URL}/api/colors", timeout=3)
        if r.status_code != 200: 
            return
            
        couleurs_actives = []
        
        # Parcourir les couleurs reçues du Web
        for c in r.json():
            nom = c.get("name", "").lower()
            est_active = c.get("status", False)
            
            # Si elle est active et reconnue dans notre dictionnaire local
            if est_active and nom in COLOR_MAP:
                couleurs_actives.append(COLOR_MAP[nom])
                
        # Envoi à l'Arduino (Format: [Nombre_de_couleurs, ID1, ID2, ...])
        if couleurs_actives:
            payload = bytes([len(couleurs_actives)] + couleurs_actives)
            st.send(SerialTransfer.PID_COLOR_LIST, payload)
            
    except Exception as e:
        log(f"[!] Erreur récupération couleurs: {e}")

def fetchAndSendCurrentOrder():
    """Demande la commande prioritaire au Web et l'envoie à l'Arduino."""
    try:
        r = requests.get(f"{BACKEND_URL}/api/orders/current", timeout=3)
        
        # S'il n'y a pas de commande
        if r.status_code == 404:
            payload_vide = bytes([0, 0, 0, 0, 0])
            st.send(PID_CURRENT_ORDER, payload_vide)
            return
            
        if r.status_code == 200:
            data = r.json()
            order_id = data.get("id", 0)
            
            # Tableau pour stocker les quantités : [Jaune, Bleu, Magenta, Brun]
            quantites = [0, 0, 0, 0]
            
            for line in data.get("ORDER_LINE", []):
                nom_couleur = line.get("COLOR", {}).get("name", "").lower()
                qty = line.get("quantity", 0)
                
                if nom_couleur in COLOR_MAP:
                    # L'index correspond à l'ID de la couleur moins 1
                    # (ex: Jaune a l'ID 1 -> Index 0)
                    index = COLOR_MAP[nom_couleur] - 1
                    quantites[index] += qty
                    
            # Payload: [ID Commande, Qté_Jaune, Qté_Bleu, Qté_Magenta, Qté_Brun]
            payload = bytes([order_id & 0xFF] + quantites)
            st.send(PID_CURRENT_ORDER, payload)
            
    except Exception as e:
        log(f"[!] Erreur récupération commande en cours: {e}")

# ==========================================
# RÉPONSES AUX TRAMES DE L'ARDUINO
# ==========================================
def handleScanNeeded():
    """L'Arduino signale qu'une boîte est prête à être scannée."""
    qr_text, hue, sat, val = decodeFrame(cam)
    scan_data = {"qrValue": qr_text, "hue": hue, "saturation": sat, "value": val}
    
    try:
        # Envoi des données du scan au Web pour qu'il prenne une décision
        r = requests.post(f"{BACKEND_URL}/api/scans", json={"scan": scan_data}, timeout=5)
        if r.status_code != 201: 
            return
            
        # Récupération de la décision du Web
        reponse_web = r.json()
        item_id = reponse_web["itemId"]
        decision_texte = reponse_web["decision"]
        order_id = reponse_web.get("orderId", 0)
        
        # Traduction de la décision en octet pour l'Arduino
        decision_octet = 0x00 # Défaut: PASS
        if decision_texte == "ORDER": decision_octet = 0x01
        elif decision_texte == "STOCK": decision_octet = 0x02
        
        # Format: [ID_Haut, ID_Bas, Decision, ID_Commande]
        payload = bytes([
            (item_id >> 8) & 0xFF, 
            item_id & 0xFF,
            decision_octet,
            order_id & 0xFF
        ])
        st.send(SerialTransfer.PID_ITEM_INFO, payload)
        
    except Exception as e: 
        log(f"[!] Erreur lors de la décision de scan: {e}")

def handleScanResult(payload):
    """L'Arduino confirme qu'une boîte a bien été poussée ou ignorée."""
    if len(payload) < 3: return
    
    item_id = (payload[0] << 8) | payload[1]
    status_str = "CONFIRMED" if payload[2] == 0x00 else "FAILED"
    
    try:
        r = requests.patch(f"{BACKEND_URL}/api/items/{item_id}/status", json={"status": {"status": status_str}}, timeout=5)
        
        # Mise à jour du compteur de commandes terminées sur l'écran LCD
        if r.status_code in (200, 201) and r.content:
            data = r.json()
            if "completedOrdersCount" in data:
                count = data["completedOrdersCount"]
                payload_count = bytes([(count >> 8) & 0xFF, count & 0xFF])
                st.send(SerialTransfer.PID_COMPLETED_COUNT, payload_count)
                
    except Exception as e: 
        log(f"[!] Erreur lors de la validation du bloc: {e}")

def handleLocalOrder(payload):
    """L'utilisateur a tapé une commande sur le pavé numérique de l'Arduino."""
    if len(payload) < 2 or payload[0] == 0: return
    
    nombre_de_lignes = payload[0]
    
    try:
        # Récupérer les vrais IDs des couleurs dans la base de données
        r_colors = requests.get(f"{BACKEND_URL}/api/colors", timeout=10)
        if r_colors.status_code != 200: return
        
        # Dictionnaire: Octet Arduino -> ID Base de données
        byte_to_db_id = {}
        for c in r_colors.json():
            nom = c.get("name", "").lower()
            if nom in COLOR_MAP:
                byte_to_db_id[COLOR_MAP[nom]] = c["id"]
                
        # Construction de la commande pour l'API
        lignes_commande = []
        for i in range(nombre_de_lignes):
            color_byte = payload[1 + i*2]
            quantite = payload[1 + i*2 + 1]
            
            if color_byte in byte_to_db_id and quantite > 0:
                lignes_commande.append({
                    "id": byte_to_db_id[color_byte], 
                    "quantity": quantite
                })
        
        # Envoi au backend
        if lignes_commande: 
            requests.post(f"{BACKEND_URL}/api/neworder", json={"lines": lignes_commande}, timeout=5)
            
    except Exception as e: 
        log(f"[!] Erreur création commande locale: {e}")

def handleSensorStatus(payload):
    """Met à jour l'état visuel des capteurs infrarouges sur l'interface Web."""
    if not payload: return
    masque = payload[0]
    
    # Le masque bit à bit indique l'état de chaque capteur (1 ou 0)
    capteurs = [
        {"name": "IR SCAN",  "state": 1 if masque & 0x01 else 0},
        {"name": "IR NEXT",  "state": 1 if masque & 0x02 else 0},
        {"name": "IR STOCK", "state": 1 if masque & 0x04 else 0},
        {"name": "IR ORDER", "state": 1 if masque & 0x08 else 0},
        {"name": "IR PASS",  "state": 1 if masque & 0x10 else 0}
    ]
    
    try: 
        requests.post(f"{BACKEND_URL}/api/sensors", json={"sensors": capteurs}, timeout=2)
    except: 
        pass # Pas grave si ça échoue, c'est juste de l'affichage

def handleArduinoFrame():
    """Fonction principale de lecture du port série. Aiguille la trame reçue."""
    result = st.available()
    if not result: return
    
    pid, payload = result

    if pid == SerialTransfer.PID_STATUS and payload and payload[0] == SerialTransfer.STATUS_SCAN_NEEDED:
        handleScanNeeded()
    elif pid == SerialTransfer.PID_SCAN_RESULT:
        handleScanResult(payload)
    elif pid == SerialTransfer.PID_SENSOR_STATUS:
        handleSensorStatus(payload)
    elif pid == SerialTransfer.PID_LOCAL_ORDER:
        handleLocalOrder(payload)

# ==========================================
# EVENEMENTS SOCKET.IO
# ==========================================
@sio.on('connect')
def on_connect():
    log("[SIO] Connecté au backend Web")
    fetchAndSendColors()
    fetchAndSendCurrentOrder()

@sio.on('color_event')
def on_color_event():
    fetchAndSendColors()

@sio.on('order_event')
def on_order_event():
    fetchAndSendCurrentOrder()

# ==========================================
# BOUCLE PRINCIPALE
# ==========================================
def main():
    global running
    log("[PI_DRIVER] Système prêt. En attente de blocs sur le convoyeur...")
    
    connected = False
    
    # Tentatives de connexion persistantes au WebSockets
    while running and not connected:
        try:
            sio.connect(BACKEND_URL, transports=['websocket'])
            connected = True
        except Exception:
            time.sleep(5)
            
    # Boucle d'écoute ininterrompue de l'Arduino
    while running:
        try: 
            handleArduinoFrame()
        except OSError: 
            # Erreur critique (câble débranché par exemple)
            cleanup()
            
        time.sleep(0.05) # Petite pause pour ne pas surcharger le processeur

if __name__ == "__main__":
    main()