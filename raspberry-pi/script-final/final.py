import collections  # pour la file d'attente FIFO
import select       # pour le read non-bloquant sur stdin
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

# Variables utiles
BEST_TEAM             = 0x01 # nous, bien evidement
START_BYTE            = 0x02 # debut de trame serie
END_BYTE              = 0x03 # fin de trame serie
TARGET_ORDER_PREFIX   = 0xFF # on l'utilise pour specifier que c'est une commande que l'on envoie
STATUS_PREFIX         = 0xFE # pour les trames Busy/Ready (Arduino -> RPi)
ORDER_DONE_PREFIX     = 0x00 # pareil, pour specifier que la commande est terminee (traitee)
STATUS_BUSY           = 0x01
STATUS_READY          = 0x00
BAUD                  = 9600
PORT                  = "/dev/serial0"

HELP_TEXT = """
=== Commandes CLI ===
  order <team> <bleu> <jaune> <magenta>    Ajouter une commande (ex: order 2 3 2 1)
  queue                                    Afficher la file d attente
  queue clear                              Vider la file d attente
  queue remove <n>                         Retirer la commande en position n
  status                                   Afficher l etat complet
  help                                     Ce message
  quit / exit / q                          Quitter
"""

# objet couleur, represente par un byte (0x01, 0x02, 0x03)
class Color:
  YELLOW  = 0x01
  BLUE    = 0x02
  MAGENTA = 0x03

COLOR_NAMES = {Color.YELLOW: "Jaune", Color.BLUE: "Bleu", Color.MAGENTA: "Magenta"}

# objet team, les memes valeurs que dans pmul2-teams.h
class Team:
  TEAM01 = 0x01
  TEAM02 = 0x02
  TEAM03 = 0x03
  TEAM04 = 0x04
  TEAM05 = 0x05
  UNKNOWN = 0xFF  # au cas ou on lit n'importe quoi

  @classmethod # methode de classe pour avoir le bon retour, peu importe le texte lu dans le qr
  def from_qr_text(cls, text):
      t = text.strip().lower()
      return {
          # formats courts (juste le chiffre)
          "1": cls.TEAM01, "2": cls.TEAM02,
          "3": cls.TEAM03, "4": cls.TEAM04,
          "5": cls.TEAM05,
          # formats longs (Team01, team01, TEAM01, etc. grace au .lower())
          "team01": cls.TEAM01, "team02": cls.TEAM02,
          "team03": cls.TEAM03, "team04": cls.TEAM04,
          "team05": cls.TEAM05,
      }.get(t, cls.UNKNOWN)

# objet etat, les differents etats de la machine
class State:
  WAITING_ORDER = "En attente de commande"
  PROCESSING    = "En process"
  ORDER_DONE    = "Commande terminee"

# initialisation des variables globales
state = State.WAITING_ORDER
stateEnteredAt = 0               # timestamp d'entree dans l'etat courant
currentOrder = None              # {"teamId", "blueTarget", "yellowTarget", "magentaTarget"}
lastOrderStatus = None           # dernier OrderUpdate recu de l'Arduino
lastSentBlock = None             # anti-doublon bloc (tuple: (qr_text, couleur))
arduinoBusy = False              # True si l'Arduino est deja sur une commande interne
DONE_COOLDOWN = 2.0              # secondes de pause entre deux commandes

# communication serie avec l'Arduino
s = serial.Serial(PORT, BAUD)
time.sleep(2) # temps de delai pour que la connexion serie soit bien etablie

# Pi -> Arduino
def sendBlockInfo(color, team):
  # Trame: [START_BYTE][couleur][team][END_BYTE] -> total de 4 bytes
  # Meme format que readBlockInfo() dans pmul2-com.cpp
  s.write(bytes([START_BYTE, color, team, END_BYTE]))
  s.flush()

