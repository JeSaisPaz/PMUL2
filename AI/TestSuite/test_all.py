# Agent: OpenCode (Claude) - AI/TestSuite
# Test: ALL - lance tous les tests du projet en une commande
#   SerialTransfer, API, E2E, DB, ping Arduino, diag complet
#
# Usage: python test_all.py [--host localhost:3000] [--port /dev/ttyUSB0]

import sys, os, subprocess, time, json

HOST = sys.argv[2] if len(sys.argv) > 2 else "localhost:3000"
PORT = sys.argv[4] if len(sys.argv) > 4 else None
found = None
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SUITE = os.path.join(ROOT, "AI", "TestSuite")
WEB   = os.path.join(ROOT, "web", "PMUL2", "pmul2-team01-app")
PI    = os.path.join(ROOT, "raspberry-pi", "script-final")

results = {}

def run(label, cmd, cwd=None, docker_exec=None):
    global results
    print(f"\n{'='*50}")
    print(f"  {label}")
    print(f"{'='*50}")
    try:
        if docker_exec:
            full_cmd = ["docker", "exec", "-i", "-w", "/home/node/app", docker_exec, "sh", "-c", cmd]
            r = subprocess.run(full_cmd, capture_output=True, text=True, timeout=120)
        else:
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

# 0. seed couleurs dans la DB (via Prisma dans le container Docker)
# le container ne monte que pmul2-team01-app donc on copie le script dedans
seed_src = os.path.join(SUITE, "seed_colors.js")
seed_dst = os.path.join(WEB, "seed_colors.js")
try:
    import shutil
    shutil.copy(seed_src, seed_dst)
    run("0. Seed couleurs (Prisma)",
        "node seed_colors.js",
        docker_exec="pmul2_app")
except Exception as e:
    results["0. Seed couleurs"] = f"CRASH: {e}"
    print(f"  CRASH: {e}")

# 1. SerialTransfer (pas de hardware)
run("1. SerialTransfer (COBS/CRC8)",
    f'python "{SUITE}/test_serial_transfer.py"')

# 2. API REST (backend doit tourner)
run("2. API REST (tous les endpoints)",
    f'python "{SUITE}/test_api.py" --host {HOST}')

# 3. E2E (scan complet)
run("3. E2E (scan -> tri -> confirmation)",
    f'python "{SUITE}/test_e2e.py" --host {HOST}')

# 4. Database (Prisma schema) - dans le container Docker
db_src = os.path.join(SUITE, "test_database.js")
db_dst = os.path.join(WEB, "test_database.js")
try:
    import shutil
    shutil.copy(db_src, db_dst)
    run("4. Database (Prisma schema FK)",
        "node test_database.js",
        docker_exec="pmul2_app")
except Exception as e:
    results["4. Database"] = f"CRASH: {e}"
    print(f"  CRASH: {e}")

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
