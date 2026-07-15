#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT_DIR/build/cloud-disk"
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

content_hash() {
  python3 - "$1" <<'PY'
import sys

content = sys.argv[1].encode()
mask = (1 << 64) - 1
hash_val = 1469598103934665603
for byte in content:
    hash_val ^= byte
    hash_val = (hash_val * 1099511628211) & mask

parts = []
for i in range(4):
    mixed = (hash_val + 0x9E3779B97F4A7C15 * (i + 1)) & mask
    mixed ^= mixed >> 30
    mixed = (mixed * 0xBF58476D1CE4E5B9) & mask
    mixed ^= mixed >> 27
    mixed = (mixed * 0x94D049BB133111EB) & mask
    mixed ^= mixed >> 31
    parts.append(f"{mixed:016x}")
print("".join(parts))
PY
}

auth_curl() {
  local method="$1"
  local path="$2"
  shift 2
  curl -fsS -X "$method" "http://127.0.0.1:$PORT$path" \
    -H "Authorization: Bearer $token" \
    "$@"
}

if [[ ! -x "$BIN" ]]; then
  echo "cloud-disk not found. Build it with:" >&2
  echo "cmake -S backend -B build && cmake --build build" >&2
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

upload="$(auth_curl POST "/api/files/upload?parent_id=0&name=prod.txt" --data-binary 'prod cloud disk')"
file_id="$(extract_json_string "$upload" "data.id")"

download="$(auth_curl GET "/api/files/download?id=$file_id")"
if [[ "$download" != "prod cloud disk" ]]; then
  echo "private download mismatch" >&2
  exit 1
fi

share="$(auth_curl POST "/api/shares" -H 'Content-Type: application/json' \
  -d "{\"file_id\":\"$file_id\",\"access_code\":\"1234\",\"allow_download\":\"true\"}")"
share_token="$(extract_json_string "$share" "data.token")"

public_download="$(curl -fsS "http://127.0.0.1:$PORT/api/public/download?token=$share_token&code=1234")"
if [[ "$public_download" != "prod cloud disk" ]]; then
  echo "public download mismatch" >&2
  exit 1
fi

# recycle: soft delete -> list -> restore -> soft delete -> permanent
auth_curl DELETE "/api/files?id=$file_id" >/dev/null
recycle="$(auth_curl GET "/api/recycle")"
[[ "$recycle" == *'"id":"'"$file_id"'"'* ]] || [[ "$recycle" == *"\"id\":\"$file_id\""* ]]

auth_curl POST "/api/recycle/restore?id=$file_id" >/dev/null
files="$(auth_curl GET "/api/files?parent_id=0")"
[[ "$files" == *"$file_id"* ]]

auth_curl DELETE "/api/files?id=$file_id" >/dev/null
auth_curl DELETE "/api/recycle/permanent?id=$file_id" >/dev/null
recycle_empty="$(auth_curl GET "/api/recycle")"
[[ "$recycle_empty" == *'"data":[]'* ]] || [[ "$recycle_empty" == *'"data": []'* ]]

# instant upload after a fresh object exists (unique payload avoids cross-run hash collisions)
payload="seed-body-$username-$RANDOM"
sha="$(content_hash "$payload")"
seed="$(auth_curl POST "/api/files/upload?parent_id=0&name=seed.bin" --data-binary "$payload")"
seed_id="$(extract_json_string "$seed" "data.id")"

init_instant="$(auth_curl POST "/api/uploads/init" -H 'Content-Type: application/json' \
  -d "{\"parent_id\":\"0\",\"name\":\"instant.bin\",\"sha256\":\"$sha\",\"size_bytes\":\"${#payload}\",\"chunk_size\":\"4\",\"total_chunks\":\"1\"}")"
status="$(extract_json_string "$init_instant" "data.status")"
[[ "$status" == "instant_available" ]]

instant="$(auth_curl POST "/api/files/instant" -H 'Content-Type: application/json' \
  -d "{\"parent_id\":\"0\",\"name\":\"instant.bin\",\"sha256\":\"$sha\",\"size_bytes\":\"${#payload}\"}")"
instant_id="$(extract_json_string "$instant" "data.id")"
instant_dl="$(auth_curl GET "/api/files/download?id=$instant_id")"
[[ "$instant_dl" == "$payload" ]]

