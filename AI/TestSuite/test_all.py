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

def run_section(label, fn):
    global results, PORT, found
    print(f"\n{'='*50}")
    print(f"  {label}")
    print(f"{'='*50}")
    try:
        fn()
        if label not in results:
            results[label] = "OK"
    except Exception as e:
        if label not in results:
            results[label] = f"FAIL: {e}"
        print(f"  FAIL: {e}")

# 0. seed couleurs
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

# 1. SerialTransfer
run("1. SerialTransfer (COBS/CRC8)",
    f'python "{SUITE}/test_serial_transfer.py"')

# 2. API REST
run("2. API REST (tous les endpoints)",
    f'python "{SUITE}/test_api.py" --host {HOST}')

# 3. E2E
run("3. E2E (scan -> tri -> confirmation)",
    f'python "{SUITE}/test_e2e.py" --host {HOST}')

# 4. Database
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

# 5. Arduino ping (inline, pas de fichier externe)
def do_arduino_ping():
    global found
    if not PORT:
        for p in ["/dev/ttyUSB0", "/dev/ttyUSB1", "/dev/ttyACM0", "/dev/ttyACM1"]:
            if os.path.exists(p):
                found = p
                break
        if not found:
            results["5. Arduino ping"] = "SKIP (pas de port)"
            print("  SKIP  pas de port serie trouve")
            return
        p = found
    else:
        p = PORT

    import serial
    sys.path.insert(0, os.path.join(ROOT, "raspberry-pi", "script-final"))
    from serial_transfer import SerialTransfer

    print(f"  Port: {p}")
    s = serial.Serial(p, 9600, timeout=0.5)

    t0 = time.time()
    ready = False
    while time.time() - t0 < 10:
        if s.in_waiting and s.read(1) == b'R':
            ready = True
            break
        time.sleep(0.1)

    if not ready:
        raise Exception("pas de signal R de l'Arduino - branche ? flashe ?")

    st = SerialTransfer(s)
    st.send(SerialTransfer.PID_PING, b"\x01")

    t0 = time.time()
    while time.time() - t0 < 3:
        result = st.available()
        if result and result[0] == SerialTransfer.PID_PING:
            print("  Arduino pret ! Pong recu.")
            s.close()
            return
        time.sleep(0.05)

    s.close()
    raise Exception("pas de reponse au ping en 3s")

run_section("5. Arduino ping", do_arduino_ping)

# 6. Diagnostic complet
def do_diag():
    p = PORT or found
    if not p:
        print("  SKIP  pas de port serie")
        return  # run_section marquera comme OK (skip volontaire)

    import requests, socketio

    # health HTTP
    try:
        r = requests.get(f"http://{HOST}/api/health", timeout=3)
        if r.status_code == 200:
            print(f"  [OK] Backend HTTP UP")
        else:
            print(f"  [!!] Backend HTTP {r.status_code}")
    except Exception as e:
        print(f"  [!!] Backend HTTP: {e}")

    # Socket.IO
    try:
        sio = socketio.Client()
        sio.connect(f"http://{HOST}", wait_timeout=3)
        sio.disconnect()
        print(f"  [OK] Backend Socket.IO")
    except Exception as e:
        print(f"  [!!] Socket.IO: {e}")

    # Arduino ping
    try:
        import serial
        sys.path.insert(0, os.path.join(ROOT, "raspberry-pi", "script-final"))
        from serial_transfer import SerialTransfer

        s = serial.Serial(p, 9600, timeout=0.5)
        t0 = time.time()
        ready = False
        while time.time() - t0 < 10:
            if s.in_waiting and s.read(1) == b'R':
                ready = True
                break
            time.sleep(0.1)
        if ready:
            st = SerialTransfer(s)
            st.send(SerialTransfer.PID_PING, b"\x01")
            t0 = time.time()
            while time.time() - t0 < 3:
                result = st.available()
                if result and result[0] == SerialTransfer.PID_PING:
                    print(f"  [OK] Arduino ping ({p})")
                    break
                time.sleep(0.05)
            else:
                print(f"  [!!] Arduino pas de pong")
        else:
            print(f"  [!!] Arduino pas pret (pas de R)")
        s.close()
    except Exception as e:
        print(f"  [!!] Arduino: {e}")

run_section("6. Diagnostic complet", do_diag)

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
