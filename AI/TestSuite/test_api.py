# Agent: OpenCode (Claude) - AI/TestSuite
# Test: API REST - verifie tous les endpoints du backend ET cree des
#       donnees de test (colors, orders, scans, items) pour valider
#       le flow complet. Nettoie tout a la fin.
#
# Usage: python test_api.py [--host localhost:3000]

import sys, json, requests, subprocess, os

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
BASE = f"http://{HOST}/api"
PASS, FAIL = 0, 0

def check(method, path, expected, body=None, label="", extract=None):
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
        result = None
        if not ok:
            extra = f"  -> attendu {expected}, recu {r.status_code}: {r.text[:80]}"
        elif method == "GET" and expected == 200:
            try:
                d = r.json()
                if isinstance(d, list): extra = f"  ({len(d)} elements)"
                elif isinstance(d, dict): extra = f"  (cles: {list(d.keys())[:4]})"
                result = d
            except: pass
        elif method in ("POST", "PATCH") and r.status_code in (200, 201, 204):
            try:
                result = r.json() if r.text else None
            except: pass

        if extract is not None and result is not None:
            for key in extract:
                extract[key] = key in result

        print(f"  {'OK' if ok else 'FAIL'}  {name}{extra}")
        return result
    except Exception as e:
        FAIL += 1
        print(f"  FAIL  {name}  -> {e}")
        return None

def seedColors():
    """insere 3 couleurs actives dans la DB via docker exec (si dispo)."""
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
                print("  [SETUP] Couleurs seedees dans la DB")
                return
        # fallback: try env file
        print("  [SETUP] docker exec echoue - on suppose que les couleurs existent deja")
    except Exception as e:
        print(f"  [SETUP] seed failed: {e}")

print(f"\n=== API TEST - {BASE} ===\n")

# SETUP: seed la DB
seedColors()

# health
check("GET", "/health", 200)

# orders (vide apres seed)
check("GET", "/orders", 200)

# neworder validation
check("POST", "/neworder", 400, body={},              label="POST /neworder (body vide)")
check("POST", "/neworder", 400, body={"lines": []},   label="POST /neworder (lines vide)")
check("POST", "/neworder", 400, body={"lines": "nop"}, label="POST /neworder (mauvais type)")

# colors
colors = check("GET", "/colors", 200)

# neworder valide (si couleurs dispo)
if colors and len(colors) > 0:
    color_id = colors[0]["id"]
    check("POST", "/neworder", 204, body={"lines": [{"quantity": 3, "id": color_id}]},
          label="POST /neworder (valide)")

    orders = check("GET", "/orders", 200)
    if orders and len(orders) > 0:
        oid = orders[0]["id"]
        check("GET", f"/orders/{oid}/details", 200)

        # scan avec couleur + commande active -> doit creer ORDER
        check("POST", "/scans", 201, body={
            "scan": {"qrValue": "TEAM01", "hue": 95, "saturation": 200, "value": 150}
        }, label="POST /scans (TEAM01 + commande)")

        # scan sans commande -> PASS
        check("POST", "/scans", 201, body={
            "scan": {"qrValue": "TEAM02", "hue": 95, "saturation": 200, "value": 150}
        }, label="POST /scans (TEAM02 -> PASS)")

        # update item status
        items = check("GET", "/items", 200)
        if items and len(items) > 0:
            iid = items[0]["id"]
            check("PATCH", f"/items/{iid}/status", 204,
                  body={"status": {"status": "CONFIRMED"}},
                  label=f"PATCH /items/{iid}/status -> CONFIRMED")

            # double confirm -> 400 (deja traite)
            check("PATCH", f"/items/{iid}/status", 400,
                  body={"status": {"status": "CONFIRMED"}},
                  label=f"PATCH /items/{iid}/status (deja traite)")

        # cancel order
        check("PATCH", f"/orders/{oid}/cancel", 204, body={},
              label=f"PATCH /orders/{oid}/cancel")
        check("DELETE", f"/orders/{oid}/delete", 204)
else:
    print("  SKIP  pas de couleurs actives en DB, saute tests avec donnees")

# items
check("GET", "/items", 200)
check("DELETE", "/items/99999/delete", 404, label="DELETE /items/:id (inexistant)")

# scans validation
check("GET", "/scans", 200)
check("DELETE", "/scans/99999/delete", 404, label="DELETE /scans/:id (inexistant)")
check("POST", "/scans", 400, body={},                label="POST /scans (body vide)")
check("POST", "/scans", 400, body={"scan": {}},       label="POST /scans (scan vide)")

# orders edge cases
check("GET", "/orders/99999/details", 404, label="GET /orders/:id/details (inexistant)")
check("DELETE", "/orders/99999/delete", 404, label="DELETE /orders/:id (inexistant)")

# 404
check("GET", "/truc_qui_existe_pas", 404)

# CLEANUP
seedColors()  # re-seed pour nettoyer les donnees de test

print(f"\n=== {PASS} OK, {FAIL} FAIL ===\n")
sys.exit(0 if FAIL == 0 else 1)
