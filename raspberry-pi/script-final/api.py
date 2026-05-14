# fuzz_api.py - teste tous les endpoints de l'API backend
# Usage: python fuzz_api.py [--host localhost:3000]

import sys, json, time, requests

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
BASE = f"http://{HOST}/api"
OK, FAIL = 0, 0

def test(method, path, expected_status, body=None, label=None):
    global OK, FAIL
    url = BASE + path
    try:
        if method == "GET":
            r = requests.get(url, timeout=5)
        elif method == "POST":
            r = requests.post(url, json=body, timeout=5)
        elif method == "PATCH":
            r = requests.patch(url, json=body, timeout=5)
        elif method == "DELETE":
            r = requests.delete(url, timeout=5)

        if r.status_code == expected_status:
            OK += 1
            tag = "OK"
        else:
            FAIL += 1
            tag = "FAIL"

        name = label or f"{method} {path}"
        detail = ""
        if r.status_code != expected_status:
            detail = f" (expected {expected_status}, got {r.status_code})"
        elif method == "GET" and r.status_code == 200 and path != "/health":
            try:
                data = r.json()
                if isinstance(data, list):
                    detail = f" ({len(data)} items)"
                elif isinstance(data, dict):
                    detail = f" (keys: {list(data.keys())[:5]})"
            except Exception:
                pass

        print(f"  [{tag}] {name}{detail}")
    except Exception as e:
        FAIL += 1
        print(f"  [FAIL] {label or path} - {e}")

print(f"\n=== FUZZ API - {BASE} ===\n")

# health ----
test("GET", "/health", 200)

# orders ----
test("GET", "/orders", 200)
test("GET", "/orders/99999/details", 404, label="GET /orders/99999/details (inexistant)")

# neworder ----
test("POST", "/neworder", 400, body={}, label="POST /neworder (body vide)")
test("POST", "/neworder", 400, body={"lines": []}, label="POST /neworder (lines vide)")
test("POST", "/neworder", 400, body={"lines": "pas_un_array"}, label="POST /neworder (mauvais type)")

# cree une commande valide (on a besoin des colors dans la db)
try:
    r_colors = requests.get(f"{BASE}/colors", timeout=5)
    if r_colors.status_code == 200 and len(r_colors.json()) > 0:
        color_id = r_colors.json()[0]["id"]
        test("POST", "/neworder", 204, body={
            "lines": [{"quantity": 3, "id": color_id}]
        }, label="POST /neworder (valide)")

        # recupere l'ID de la commande creee
        r_orders = requests.get(f"{BASE}/orders", timeout=5)
        orders = r_orders.json()
        if len(orders) > 0:
            order_id = orders[0]["id"]
            test("GET", f"/orders/{order_id}/details", 200)
            test("PATCH", f"/orders/{order_id}/cancel", 204, body={},
                 label=f"PATCH /orders/{order_id}/cancel")
            test("DELETE", f"/orders/{order_id}/delete", 204)
    else:
        print("  [SKIP] Pas de colors en DB - saute les tests neworder")
except Exception as e:
    print(f"  [SKIP] Erreur setup orders: {e}")

# items ----
test("GET", "/items", 200)
test("DELETE", "/items/99999/delete", 404, label="DELETE /items/99999 (inexistant)")

try:
    r_items = requests.get(f"{BASE}/items", timeout=5)
    items = r_items.json()
    if len(items) > 0:
        item_id = items[0]["id"]
        test("PATCH", f"/items/{item_id}/status", 400,
             body={"status": {"status": "INVALIDE"}},
             label=f"PATCH /items/{item_id}/status (mauvais status)")
except Exception as e:
    print(f"  [SKIP] Erreur tests items: {e}")

# colors ----
test("GET", "/colors", 200)

# scans ----
test("GET", "/scans", 200)
test("DELETE", "/scans/99999/delete", 404, label="DELETE /scans/99999 (inexistant)")
# POST scan avec donnees invalides
test("POST", "/scans", 400, body={}, label="POST /scans (body vide)")
test("POST", "/scans", 400,
     body={"scan": {}},
     label="POST /scans (scan vide)")

# edge cases ----
test("GET", "/rien_du_tout", 404, label="GET /rien_du_tout (404)")

# resume ----
print(f"\n=== RESULTAT: {OK} OK, {FAIL} FAIL ===\n")
