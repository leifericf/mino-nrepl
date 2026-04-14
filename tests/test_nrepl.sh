#!/bin/sh
#
# test_nrepl.sh -- automated protocol tests for mino-nrepl.
#
# Starts the server, exercises each op via bencode over TCP, and
# validates responses. Requires: nc (netcat), python3.
#

set -e

NREPL=./mino-nrepl
PORT=0
PASS=0
FAIL=0
PID=""

cleanup() {
    if [ -n "$PID" ]; then
        kill "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

die() { echo "FAIL: $1"; FAIL=$((FAIL + 1)); }
ok()  { echo "  ok: $1"; PASS=$((PASS + 1)); }

# Build if needed.
if [ ! -x "$NREPL" ]; then
    echo "Building mino-nrepl..."
    make
fi

# Start server on a random port.
$NREPL --port 0 > /tmp/mino-nrepl-test.log 2>&1 &
PID=$!
sleep 0.5

# Read port from .nrepl-port file.
if [ ! -f .nrepl-port ]; then
    echo "FATAL: .nrepl-port not created"
    exit 1
fi
PORT=$(cat .nrepl-port | tr -d '[:space:]')
echo "Server running on port $PORT (pid $PID)"
echo ""

# Use Python for bencode send/recv (more reliable than raw nc).
python3 - "$PORT" <<'PYEOF'
import socket, sys, re, time

port = int(sys.argv[1])
passed = 0
failed = 0

def bencode_str(s):
    return f"{len(s)}:{s}"

def bencode_int(n):
    return f"i{n}e"

def bencode_dict(d):
    parts = ["d"]
    for k in sorted(d.keys()):
        v = d[k]
        parts.append(bencode_str(k))
        if isinstance(v, str):
            parts.append(bencode_str(v))
        elif isinstance(v, int):
            parts.append(bencode_int(v))
    parts.append("e")
    return "".join(parts)

def send_recv(msg, read_timeout=1.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(("127.0.0.1", port))
    s.sendall(msg.encode())
    s.settimeout(read_timeout)
    data = b""
    try:
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    s.close()
    return data.decode(errors="replace")

def ok(name):
    global passed
    passed += 1
    print(f"  ok: {name}")

def fail(name, detail=""):
    global failed
    failed += 1
    msg = f"FAIL: {name}"
    if detail:
        msg += f" ({detail})"
    print(msg)

# --- Test: clone ---
resp = send_recv(bencode_dict({"op": "clone", "id": "t1"}))
m = re.search(r"11:new-session36:([a-f0-9-]{36})", resp)
if m:
    session = m.group(1)
    ok("clone returns session UUID")
else:
    fail("clone", resp)
    session = None

if not session:
    print("Cannot continue without a session")
    sys.exit(1)

# --- Test: describe ---
resp = send_recv(bencode_dict({"op": "describe", "id": "t2"}))
if "4:eval" in resp and "5:clone" in resp and "11:completions" in resp:
    ok("describe lists ops")
else:
    fail("describe", resp)

if "10:mino-nrepl" in resp:
    ok("describe includes version")
else:
    fail("describe version", resp)

# --- Test: eval simple expression ---
resp = send_recv(bencode_dict({"op": "eval", "id": "t3", "code": "(+ 1 2)", "session": session}))
if "5:value1:3" in resp:
    ok("eval (+ 1 2) returns 3")
else:
    fail("eval (+ 1 2)", resp)

if "2:ns4:user" in resp:
    ok("eval includes ns field")
else:
    fail("eval ns", resp)

# --- Test: eval with side-effect output ---
resp = send_recv(bencode_dict({"op": "eval", "id": "t4", "code": '(do (println "hello") 42)', "session": session}))
if "3:out" in resp and "hello" in resp:
    ok("eval captures println output")
else:
    fail("eval println capture", resp)

if "5:value2:42" in resp:
    ok("eval returns value after println")
else:
    fail("eval value after println", resp)

# --- Test: eval error ---
resp = send_recv(bencode_dict({"op": "eval", "id": "t5", "code": "(/ 1 0)", "session": session}))
if "5:error" in resp and "3:err" in resp:
    ok("eval error returns error status")
else:
    fail("eval error", resp)

# --- Test: eval def persistence ---
send_recv(bencode_dict({"op": "eval", "id": "t6a", "code": "(def x 42)", "session": session}))
resp = send_recv(bencode_dict({"op": "eval", "id": "t6b", "code": "x", "session": session}))
if "5:value2:42" in resp:
    ok("eval persists defs across calls")
else:
    fail("eval def persistence", resp)

# --- Test: completions ---
resp = send_recv(bencode_dict({"op": "completions", "id": "t7", "prefix": "ma", "session": session}))
if "9:candidate3:map" in resp:
    ok("completions returns map for prefix 'ma'")
else:
    fail("completions", resp)

# --- Test: ls-sessions ---
resp = send_recv(bencode_dict({"op": "ls-sessions", "id": "t8"}))
if session in resp:
    ok("ls-sessions includes our session")
else:
    fail("ls-sessions", resp)

# --- Test: unknown op ---
resp = send_recv(bencode_dict({"op": "bogus", "id": "t9"}))
if "10:unknown-op" in resp:
    ok("unknown op returns unknown-op status")
else:
    fail("unknown op", resp)

# --- Test: close ---
resp = send_recv(bencode_dict({"op": "close", "id": "t10", "session": session}))
if "4:done" in resp:
    ok("close returns done")
else:
    fail("close", resp)

# Verify session is gone.
resp = send_recv(bencode_dict({"op": "eval", "id": "t11", "code": "1", "session": session}))
if "5:error" in resp or "unknown session" in resp:
    ok("closed session is no longer accessible")
else:
    fail("closed session still accessible", resp)

# --- Summary ---
print(f"\n{passed + failed} tests, {passed} passed, {failed} failed")
sys.exit(1 if failed > 0 else 0)
PYEOF

TEST_EXIT=$?

# Check that server shuts down cleanly.
kill "$PID" 2>/dev/null
wait "$PID" 2>/dev/null || true
PID=""

if [ -f .nrepl-port ]; then
    echo "FAIL: .nrepl-port not cleaned up on shutdown"
    exit 1
else
    echo "  ok: .nrepl-port cleaned up on shutdown"
fi

exit $TEST_EXIT