def sendTargetOrder(team, blue, yellow, magenta):
  # Trame: [START_BYTE][0xFF][team][bleu][jaune][magenta][checksum][END_BYTE] -> 8 bytes
  # 0xFF permet a l'Arduino de differencier d'un BlockInfo
  checksum = (team + blue + yellow + magenta) & 0xFF
  s.write(bytes([START_BYTE, TARGET_ORDER_PREFIX,
                  team, blue, yellow, magenta, checksum, END_BYTE]))
  s.flush()

# Arduino -> Pi
def receiveFrame():
    """
    Lit une trame entrante et la dispatche selon son discriminateur
    (le byte juste apres START_BYTE) :
      - 0xFE -> Status (Busy/Ready)
      - 0xFF -> TargetOrder inverse (commande interne Arduino)
      - autre -> OrderUpdate classique (progres de la commande)
    Retourne un dict {"type": "...", ...} ou None si pas de trame complete.
    """
    # Au minimum 4 bytes (la plus petite trame : Status)
    if s.in_waiting < 4:
        return None

    # On verifie le START_BYTE (si c'est pas ca, on jette)
    if s.read(1)[0] != START_BYTE:
        return None

    discriminator = s.read(1)[0]  # le deuxieme byte nous dit le type de trame

    # status (Busy/Ready)
    if discriminator == STATUS_PREFIX:
        if s.in_waiting < 2:
            return None
        status, end_byte = s.read(2)
        if end_byte != END_BYTE:
            return None
        return {"type": "status", "busy": (status == STATUS_BUSY)}

    # TargetOrder inverse (commande interne Arduino)
    if discriminator == TARGET_ORDER_PREFIX:
        if s.in_waiting < 6:
            return None
        data = s.read(6)
        if len(data) < 6:
            return None
        team_id, blue, yellow, magenta, checksum_rx, endByte = data
        if endByte != END_BYTE:
            return None
        if checksum_rx != ((team_id + blue + yellow + magenta) & 0xFF):
            return None
        return {"type": "target_order", "teamId": team_id,
                "blue": blue, "yellow": yellow, "magenta": magenta}

    # OrderUpdate classique (progres de la commande)
    # Trame: [START_BYTE][teamId][blue][yellow][magenta][checksum][END_BYTE] -> 7 bytes
    # On a deja lu START_BYTE et le discriminator (=teamId ici)
    # reste 5 bytes a lire
    if s.in_waiting < 5:
        return None
    data = s.read(5)
    if len(data) < 5:
        return None # payload de la mauvaise taille
    blue, yellow, magenta, checksum_rx, endByte = data
    if endByte != END_BYTE:
        return None # dernier bit pas le bon
    team_id = discriminator  # le deuxieme byte lu etait le teamId
    if checksum_rx != ((team_id + blue + yellow + magenta) & 0xFF):
        return None # mauvais checksum
    return {"type": "order_update", "teamId": team_id,
            "blue": blue, "yellow": yellow, "magenta": magenta} # tout est ok, on renvoie

# setup camera
cam = Picamera2()
cam.configure(cam.create_preview_configuration(main={"size": (640, 480)}))
cam.start_preview(Preview.DRM)
cam.start()
time.sleep(2) # laisser le temps a la camera de demarrer

# File d'attente FIFO (rien a voir avec FIFA, le premier qui fait la blague, je le *****)
orderQueue = collections.deque()
QUEUE_ID_COUNTER = [0]  # petit compteur pour donner un ID unique a chaque commande, TODO: integrer en interne dans la db ?

def enqueueOrder(team_id, blue, yellow, magenta, source="cli"):
    """Ajoute une commande a la file d'attente."""
    QUEUE_ID_COUNTER[0] += 1
    entry = {
        "id": QUEUE_ID_COUNTER[0],
        "teamId": team_id,
        "blue": blue,
        "yellow": yellow,
        "magenta": magenta,
        "source": source,
        "ts": time.time(),
    }
    orderQueue.append(entry)  # on push a la fin (FIFO)
    pos = len(orderQueue)
    print(f"  [QUEUE] Commande #{entry['id']} ajoutee (position {pos}) : "
          f"Team {team_id} | B={blue} Y={yellow} M={magenta} [{source}]")
    return entry

