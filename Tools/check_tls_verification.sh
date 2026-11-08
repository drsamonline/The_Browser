#!/usr/bin/env bash
# Verifies that TLS certificate validation is actually enforced: an untrusted
# certificate is refused, a trusted one is accepted, and a trusted certificate
# for the wrong host is still refused.
#
# Needs: openssl, python3, and a built `aetheris` binary.
#   ./Tools/check_tls_verification.sh build-standalone/bin/aetheris
#
# Exits 0 only when all three expectations hold.

set -euo pipefail

BINARY="${1:-build-standalone/bin/aetheris}"
if [[ ! -x "$BINARY" ]]; then
    echo "usage: $0 <path-to-aetheris>" >&2
    exit 2
fi
BINARY="$(cd "$(dirname "$BINARY")" && pwd)/$(basename "$BINARY")"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

make_cert() { # name san
    openssl req -x509 -newkey rsa:2048 -keyout "$1.key" -out "$1.crt" -days 1 -nodes \
        -subj "/CN=$2" -addext "subjectAltName=$3" >/dev/null 2>&1
}

make_cert good "127.0.0.1" "IP:127.0.0.1"
make_cert wrong "example.test" "DNS:example.test"

python3 - "$BINARY" <<'PY'
import http.server, os, socketserver, ssl, subprocess, sys, threading

binary = sys.argv[1]
failures = []

def serve(cert, key):
    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            body = b"<html><body>ok</body></html>"
            self.send_response(200)
            self.send_header("Content-Type", "text/html")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        def log_message(self, *args):
            pass
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server = socketserver.TCPServer(("127.0.0.1", 0), Handler)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, server.server_address[1]

def fetch(port, ca_file=None):
    env = dict(os.environ)
    if ca_file:
        env["AETHERIS_CA_FILE"] = os.path.abspath(ca_file)
    result = subprocess.run([binary, "--fetch", f"https://127.0.0.1:{port}/"],
                            capture_output=True, text=True, timeout=30, env=env)
    return result.returncode, (result.stderr or result.stdout).strip()

server, port = serve("good.crt", "good.key")
code, message = fetch(port)
if code == 0:
    failures.append("an untrusted self-signed certificate was accepted")
else:
    print(f"ok: untrusted certificate refused ({message[:60]})")

code, message = fetch(port, "good.crt")
if code != 0:
    failures.append(f"a certificate trusted via AETHERIS_CA_FILE was refused ({message[:60]})")
else:
    print("ok: trusted certificate accepted")
server.shutdown()

server, port = serve("wrong.crt", "wrong.key")
code, message = fetch(port, "wrong.crt")
if code == 0:
    failures.append("a trusted certificate for the wrong host was accepted")
else:
    print(f"ok: hostname mismatch refused ({message[:60]})")
server.shutdown()

if failures:
    for failure in failures:
        print(f"FAILED: {failure}", file=sys.stderr)
    sys.exit(1)
print("TLS verification checks passed")
PY
