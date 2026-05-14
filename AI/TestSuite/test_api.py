# Agent: OpenCode (Claude) - AI/TestSuite
# Test: API REST - verifie que tous les endpoints du backend repondent
#       correctement, sans dependance Socket.IO
#
# Usage: python test_api.py [--host localhost:3000]

import sys, json, requests

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
BASE = f"http://{HOST}/api"
PASS, FAIL = 0, 0

def check(method, path, expected, body=None, label=""):
    global PASS, FAIL
    url = f"{BASE}{path}"
    name = label or f"{method} {path}"
    try:
        if method == "GET":     r = requests.get(url, timeout=5)
        elif method == "POST":  r = requests.post(url, json=body, timeout=5)
        elif method == "PATCH": r = requests.patch(url, json=body, timeout=5)
        elif method == "DELETE":r = requests.delete(url, timeout=5)
        else: raise ValueError(f"Unknown method {method}")

        ok = r.status_code == expected
        PASS += ok; FAIL += not ok
        extra = ""
        if not ok:
            extra = f"  -> attendu {expected}, recu {r.status_code}"
        elif method == "GET" and expected == 200:
            try:
                d = r.json()
                if isinstance(d, list): extra = f"  ({len(d)} elements)"
                elif isinstance(d, dict): extra = f"  (cles: {list(d.keys())[:4]})"
            except: pass
        print(f"  {'OK' if ok else 'FAIL'}  {name}{extra}")
    except Exception as e:
        FAIL += 1
        print(f"  FAIL  {name}  -> {e}")

print(f"\n=== API TEST - {BASE} ===\n")

# health
check("GET", "/health", 200)

# orders
check("GET", "/orders", 200)
check("GET", "/orders/99999/details", 404, label="GET /orders/:id/details (inexistant)")
check("DELETE", "/orders/99999/delete", 404, label="DELETE /orders/:id (inexistant)")

# neworder (validation)
check("POST", "/neworder", 400, body={},              label="POST /neworder (body vide)")
check("POST", "/neworder", 400, body={"lines": []},   label="POST /neworder (lines vide)")
check("POST", "/neworder", 400, body={"lines": "nop"}, label="POST /neworder (mauvais type)")

# neworder (valide)
color_id = None
try:
    r = requests.get(f"{BASE}/colors", timeout=5)
    if r.ok and len(r.json()) > 0:
        color_id = r.json()[0]["id"]
except: pass

if color_id:
    check("POST", "/neworder", 204, body={"lines": [{"quantity": 2, "id": color_id}]},
          label="POST /neworder (valide)")
    try:
        orders = requests.get(f"{BASE}/orders", timeout=5).json()
        if orders:
            oid = orders[0]["id"]
            check("GET", f"/orders/{oid}/details", 200)
            check("PATCH", f"/orders/{oid}/cancel", 204, body={}, label=f"PATCH /orders/{oid}/cancel")
            check("DELETE", f"/orders/{oid}/delete", 204)
    except Exception as e:
        print(f"  SKIP  tests ordre -> {e}")
else:
    print("  SKIP  pas de couleurs en DB, saute neworder valide")

# items
check("GET", "/items", 200)
check("DELETE", "/items/99999/delete", 404, label="DELETE /items/:id (inexistant)")

# colors
check("GET", "/colors", 200)

# scans
check("GET", "/scans", 200)
check("DELETE", "/scans/99999/delete", 404, label="DELETE /scans/:id (inexistant)")
check("POST", "/scans", 400, body={},                label="POST /scans (body vide)")
check("POST", "/scans", 400, body={"scan": {}},       label="POST /scans (scan vide)")

# 404
check("GET", "/truc_qui_existe_pas", 404)

# resume
print(f"\n=== {PASS} OK, {FAIL} FAIL ===\n")
sys.exit(0 if FAIL == 0 else 1)
