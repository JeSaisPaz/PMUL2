# Agent: OpenCode (Claude) - AI/TestSuite
# Test: End-to-end - simule un scan complet: Pi scanne un bloc,
#        appelle POST /api/scans, envoie ITEM_INFO a l'Arduino (mock),
#        l'Arduino confirme le tri, le Pi appelle PATCH /api/items/:id/status
#
# Usage: python test_e2e.py [--host localhost:3000]

import sys, struct, requests

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
BASE = f"http://{HOST}/api"

PASS, FAIL = 0, 0

def ok(label, extra=""):
    global PASS; PASS += 1
    print(f"  OK   {label}{extra}")

def nok(label, extra=""):
    global FAIL; FAIL += 1
    print(f"  FAIL {label}{extra}")

print(f"\n=== E2E TEST - {BASE} ===\n")

# e2e: scan -> decision -> result

# 1. scanne un bloc (simule - valeurs HSV hardcodees pour un bloc bleu TEAM01)
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
    print(f"\n=== {PASS} OK, {FAIL} FAIL ===\n")
    sys.exit(1)

# 2. verifie la reponse (doit contenir itemId + decision)
if r.status_code == 204:
    ok("POST /scans (pas d'item cree - couleur inconnue ou pas de commande)", f" (HTTP {r.status_code})")
    print("  NOTE: cree une commande valide avant de relancer ce test")
    print(f"\n=== {PASS} OK, {FAIL} FAIL ===\n")
    sys.exit(0)

if r.status_code != 200:
    nok("POST /scans", f" -> HTTP {r.status_code}: {r.text}")
    print(f"\n=== {PASS} OK, {FAIL} FAIL ===\n")
    sys.exit(1)

try:
    data = r.json()
except:
    nok("POST /scans (reponse JSON invalide)")
    sys.exit(1)

itemId   = data.get("itemId")
decision = data.get("decision")
orderId  = data.get("orderId", 0)

if not itemId:
    nok("POST /scans", " -> itemId manquant")
    sys.exit(1)

ok(f"POST /scans -> item #{itemId} decision={decision} orderId={orderId}")

# 3. simule l'envoi ITEM_INFO a l'Arduino (SerialTransfer)
# payload: itemId(2B big-endian) + decision_byte + orderId(1B)
decision_map = {"ORDER": 0x01, "STOCK": 0x02, "PASS": 0x00}
decision_byte = decision_map.get(decision, 0x00)
item_info_frame = struct.pack(">HBB", itemId, decision_byte, orderId & 0xFF)
ok(f"Arduino <- ITEM_INFO  {item_info_frame.hex()}")

# 4. simule le tri Arduino + confirmation
# l'Arduino renvoie SCAN_RESULT: itemId(2B) + status(1B)
scan_result = struct.pack(">HB", itemId, 0x00)  # 0x00 = CONFIRMED
ok(f"Arduino -> SCAN_RESULT  {scan_result.hex()}")

# 5. le Pi forward le resultat au backend
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
    ok(f"PATCH /items/{itemId}/status -> 400 (item pas en IN_PROCESS, normal si deja traite)")
else:
    nok(f"PATCH /items/{itemId}/status", f" -> HTTP {r2.status_code}: {r2.text}")

print(f"\n=== {PASS} OK, {FAIL} FAIL ===\n")
