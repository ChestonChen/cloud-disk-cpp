#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build-prod/cloud-disk-prod"
PORT="${CLOUD_DISK_PROD_TEST_PORT:-$(python3 - <<'PY'
import socket

with socket.socket() as sock:
    sock.bind(("127.0.0.1", 0))
    print(sock.getsockname()[1])
PY
)}"
TMP_DIR="$(mktemp -d)"
SERVER_PID=""

cleanup() {
  if [[ -n "$SERVER_PID" ]] && kill -0 "$SERVER_PID" >/dev/null 2>&1; then
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" >/dev/null 2>&1 || true
  fi
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

extract_json_string() {
  python3 - "$1" "$2" <<'PY'
import json
import sys

doc = json.loads(sys.argv[1])
value = doc
for key in sys.argv[2].split("."):
    value = value[key]
print(value)
PY
}

if [[ ! -x "$BIN" ]]; then
  echo "cloud-disk-prod not found. Build it with:" >&2
  echo "cmake -S backend -B build-prod -DCLOUD_DISK_WITH_DROGON=ON && cmake --build build-prod" >&2
  exit 1
fi

CLOUD_DISK_DROGON_CONFIG="${CLOUD_DISK_DROGON_CONFIG:-$ROOT_DIR/backend/config/drogon.example.json}" \
CLOUD_DISK_STORAGE="$TMP_DIR/storage" \
CLOUD_DISK_WEB_ROOT="$ROOT_DIR/web" \
CLOUD_DISK_PORT="$PORT" \
"$BIN" >"$TMP_DIR/prod.log" 2>&1 &
SERVER_PID="$!"

for _ in {1..80}; do
  if curl -fsS "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

health="$(curl -fsS "http://127.0.0.1:$PORT/health")"
[[ "$health" == *'"stack":"drogon-mysql-redis"'* ]]

username="prod_$(date +%s)_$RANDOM"
password="secret123"
curl -fsS -X POST "http://127.0.0.1:$PORT/api/user/register" \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$username\",\"password\":\"$password\"}" >/dev/null

login="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/user/login" \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$username\",\"password\":\"$password\"}")"
token="$(extract_json_string "$login" "data.token")"

upload="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/files/upload?parent_id=0&name=prod.txt" \
  -H "Authorization: Bearer $token" \
  --data-binary 'prod cloud disk')"
file_id="$(extract_json_string "$upload" "data.id")"

download="$(curl -fsS "http://127.0.0.1:$PORT/api/files/download?id=$file_id" \
  -H "Authorization: Bearer $token")"
if [[ "$download" != "prod cloud disk" ]]; then
  echo "private download mismatch" >&2
  exit 1
fi

share="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/shares" \
  -H "Authorization: Bearer $token" \
  -H 'Content-Type: application/json' \
  -d "{\"file_id\":\"$file_id\",\"access_code\":\"1234\",\"allow_download\":\"true\"}")"
share_token="$(extract_json_string "$share" "data.token")"

public_download="$(curl -fsS "http://127.0.0.1:$PORT/api/public/download?token=$share_token&code=1234")"
if [[ "$public_download" != "prod cloud disk" ]]; then
  echo "public download mismatch" >&2
  exit 1
fi

echo "PROD SMOKE OK"