def dequeueNext():
    """Retire et retourne la plus ancienne commande de la file, ou None."""
    if orderQueue:
        entry = orderQueue.popleft()  # on pop du debut (FIFO)
        print(f"  [QUEUE] Commande #{entry['id']} retiree (restant: {len(orderQueue)})")
        return entry
    return None

# Lecture CLI non-bloquante
# (merci stack overflow: https://stackoverflow.com/questions/375427/
#  a-non-blocking-read-on-a-subprocess-pipe-in-python)
def readCLI(timeout=0.0):
    r, _, _ = select.select([sys.stdin], [], [], timeout)
    if r:
        return sys.stdin.readline().strip()
    return None

# envoi de commande vers l'Arduino
def sendOrderNow(team_id, blue, yellow, magenta):
    """
    Envoie immediatement une commande a l'Arduino et declenche le processus
    de tri des caisses. Appelee depuis processQueue() ou tryLaunchOrder().
    """
    global currentOrder, lastSentBlock, arduinoBusy

    # on stocke la commande courante pour l'afficher dans le status
    currentOrder = {"teamId": team_id, "blueTarget": blue,
                    "yellowTarget": yellow, "magentaTarget": magenta}

    sendTargetOrder(team_id, blue, yellow, magenta)  # la trame part ici

    arduinoBusy = True  # on marque l'Arduino comme occupe
    print(f"[ORDER] Commande envoyee : Team {team_id} "
          f"| Bleu={blue} Jaune={yellow} Magenta={magenta}")
    lastSentBlock = None
    setState(State.PROCESSING)


def tryLaunchOrder(team_id, blue, yellow, magenta, source="cli"):
    """
    Tente d'envoyer une commande immediatement.
    Si l'Arduino est occupe ou deja en train de process -> on met en file d'attente.
    """
    if arduinoBusy or state != State.WAITING_ORDER:
        enqueueOrder(team_id, blue, yellow, magenta, source)
        return
    sendOrderNow(team_id, blue, yellow, magenta)


def processQueue():
    """
    Appelée a chaque itération de la boucle principale.
    Si le systeme est pret (WAITING_ORDER et Arduino pas busy),
    depile et envoie la plus ancienne commande en attente.
    """
    if state != State.WAITING_ORDER or arduinoBusy:
        return
    entry = dequeueNext()
    if entry:
        sendOrderNow(entry["teamId"], entry["blue"],
                      entry["yellow"], entry["magenta"])

