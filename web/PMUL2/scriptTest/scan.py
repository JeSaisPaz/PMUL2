import requests
import random
import time
import json

BASE_URL = "http://localhost:3000/api"

# Liste des équipes (nettoyée de la chaîne de test trop longue pour la lisibilité)
TEAMS = ["TEAM01", "TEAM02", "TEAM03", "TEAM04", "TEAM05"]

# Tes nouvelles plages de couleurs basées sur les données fournies
COLOR_RANGES = [
    { "name": "Red",     "h": (0, 10),   "s": (50, 100), "v": (50, 100) },
    { "name": "Orange",  "h": (11, 25),  "s": (50, 100), "v": (50, 100) },
    { "name": "Yellow",  "h": (26, 35),  "s": (50, 100), "v": (50, 100) },
    { "name": "Green",   "h": (36, 150), "s": (50, 100), "v": (50, 100) },
    { "name": "Blue",    "h": (151, 260),"s": (50, 100), "v": (50, 100) },
    { "name": "Magenta", "h": (261, 360),"s": (50, 100), "v": (50, 100) },
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
        # Envoi de la requête POST
        res = requests.post(f"{BASE_URL}/scans", json={"scan": scan})
        
        print(f"\n>>> SCAN ENVOYÉ : {scan['qrValue']}")
        print(f"Détails : H:{scan['hue']} S:{scan['saturation']} V:{scan['value']}")
        print(f"Statut HTTP : {res.status_code}")
        
        # Affichage du JSON de réponse formaté
        try:
            response_json = res.json()
            print("Réponse du serveur :")
            print(json.dumps(response_json, indent=4, ensure_ascii=False))
        except json.JSONDecodeError:
            print(f"Réponse brute (non-JSON) : {res.text}")
            
        print("-" * 40)

    except requests.exceptions.ConnectionError:
        print("CRITIQUE : Impossible de contacter le serveur sur", BASE_URL)

if __name__ == "__main__":
    COUNT = 10   # Nombre de scans à simuler
    DELAY = 1.0  # Pause entre les envois

    print(f"Démarrage de la simulation ({COUNT} scans)...\n")

    for i in range(COUNT):
        scan_data = random_scan()
        send_scan(scan_data)
        time.sleep(DELAY)

    print("\nSimulation terminée.")