# chunked upload for brand-new content
chunk_payload="ABCDEFGHIJKLMNOP-$username-$RANDOM"
# pad/trim to multiple of 4 for simple chunking in the test below
chunk_payload="$(printf '%s' "$chunk_payload" | python3 -c 'import sys; s=sys.stdin.read(); print(s[:((len(s)//4)*4)] if len(s)>=16 else (s+"XXXX")[:16])')"
chunk_sha="$(content_hash "$chunk_payload")"
chunk_len=${#chunk_payload}
total_chunks=$((chunk_len / 4))
init="$(auth_curl POST "/api/uploads/init" -H 'Content-Type: application/json' \
  -d "{\"parent_id\":\"0\",\"name\":\"chunked.bin\",\"sha256\":\"$chunk_sha\",\"size_bytes\":\"$chunk_len\",\"chunk_size\":\"4\",\"total_chunks\":\"$total_chunks\"}")"
[[ "$(extract_json_string "$init" "data.status")" == "uploading" ]]
upload_id="$(extract_json_string "$init" "data.upload_id")"

python3 - "$chunk_payload" "$PORT" "$token" "$upload_id" "$total_chunks" <<'PY'
import sys, urllib.request
payload, port, token, upload_id, total = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], int(sys.argv[5])
for i in range(total):
    body = payload[i * 4:(i + 1) * 4].encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/uploads/chunk?upload_id={upload_id}&chunk_index={i}",
        data=body,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/octet-stream"},
        method="POST",
    )
    with urllib.request.urlopen(req) as resp:
        resp.read()
PY

completed="$(auth_curl POST "/api/uploads/complete?upload_id=$upload_id")"
chunk_id="$(extract_json_string "$completed" "data.id")"
chunk_dl="$(auth_curl GET "/api/files/download?id=$chunk_id")"
[[ "$chunk_dl" == "$chunk_payload" ]]

# upload resume: send first half, re-init, finish remaining
resume_payload="RESUME1234567890-$username-$RANDOM"
resume_payload="$(printf '%s' "$resume_payload" | python3 -c 'import sys; s=sys.stdin.read(); print((s+"XXXXXXXX")[:16])')"
resume_sha="$(content_hash "$resume_payload")"
resume_init="$(auth_curl POST "/api/uploads/init" -H 'Content-Type: application/json' \
  -d "{\"parent_id\":\"0\",\"name\":\"resume.bin\",\"sha256\":\"$resume_sha\",\"size_bytes\":\"16\",\"chunk_size\":\"4\",\"total_chunks\":\"4\"}")"
resume_id="$(extract_json_string "$resume_init" "data.upload_id")"
python3 - "$resume_payload" "$PORT" "$token" "$resume_id" <<'PY'
import sys, urllib.request
payload, port, token, upload_id = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
for i in range(2):  # only first 2 chunks
    body = payload[i * 4:(i + 1) * 4].encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/uploads/chunk?upload_id={upload_id}&chunk_index={i}",
        data=body,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/octet-stream"},
        method="POST",
    )
    with urllib.request.urlopen(req) as resp:
        resp.read()
PY
resume_status="$(auth_curl GET "/api/uploads/status?upload_id=$resume_id")"
[[ "$resume_status" == *'"uploaded_chunks":[0,1]'* ]] || [[ "$resume_status" == *'"uploaded_chunks":[0, 1]'* ]]

resume_again="$(auth_curl POST "/api/uploads/init" -H 'Content-Type: application/json' \
  -d "{\"parent_id\":\"0\",\"name\":\"resume.bin\",\"sha256\":\"$resume_sha\",\"size_bytes\":\"16\",\"chunk_size\":\"4\",\"total_chunks\":\"4\"}")"
[[ "$(extract_json_string "$resume_again" "data.upload_id")" == "$resume_id" ]]
python3 - "$resume_payload" "$PORT" "$token" "$resume_id" <<'PY'
import sys, urllib.request
payload, port, token, upload_id = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
for i in (2, 3):
    body = payload[i * 4:(i + 1) * 4].encode()
    req = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/uploads/chunk?upload_id={upload_id}&chunk_index={i}",
        data=body,
        headers={"Authorization": f"Bearer {token}", "Content-Type": "application/octet-stream"},
        method="POST",
    )
    with urllib.request.urlopen(req) as resp:
        resp.read()
PY
resume_done="$(auth_curl POST "/api/uploads/complete?upload_id=$resume_id")"
resume_file_id="$(extract_json_string "$resume_done" "data.id")"
[[ "$(auth_curl GET "/api/files/download?id=$resume_file_id")" == "$resume_payload" ]]

# download resume via HTTP Range
range_code="$(curl -sS -o "$TMP_DIR/range.bin" -w '%{http_code}' \
  -H "Authorization: Bearer $token" -H 'Range: bytes=0-5' \
  "http://127.0.0.1:$PORT/api/files/download?id=$resume_file_id")"
[[ "$range_code" == "206" ]]
[[ "$(cat "$TMP_DIR/range.bin")" == "${resume_payload:0:6}" ]]

# cleanup seeded files
auth_curl DELETE "/api/files?id=$seed_id" >/dev/null
auth_curl DELETE "/api/recycle/permanent?id=$seed_id" >/dev/null
auth_curl DELETE "/api/files?id=$instant_id" >/dev/null
auth_curl DELETE "/api/recycle/permanent?id=$instant_id" >/dev/null
auth_curl DELETE "/api/files?id=$chunk_id" >/dev/null
auth_curl DELETE "/api/recycle/permanent?id=$chunk_id" >/dev/null
auth_curl DELETE "/api/files?id=$resume_file_id" >/dev/null
auth_curl DELETE "/api/recycle/permanent?id=$resume_file_id" >/dev/null

echo "PROD SMOKE OK"
