# Agent: OpenCode (Claude) - AI/TestSuite
# Test: ALL - lance tous les tests du projet en une commande
#   SerialTransfer, API, E2E, DB, ping Arduino, diag complet
#
# Usage: python test_all.py [--host localhost:3000] [--port /dev/ttyUSB0]

import sys, os, subprocess, time, json

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
PORT = sys.argv[4] if len(sys.argv) > 4 else None
found = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SUITE = os.path.join(ROOT, "AI", "TestSuite")
WEB   = os.path.join(ROOT, "web", "PMUL2", "pmul2-team01-app")
PI    = os.path.join(ROOT, "raspberry-pi", "script-final")

results = {}

def run(label, cmd, cwd=None):
    print(f"\n{'='*50}")
    print(f"  {label}")
    print(f"{'='*50}")
    try:
        r = subprocess.run(cmd, shell=True, cwd=cwd or ROOT, capture_output=True, text=True, timeout=120)
        print(r.stdout[-2000:] if len(r.stdout) > 2000 else r.stdout)
        if r.stderr.strip():
            print(r.stderr[-500:])
        ok = r.returncode == 0
        results[label] = "OK" if ok else f"FAIL (exit {r.returncode})"
    except subprocess.TimeoutExpired:
        results[label] = "TIMEOUT"
        print("  TIMEOUT")
    except Exception as e:
        results[label] = f"CRASH: {e}"
        print(f"  CRASH: {e}")

# 0. seed couleurs dans la DB (via Prisma)
run("0. Seed couleurs (Prisma)",
    f'node "{SUITE}/seed_colors.js"', cwd=WEB)

# 1. SerialTransfer (pas de hardware)
run("1. SerialTransfer (COBS/CRC8)",
    f'python "{SUITE}/test_serial_transfer.py"')

# 2. API REST (backend doit tourner)
run("2. API REST (tous les endpoints)",
    f'python "{SUITE}/test_api.py" --host {HOST}')

# 3. E2E (scan complet)
run("3. E2E (scan -> tri -> confirmation)",
    f'python "{SUITE}/test_e2e.py" --host {HOST}')

# 4. Database (Prisma + schema)
run("4. Database (Prisma schema FK)",
    f'node "{SUITE}/test_database.js"', cwd=WEB)

# 5. ping Arduino (si port serie dispo)
if PORT:
    run(f"5. Arduino ping ({PORT})",
        f'python "{PI}/ping-test.py"')
else:
    # auto-detect le port
    ports = ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1"]
    for p in ports:
        if os.path.exists(p):
            found = p
            break
    if found:
        run(f"5. Arduino ping ({found})",
            f'python "{PI}/ping-test.py"')
    else:
        results["5. Arduino ping"] = "SKIP (pas de port)"
        print("\n  SKIP  pas de port serie trouve")

# 6. diag complet
if PORT or found:
    p = PORT or found
    run(f"6. Diagnostic complet ({p})",
        f'python "{PI}/diag.py"')
else:
    results["6. Diagnostic"] = "SKIP (pas de port)"
    print("\n  SKIP  pas de port serie")

# resume
print(f"\n{'='*50}")
print(f"  RESULTATS")
print(f"{'='*50}")
all_ok = True
for label, status in results.items():
    ok = status == "OK"
    if not ok: all_ok = False
    print(f"  {'OK' if ok else '!!'}  {label}: {status}")

print(f"\n  {sum(1 for s in results.values() if s == 'OK')}/{len(results)} passes")
sys.exit(0 if all_ok else 1)
