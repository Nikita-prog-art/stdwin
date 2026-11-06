#!/bin/sh

# End-to-end TLS check for the Windows build: starts http_serve_ok.exe serving
# HTTPS on loopback and requests it with curl. Requires a build made with
# ./dev/openssl-win.sh + ./dev/win.sh.

set -xue

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="${TLS_SMOKE_BIN:-$ROOT/.build-win/tst/http_serve_ok.exe}"
PORT="${TLS_SMOKE_PORT:-18443}"

if [ ! -e "$BIN" ]; then
    echo "tls: $BIN not found; run ./dev/openssl-win.sh and ./dev/win.sh first" >&2
    exit 1
fi

if command -v curl.exe > /dev/null 2>&1; then
    CURL=curl.exe
else
    CURL=curl
fi

OUT=$(mktemp)
ERR=$(mktemp)
SERVER=
trap 'kill "${SERVER}" 2>/dev/null || true; rm -f "$OUT" "$ERR"' EXIT

"$BIN" --port="$PORT" &
SERVER=$!

OK=
i=0
while [ "$i" -lt 60 ]; do
    if "$CURL" -k -s -S -D - --max-time 5 "https://127.0.0.1:$PORT/" > "$OUT" 2> "$ERR"; then
        OK=1
        break
    fi

    i=$((i + 1))
    sleep 0.5
done

if [ -z "$OK" ]; then
    cat "$ERR" >&2
    echo "tls: no HTTPS response from 127.0.0.1:$PORT" >&2
    exit 1
fi

cat "$OUT"

if ! grep -q '200 OK' "$OUT"; then
    echo "tls: unexpected response status" >&2
    exit 1
fi

if ! grep -q 'Payload:' "$OUT"; then
    echo "tls: response has no Payload header" >&2
    exit 1
fi

echo "tls: ok" >&2