# detection QR + couleur depuis un frame camera
# depuis le frame du code QR, on retourne le texte qu'il contient et la couleur du block
def decodeFrame(frame_bgr):
    # conversion en HSV pour la detection de couleur
    hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)
    # decodage du code QR
    qr_results = decode(frame_bgr)
    # on handle si il n'y a rien
    if not qr_results:
        return None, None

    obj = qr_results[0]  # on prend le premier QR code trouve dans le frame
    h, w = frame_bgr.shape[:2]

    # 10x10 a droite du QR (meme logique que dans notre test-camera.py)
    px = min(obj.rect.left + obj.rect.width + 3, w - 15)
    py = min(obj.rect.top + (obj.rect.height // 2), h - 15)
    patch = hsv[py:py + 10, px:px + 10]
    avgHue = np.mean(patch[:, :, 0])  # moyenne arithmetique du canal H (teinte)

    # choix de la couleur de retour selon la teinte
    if 25 <= avgHue < 35:
        color = Color.YELLOW
    elif 85 <= avgHue < 105:
        color = Color.BLUE
    elif 140 <= avgHue < 160:
        color = Color.MAGENTA
    else:
        return None, None  # couleur inconnue, on ignore

    # On essaie de decoder le texte du QR en utf-8
    try:
        qr_text = obj.data.decode("utf-8")
    except Exception: # si ca foire, on renvoie rien
        return None, None

    return qr_text, color

# helpers
# fonction de changement d'etat
def setState(newState):
    global state, stateEnteredAt
    state = newState          # affectation au nouvel etat
    stateEnteredAt = time.time()  # timestamp

# fonction de fermeture propre avant de shutdown
def cleanup(signum=None, frame=None):
    print("\n[!] Shutting down...")
    cam.stop()
    cam.stop_preview()
    s.close()
    sys.exit(0)

# Interruption si l'utilisateur ferme le script de facon non conventionelle (force le cleanup)
signal.signal(signal.SIGINT, cleanup)
signal.signal(signal.SIGTERM, cleanup)

# boucle principale
def main():
    global state, currentOrder, lastOrderStatus
    global lastSentBlock, arduinoBusy

    print("PMUL2 - File d'attente active. Tapez 'help' pour les commandes.\n")
    setState(State.WAITING_ORDER)

    while True:
        now = time.time()

        # 1) Toujours écouter ce que l'Arduino nous envoie
        frame_in = receiveFrame()

        # 2) Trames Status (Busy/Ready)
        # l'Arduino nous dit s'il est occupe par une commande interne
        if frame_in and frame_in["type"] == "status":
            if frame_in["busy"] and not arduinoBusy:
                print("[ARDUINO] L'Arduino signale : OCCUPE (commande interne en cours)")
                arduinoBusy = True
            elif not frame_in["busy"] and arduinoBusy:
                print("[ARDUINO] L'Arduino signale : DISPONIBLE")
                arduinoBusy = False

        # 3) Commande terminee
        # l'Arduino envoie un OrderUpdate avec teamId=0x00 pour dire "c'est fini"
        if frame_in and frame_in["type"] == "order_update" \
                and frame_in["teamId"] == ORDER_DONE_PREFIX:
            print(f"[ORDER_DONE] Commande terminee ! (Arduino: teamId=0x00)")
            currentOrder = None
            arduinoBusy = False  # il redevient dispo
            setState(State.ORDER_DONE)

        # 4) Commande interne Arduino
        # si l'Arduino demarre sa propre commande, il nous envoie un TargetOrder
        if frame_in and frame_in["type"] == "target_order":
            if frame_in["teamId"] != ORDER_DONE_PREFIX:
                # La commande interne est prioritaire : on l'accepte meme si BUSY
                # est deja arrive (les deux trames font partie de la meme annonce).
                if state == State.WAITING_ORDER:
                    print(f"\n[ORDER] Commande interne Arduino : Team {frame_in['teamId']} "
                          f"| Bleu={frame_in['blue']} Jaune={frame_in['yellow']} Magenta={frame_in['magenta']}")
                    currentOrder = {"teamId": frame_in["teamId"],
                                    "blueTarget": frame_in["blue"],
                                    "yellowTarget": frame_in["yellow"],
                                    "magentaTarget": frame_in["magenta"]}
                    arduinoBusy = True
                    lastSentBlock = None
                    setState(State.PROCESSING)
                elif arduinoBusy:
                    print("  [ARDUINO] Nouvelle commande interne recue (deja occupe).")

        # 5) Commandes CLI (non-bloquant)
        cli = readCLI(timeout=0.0)
        if cli:
            parts = cli.strip().split()
            if not parts:
                continue
            cmd = parts[0].lower()

            if cmd in ("quit", "exit", "q"):
                cleanup()

            elif cmd == "help":
                print(HELP_TEXT)

            elif cmd == "status":
                busyStr = "OCCUPE" if arduinoBusy else "DISPONIBLE"
                qlen = len(orderQueue)
                print(f"  Etat machine : {state}")
                print(f"  Arduino      : {busyStr}")
                print(f"  File attente : {qlen} commande(s)")
                if qlen > 0:
                    for i, entry in enumerate(orderQueue):
                        age = int(now - entry["ts"])
                        print(f"    #{i+1} [id={entry['id']}] Team {entry['teamId']} "
                              f"B={entry['blue']} Y={entry['yellow']} M={entry['magenta']} "
                              f"({entry['source']}, -{age}s)")
                if currentOrder:
                    print(f"  Commande     : Team {currentOrder['teamId']} "
                          f"| Cible B={currentOrder['blueTarget']} Y={currentOrder['yellowTarget']} M={currentOrder['magentaTarget']}")
                if lastOrderStatus:
                    print(f"  Progres      : teamId={lastOrderStatus['teamId']} "
                          f"B={lastOrderStatus['blue']} Y={lastOrderStatus['yellow']} M={lastOrderStatus['magenta']}")

            elif cmd == "queue":
                if len(parts) >= 2:
                    sub = parts[1].lower()
                    if sub == "clear":
                        n = len(orderQueue)
                        orderQueue.clear()
                        print(f"  [QUEUE] File videe ({n} commande(s) supprimees).")
                    elif sub == "remove" and len(parts) >= 3:
                        try:
                            idx = int(parts[2]) - 1  # 1-indexed pour l'utilisateur
                            if 0 <= idx < len(orderQueue):
                                entry = orderQueue[idx]
                                del orderQueue[idx]
                                print(f"  [QUEUE] Commande #{entry['id']} retiree.")
                            else:
                                print(f"  [!] Position invalide (1-{len(orderQueue)}).")
                        except ValueError:
                            print("  Usage: queue remove <position>")
                    else:
                        print("  Sous-commandes: queue | queue clear | queue remove <n>")
                else:
                    qlen = len(orderQueue)
                    if qlen == 0:
                        print("  File d'attente vide.")
                    else:
                        print(f"  {qlen} commande(s) en attente :")
                        for i, entry in enumerate(orderQueue):
                            age = int(now - entry["ts"])
                            print(f"    #{i+1} [id={entry['id']}] Team {entry['teamId']} "
                                  f"B={entry['blue']} Y={entry['yellow']} M={entry['magenta']} "
                                  f"({entry['source']}, -{age}s)")

            elif cmd == "order":
                if len(parts) != 5:
                    print("  Usage: order <team> <bleu> <jaune> <magenta>")
                    continue
                try:
                    tid = int(parts[1])
                    b   = int(parts[2])
                    y   = int(parts[3])
                    m   = int(parts[4])
                    if not (1 <= tid <= 5):
                        print("  Team doit etre entre 1 et 5.")
                        continue
                except ValueError:
                    print("  Les arguments doivent etre des entiers.")
                    continue

                tryLaunchOrder(tid, b, y, m, source="cli")

            else:
                print(f"  Commande inconnue: {cmd} (tapez 'help')")

        # 6) Machine a etats
        if state == State.WAITING_ORDER:
            # on traite la file d'attente en priorite
            processQueue()
            time.sleep(0.05)

        elif state == State.PROCESSING:
            # on est en train de process une commande : on scanne les blocs en continu
            frame = cam.capture_array()
            if frame is not None:
                qr_text, color = decodeFrame(frame)
                if qr_text is not None and color is not None:
                    # anti-doublon : ne pas renvoyer le meme bloc deux fois de suite
                    boxHash = (qr_text, color)
                    if boxHash != lastSentBlock:
                        team = Team.from_qr_text(qr_text)
                        print(f"  [BLOC] Team {qr_text} | {COLOR_NAMES[color]}  ->  Arduino")
                        sendBlockInfo(color, team)
                        lastSentBlock = boxHash

            # on affiche le progres recu de l'Arduino
            if frame_in and frame_in["type"] == "order_update" \
                    and frame_in["teamId"] != ORDER_DONE_PREFIX:
                lastOrderStatus = frame_in
                print(f"  <--  Progres: teamId={frame_in['teamId']} "
                      f"B={frame_in['blue']} Y={frame_in['yellow']} M={frame_in['magenta']}")

            time.sleep(0.05)

        elif state == State.ORDER_DONE:
            # petit cooldown entre deux commandes, puis on repart
            if now - stateEnteredAt > DONE_COOLDOWN:
                pending = len(orderQueue)
                if pending > 0:
                    print(f"[READY] {pending} commande(s) en attente. Reprise de la file.\n")
                else:
                    print("[READY] En attente d'une nouvelle commande.\n")
                lastSentBlock = None
                setState(State.WAITING_ORDER)
            else:
                time.sleep(0.05)

if __name__ == "__main__":
    main()
