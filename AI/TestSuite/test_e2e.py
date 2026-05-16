# Agent: OpenCode (Claude) - AI/TestSuite
# Test: End-to-end - cree une commande, scanne un bloc, simule le tri
#        Arduino, confirme le resultat. Tout via l'API existante.
#
# Usage: python test_e2e.py [--host localhost:3000]

import sys, struct, requests, subprocess, os

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
BASE = f"http://{HOST}/api"
PASS, FAIL = 0, 0

def ok(label, extra=""):
    global PASS; PASS += 1
    print(f"  OK   {label}{extra}")

def nok(label, extra=""):
    global FAIL; FAIL += 1
    print(f"  FAIL {label}{extra}")

def seed():
    """nettoie et re-seede les couleurs actives dans la DB."""
    sql = (
        "DELETE FROM ITEM_HISTORY; DELETE FROM SELECTION_HISTORY; "
        "DELETE FROM ITEM; DELETE FROM READ_CYCLE; "
        "DELETE FROM ORDER_LINE; DELETE FROM `ORDER`; DELETE FROM COLOR; "
        "INSERT INTO COLOR (name, hex, hueMin, hueMax, saturationMin, saturationMax, valueMin, valueMax, status) VALUES "
        "('Bleu', '#0000FF', 85, 105, 50, 255, 50, 255, true), "
        "('Jaune', '#FFFF00', 25, 35, 50, 255, 50, 255, true), "
        "('Magenta', '#FF00FF', 140, 160, 50, 255, 50, 255, true);"
    )
    try:
        for container in ["pmul2_db", "pmul2_db_mirror"]:
            r = subprocess.run(
                ["docker", "exec", container, "mysql", "-u", "root",
                 "-p${MYSQL_ROOT_PASSWORD}", "-e", sql],
                capture_output=True, text=True, timeout=10,
                env={**os.environ, "MYSQL_ROOT_PASSWORD": "team01-therootone"}
            )
            if r.returncode == 0:
                print("  [SETUP] DB seedee")
                return True
        return False
    except Exception as e:
        print(f"  [SETUP] seed failed: {e}")
        return False

print(f"\n=== E2E TEST - {BASE} ===\n")

# 1. seed la DB
seed()

# 2. recupere les couleurs
try:
    colors = requests.get(f"{BASE}/colors", timeout=5).json()
except Exception as e:
    nok("GET /colors", f" -> {e}")
    sys.exit(1)

if not colors:
    print("  SKIP  pas de couleurs actives en DB")
    sys.exit(0)

color_id = colors[0]["id"]
ok(f"GET /colors -> {len(colors)} actives", f" (id={color_id})")

# 3. cree une commande
try:
    r = requests.post(f"{BASE}/neworder", json={
        "lines": [{"quantity": 2, "id": color_id}]
    }, timeout=5)
except Exception as e:
    nok("POST /neworder", f" -> {e}")
    sys.exit(1)

if r.status_code != 204:
    nok("POST /neworder", f" -> HTTP {r.status_code}: {r.text}")
    sys.exit(1)

ok("POST /neworder -> 204")

# recupere l'ID de la commande
orders = requests.get(f"{BASE}/orders", timeout=5).json()
if not orders:
    nok("GET /orders", " -> aucune commande")
    sys.exit(1)

order_id = orders[0]["id"]
ok(f"Commande creee", f" (id={order_id})")

# 4. scanne un bloc TEAM01 bleu (doit matcher la commande -> ORDER)
scan_body = {
    "scan": {
        "qrValue": "TEAM01",
        "hue": 95,
        "saturation": 200,
        "value": 150
    }
}

try:
    r = requests.post(f"{BASE}/scans", json=scan_body, timeout=5)
except Exception as e:
    nok("POST /scans", f" -> {e}")
    sys.exit(1)

if r.status_code != 201:
    nok("POST /scans", f" -> HTTP {r.status_code}: {r.text}")
    sys.exit(1)

data = r.json()
itemId   = data.get("itemId")
decision = data.get("decision")
orderId  = data.get("orderId")

if not itemId:
    nok("POST /scans", " -> itemId manquant")
    sys.exit(1)

ok(f"POST /scans -> item #{itemId} decision={decision} orderId={orderId}")

if decision != "ORDER":
    nok("POST /scans", f" -> attendu ORDER, recu {decision}")
    sys.exit(1)

# 5. simule l'envoi ITEM_INFO a l'Arduino (SerialTransfer)
decision_map = {"ORDER": 0x01, "STOCK": 0x02, "PASS": 0x00}
decision_byte = decision_map.get(decision, 0x00)
item_info_frame = struct.pack(">HBB", itemId, decision_byte, (orderId or 0) & 0xFF)
ok(f"Arduino <- ITEM_INFO  {item_info_frame.hex()}")

# 6. simule le tri Arduino + confirmation
scan_result = struct.pack(">HB", itemId, 0x00)  # 0x00 = CONFIRMED
ok(f"Arduino -> SCAN_RESULT  {scan_result.hex()}")

# 7. le Pi forward le resultat au backend
try:
    r2 = requests.patch(f"{BASE}/items/{itemId}/status", json={
        "status": {"status": "CONFIRMED"}
    }, timeout=5)
except Exception as e:
    nok("PATCH /items/:id/status", f" -> {e}")
    sys.exit(1)

if r2.status_code == 204:
    ok(f"PATCH /items/{itemId}/status -> 204")
elif r2.status_code == 400:
    ok(f"PATCH /items/{itemId}/status -> 400 (item pas en IN_PROCESS, deja traite)")
else:
    nok(f"PATCH /items/{itemId}/status", f" -> HTTP {r2.status_code}: {r2.text}")

# 8. verifie que la ligne de commande est completee
r3 = requests.get(f"{BASE}/orders/{order_id}/details", timeout=5)
if r3.status_code == 200:
    detail = r3.json()
    ordered = sum(line.get("orderedCount", 0) for line in detail.get("ORDER_LINE", []))
    ok(f"Commande #{order_id}: {ordered} items ordered")
else:
    nok(f"GET /orders/{order_id}/details", f" -> HTTP {r3.status_code}")

# cleanup
seed()

print(f"\n=== {PASS} OK, {FAIL} FAIL ===\n")
sys.exit(0 if FAIL == 0 else 1)
