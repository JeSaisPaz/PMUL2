import serial
import signal
import sys
import time
import cv2
import mysql.connector
import numpy as np
from pyzbar.pyzbar import decode
from picamera2 import Picamera2, Preview

mydb = mysql.connector.connect(
  host="ip",
  user="username",
  password="password"
)


# Bits et autres pour notre protocol série
BEST_TEAM             = 0x01 # Nous, bien évidement
START_BYTE            = 0x02
END_BYTE              = 0x03
TARGET_ORDER_PREFIX   = 0xFF # On l'utilise pour specifier que c'est une commande que l'on demande
ORDER_DONE_PREFIX     = 0x00 # Pareil, pour specifier que la commande est terminée (traitée)
BAUD                  = 9600
PORT                  = "/dev/serial0"

# objet couleur
class Color:
  YELLOW  = 0x01
  BLUE    = 0x02
  MAGENTA = 0x03

COLOR_NAMES = {Color.YELLOW: "Jaune", Color.BLUE: "Bleu", Color.MAGENTA: "Magenta"}

# objet team
class Team:
  TEAM01 = 0x01
  TEAM02 = 0x02
  TEAM03 = 0x03
  TEAM04 = 0x04
  TEAM05 = 0x05

# objet etat
class State:
  WAITING_ORDER = "En attente de commande"
  PROCESSING    = "En process"
  ORDER_DONE    = "Commande terminée"

# initialisation des variables 
state = State.WAITING_ORDER
stateEnteredAt = 0
currentOrder = None
last_order_status = None
last_sent_block = None
DONE_COOLDOWN = 2.0

# Communication serie
s = serial.Serial(SERIAL_PORT, BAUD)
time.sleep(2)

# Pi -> Arduino
def sendBlockInfo(color, team):
  # Trame: [START_BYTE][coleur][team][END_BYTE]: total de 4 bytes
  s.write(bytes([START_BYTE, color, team, END_BYTE]))
  s.flush()

def sendTargetOrder(team, blue, yellow, magenta):
  checksum = (team + blue + yellow + magenta) & 0xFF
  # Trame: [START_BYTE][TARGET_ORDER_PREFIX][team][bleu][jaune][magenta][checksum][END_BYTE]
  s.write(bytes([START_BYTE, TARGET_ORDER_PREFIX, team, blue, yellow, magenta, END_BYTE]))
  s.flush()

# Arduino -> Pi
def receive_order():
    # Trame: [START_BYTE][team][blue][yellow][magenta][checksum][END_BYTE]
    if s.in_waiting < 7:
        return None # mauvaise taille de trame
    if s.read(1)[0] != START_BYTE:
        return None # premier bit pas le bon
    data = s.read(6)
    if len(data) < 6:
        return None # payload de la mauvaise taille
    team_id, blue, yellow, magenta, checksum_rx, end_byte = data # assignation si tout est ok mais pas de retour encore
    if end_byte != END_BYTE:
        return None # dernier bit pas le bon
    if checksum_rx != ((team_id + blue + yellow + magenta) & 0xFF):
        return None # mauvais checksum
    return {"teamId": team_id, "blue": blue, "yellow": yellow, "magenta": magenta} # tout est ok, on renvoie
  
# setup camera
cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start_preview(Preview.DRM)
cam.start()
time.sleep(2)

# Depuis le frame du code qr, nous retourne le texte qu'il contient et la couleur du block
def decodeFrame(frame_bgr):
    # conversion en HSV pour la detection de couleur
    hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)
    # decodage du code QR
    qr_results = decode(frame_bgr)
    # on handle si il n'y a rien
    if not qr_results:
        return None, None

    obj = qr_results[0]
    h, w = frame_bgr.shape[:2]

    # 10x10 a droite du QR (meme logique que dans notre test-camera.py)
    px = min(obj.rect.left + obj.rect.width + 3, w - 15)
    py = min(obj.rect.top + (obj.rect.height // 2), h - 15)
    patch = hsv[py:py + 10, px:px + 10]
    avgHue = np.mean(patch[:, :, 0])

    # choix de la couleur de retour selon le hue
    if 25 <= avgHue < 35: 
        color = Color.YELLOW
    elif 85 <= avgHue < 105:
        color = Color.BLUE
    elif 140 <= avgHue < 160:
        color = Color.MAGENTA
    else:
        return None, None

    # On essaie de decoder en utf-8
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception: # si ca foire, on renvoie rien
        return None, None

    return qr_text, color

# Parse "O:<[team]:[blue]:[yellow]:[magenta]"
# Ex: "O:2:3:2:1" -> {"team": 2, "blueTarget": 3, "yellowTarget": 2, "magentaTarget": 1}
def parseOrderFromQrText(qr_text):
  # On separe la chaine de caractere a chaque ":"
  parts = qr_text.strip().split(":")

  # On verifie le nombre de morceaux de textes, et si le premier est celui attendu
  if len(parts) != 5 or parts[0] != "O":
      return None
  try: # on essaie d'affecter chaque morceaux du texte a une liste contenant la commande
      team_id = int(parts[1])
      blue    = int(parts[2])
      yellow  = int(parts[3])
      magenta = int(parts[4])
      if not (1 <= team_id <= 5):
          return None
      return {"teamId": team_id, "blueTarget": blue, "yellowTarget": yellow, "magentaTarget": magenta}
  except ValueError:
      return None

# fonction de changement d'etat
def set_state(new_state):
    global state, state_entered_at
    state = new_state # affectation au nouvel etat
    state_entered_at = time.time() # timestamp

# fonction de fermeture avant de shutdown
def cleanup(signum=None, frame=None):
    print("\n[!] Shutting down...")
    cam.stop()
    cam.stop_preview()
    s.close()
    sys.exit(0)

# Interruption si l'utilisateur ferme le script de facon non conventionelle (force le cleanup)
signal.signal(signal.SIGINT, cleanup) 
signal.signal(signal.SIGTERM, cleanup) 

# boucle principale du programme
