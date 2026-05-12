import requests
import random
import time

BASE_URL = "http://localhost:3000/api/"

TEAMS = ["TEAM01", "TEAM02", "TEAM03", "TEAM04", "TEAM05", "duqhgduiqdhguqigdqyugdqhvdhjqge_zijgjkdghfyqsgtfd-fdsAZ ILJEIOAEAEGHDEVQHJDVQTGDQGDQB HAJBHJAGEQYUDFGQHDVQGHJDVGDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD"]

# Ranges HSV approximatives pour simuler des couleurs
COLOR_RANGES = [
    {"name": "Rouge",  "h": (0, 10),   "s": (50, 100), "v": (50, 100)},
    {"name": "Vert",   "h": (100, 140),"s": (50, 100), "v": (50, 100)},
    {"name": "Bleu",   "h": (200, 240),"s": (50, 100), "v": (50, 100)},
    {"name": "Jaune",  "h": (50, 70),  "s": (50, 100), "v": (50, 100)},
    {"name": "Random", "h": (0, 360),  "s": (0, 100),  "v": (0, 100)},  # peut être invalide
]

def random_scan():
    team = random.choice(TEAMS)
    color = random.choice(COLOR_RANGES)

    return {
        "qrValue":    team,
        "hue":        random.randint(*color["h"]),
        "saturation": random.randint(*color["s"]),
        "value":      random.randint(*color["v"]),
    }

def send_scan(scan):
    try:
        res = requests.post(f"{BASE_URL}/scans", json={"scan": scan})
        print(f"[{res.status_code}] team={scan['qrValue']} h={scan['hue']} s={scan['saturation']} v={scan['value']}")
    except requests.exceptions.ConnectionError:
        print("Impossible de joindre le serveur")

if __name__ == "__main__":
    COUNT  = 20   # nombre de scans
    DELAY  = 0.5  # secondes entre chaque scan

    print(f"Envoi de {COUNT} scans...\n")

    for i in range(COUNT):
        scan = random_scan()
        send_scan(scan)
        time.sleep(DELAY)

    print("\nTerminé !")