import requests
import time

BACKEND_URL = "http://localhost:3000"
DELAY = 2  # secondes entre chaque requête

def handleSensorStatus(payload):
    """L'Arduino envoie l'etat des capteurs IR - on POST au backend."""
    if len(payload) < 1:
        return
    mask = payload[0]
    sensors = [
        {"name": "IR 1", "state": 1 if mask & 0x01 else 0},
        {"name": "IR 2", "state": 1 if mask & 0x02 else 0},
        {"name": "IR 3", "state": 1 if mask & 0x04 else 0},
        {"name": "IR 4", "state": 1 if mask & 0x08 else 0},
        {"name": "IR 5", "state": 1 if mask & 0x10 else 0},
    ]
    try:
        res = requests.post(f"{BACKEND_URL}/api/stats/sensors", json={"sensors": sensors}, timeout=2)
        print(f"[{mask:#04x}] POST {res.status_code} → {[s['name'] + ':' + str(s['state']) for s in sensors]}")
    except Exception as e:
        print(f"Erreur: {e}")

if __name__ == "__main__":
    tests = [
        (0b00000, "Tous OFF"),
        (0b11111, "Tous ON"),
        (0b10101, "ir1 + ir3 + ir5 ON"),
        (0b01010, "ir2 + ir4 ON"),
        (0b00001, "ir1 seul ON"),
    ]

    for mask, label in tests:
        print(f"\n=== {label} ===")
        handleSensorStatus([mask])
        time.sleep(DELAY)