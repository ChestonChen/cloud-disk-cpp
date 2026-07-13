#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BIN="$BUILD_DIR/cloud-disk"
PORT="${CLOUD_DISK_TEST_PORT:-$(python3 - <<'PY'
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

assert_contains() {
  local value="$1"
  local needle="$2"
  local message="$3"
  if [[ "$value" != *"$needle"* ]]; then
    echo "ASSERTION FAILED: $message" >&2
    echo "Expected to find: $needle" >&2
    echo "Actual: $value" >&2
    exit 1
  fi
}

extract_json_string() {
  python3 - "$1" "$2" <<'PY'
import json
import sys

doc = json.loads(sys.argv[1])
path = sys.argv[2].split(".")
value = doc
for key in path:
    value = value[key]
print(value)
PY
}

content_hash() {
  python3 - "$1" <<'PY'
import sys

content = sys.argv[1].encode()
mask = (1 << 64) - 1
h = 1469598103934665603
for ch in content:
    h ^= ch
    h = (h * 1099511628211) & mask

parts = []
for i in range(4):
    mixed = (h + 0x9e3779b97f4a7c15 * (i + 1)) & mask
    mixed ^= mixed >> 30
    mixed = (mixed * 0xbf58476d1ce4e5b9) & mask
    mixed ^= mixed >> 27
    mixed = (mixed * 0x94d049bb133111eb) & mask
    mixed ^= mixed >> 31
    parts.append(f"{mixed:016x}")
print("".join(parts))
PY
}

cmake -S "$ROOT_DIR/backend" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" >/dev/null

CLOUD_DISK_STORAGE="$TMP_DIR/storage" CLOUD_DISK_PORT="$PORT" "$BIN" >"$TMP_DIR/server.log" 2>&1 &
SERVER_PID="$!"

for _ in {1..50}; do
  if curl -fsS "http://127.0.0.1:$PORT/health" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

health="$(curl -fsS "http://127.0.0.1:$PORT/health")"
assert_contains "$health" '"status":"healthy"' "health endpoint should be healthy"

home="$(curl -fsS "http://127.0.0.1:$PORT/")"
assert_contains "$home" '<title>Cloud Disk 本地网盘</title>' "home page should serve the web UI"
app_js="$(curl -fsS "http://127.0.0.1:$PORT/app.js")"
assert_contains "$app_js" 'function uploadSelectedFile' "app script should be served"

username="alice_$RANDOM"
password="secret123"

register="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/user/register" \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$username\",\"password\":\"$password\"}")"
assert_contains "$register" "\"username\":\"$username\"" "register should return created user"

login="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/user/login" \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$username\",\"password\":\"$password\"}")"
token="$(extract_json_string "$login" "data.token")"
if [[ -z "$token" ]]; then
  echo "ASSERTION FAILED: login should return token" >&2
  exit 1
fi

folder="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/folders" \
  -H "Authorization: Bearer $token" \
  -H 'Content-Type: application/json' \
  -d '{"parent_id":"0","name":"docs"}')"
assert_contains "$folder" '"name":"docs"' "folder should be created"
folder_id="$(extract_json_string "$folder" "data.id")"

upload="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/files/upload?parent_id=0&name=hello.txt" \
  -H "Authorization: Bearer $token" \
  --data-binary 'hello cloud disk')"
assert_contains "$upload" '"name":"hello.txt"' "upload should create a file"
assert_contains "$upload" '"sha256":"' "upload should return content hash"
file_id="$(extract_json_string "$upload" "data.id")"
file_sha="$(extract_json_string "$upload" "data.sha256")"

instant="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/files/instant" \
  -H "Authorization: Bearer $token" \
  -H 'Content-Type: application/json' \
  -d "{\"parent_id\":\"0\",\"name\":\"hello-copy.txt\",\"sha256\":\"$file_sha\",\"size_bytes\":\"16\"}")"
assert_contains "$instant" '"name":"hello-copy.txt"' "instant upload should create logical copy"
assert_contains "$instant" '"ref_count":"2"' "instant upload should increase object ref count"
copy_id="$(extract_json_string "$instant" "data.id")"

nested_upload="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/files/upload?parent_id=$folder_id&name=nested.txt" \
  -H "Authorization: Bearer $token" \
  --data-binary 'nested content')"
assert_contains "$nested_upload" '"name":"nested.txt"' "nested upload should create a file"

profile="$(curl -fsS "http://127.0.0.1:$PORT/api/user/me" -H "Authorization: Bearer $token")"
assert_contains "$profile" '"storage_used":"46"' "profile should track uploaded bytes"

share="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/shares" \
  -H "Authorization: Bearer $token" \
  -H 'Content-Type: application/json' \
  -d "{\"file_id\":\"$copy_id\",\"access_code\":\"1234\",\"allow_download\":\"true\"}")"
assert_contains "$share" '"token":"' "share should return token"
assert_contains "$share" "/share?token=" "share should return user-facing share page URL"
share_token="$(extract_json_string "$share" "data.token")"

share_page="$(curl -fsS "http://127.0.0.1:$PORT/share?token=$share_token&code=1234")"
assert_contains "$share_page" 'Cloud Disk 文件分享' "share page should be readable in a browser"
assert_contains "$share_page" '下载文件' "share page should expose a download action"
public_meta="$(curl -fsS "http://127.0.0.1:$PORT/api/public/share?token=$share_token&code=1234")"
assert_contains "$public_meta" '"name":"hello-copy.txt"' "public share metadata should expose file name"
public_download="$(curl -fsS "http://127.0.0.1:$PORT/api/public/download?token=$share_token&code=1234")"
if [[ "$public_download" != "hello cloud disk" ]]; then
  echo "ASSERTION FAILED: public share download should match content" >&2
  exit 1
fi

bob="bob_$RANDOM"
curl -fsS -X POST "http://127.0.0.1:$PORT/api/user/register" \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$bob\",\"password\":\"$password\"}" >/dev/null
bob_login="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/user/login" \
  -H 'Content-Type: application/json' \
  -d "{\"username\":\"$bob\",\"password\":\"$password\"}")"
bob_token="$(extract_json_string "$bob_login" "data.token")"
bob_private_status="$(curl -sS -o "$TMP_DIR/bob-private-download" -w '%{http_code}' \
  "http://127.0.0.1:$PORT/api/files/download?id=$copy_id" \
  -H "Authorization: Bearer $bob_token")"
if [[ "$bob_private_status" != "404" ]]; then
  echo "ASSERTION FAILED: another user should not download private files by id" >&2
  echo "Status: $bob_private_status" >&2
  exit 1
fi

curl -fsS -X DELETE "http://127.0.0.1:$PORT/api/files?id=$copy_id" -H "Authorization: Bearer $token" >/dev/null
recycle="$(curl -fsS "http://127.0.0.1:$PORT/api/recycle" -H "Authorization: Bearer $token")"
assert_contains "$recycle" '"name":"hello-copy.txt"' "deleted file should appear in recycle bin"
restore="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/recycle/restore?id=$copy_id" -H "Authorization: Bearer $token")"
assert_contains "$restore" '"restored":"true"' "restore should mark file active again"
curl -fsS -X DELETE "http://127.0.0.1:$PORT/api/files?id=$copy_id" -H "Authorization: Bearer $token" >/dev/null
permanent="$(curl -fsS -X DELETE "http://127.0.0.1:$PORT/api/recycle/permanent?id=$copy_id" -H "Authorization: Bearer $token")"
assert_contains "$permanent" '"permanently_deleted":"true"' "permanent delete should remove recycled file"

chunk_content='chunk-onechunk-two'
chunk_hash="$(content_hash "$chunk_content")"
upload_init="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/uploads/init" \
  -H "Authorization: Bearer $token" \
  -H 'Content-Type: application/json' \
  -d "{\"parent_id\":\"0\",\"name\":\"chunked.txt\",\"sha256\":\"$chunk_hash\",\"size_bytes\":\"18\",\"chunk_size\":\"9\",\"total_chunks\":\"2\"}")"
upload_id="$(extract_json_string "$upload_init" "data.upload_id")"
curl -fsS -X POST "http://127.0.0.1:$PORT/api/uploads/chunk?upload_id=$upload_id&chunk_index=0" \
  -H "Authorization: Bearer $token" \
  --data-binary 'chunk-one' >/dev/null
progress="$(curl -fsS "http://127.0.0.1:$PORT/api/uploads/progress?upload_id=$upload_id" -H "Authorization: Bearer $token")"
assert_contains "$progress" '"chunk_index":"0"' "upload progress should include first chunk"
curl -fsS -X POST "http://127.0.0.1:$PORT/api/uploads/chunk?upload_id=$upload_id&chunk_index=1" \
  -H "Authorization: Bearer $token" \
  --data-binary 'chunk-two' >/dev/null
complete="$(curl -fsS -X POST "http://127.0.0.1:$PORT/api/uploads/complete?upload_id=$upload_id" -H "Authorization: Bearer $token")"
assert_contains "$complete" '"name":"chunked.txt"' "complete should create chunked file"
chunked_id="$(extract_json_string "$complete" "data.id")"
chunked_download="$(curl -fsS "http://127.0.0.1:$PORT/api/files/download?id=$chunked_id" -H "Authorization: Bearer $token")"
if [[ "$chunked_download" != "$chunk_content" ]]; then
  echo "ASSERTION FAILED: chunked download should match merged content" >&2
  exit 1
fi

list="$(curl -fsS "http://127.0.0.1:$PORT/api/files?parent_id=0" -H "Authorization: Bearer $token")"
assert_contains "$list" '"name":"docs"' "list should contain folder"
assert_contains "$list" '"name":"hello.txt"' "list should contain uploaded file"
assert_contains "$list" '"name":"chunked.txt"' "list should contain chunked uploaded file"

download="$(curl -fsS "http://127.0.0.1:$PORT/api/files/download?id=$file_id" -H "Authorization: Bearer $token")"
if [[ "$download" != "hello cloud disk" ]]; then
  echo "ASSERTION FAILED: downloaded content should match upload" >&2
  echo "Actual: $download" >&2
  exit 1
fi

delete_response="$(curl -fsS -X DELETE "http://127.0.0.1:$PORT/api/files?id=$file_id" -H "Authorization: Bearer $token")"
assert_contains "$delete_response" '"deleted":"true"' "delete should mark file deleted"

list_after_delete="$(curl -fsS "http://127.0.0.1:$PORT/api/files?parent_id=0" -H "Authorization: Bearer $token")"
if [[ "$list_after_delete" == *'"name":"hello.txt"'* ]]; then
  echo "ASSERTION FAILED: deleted file should be hidden from list" >&2
  echo "Actual: $list_after_delete" >&2
  exit 1
fi

curl -fsS -X DELETE "http://127.0.0.1:$PORT/api/files?id=$folder_id" -H "Authorization: Bearer $token" >/dev/null
list_deleted_folder="$(curl -fsS "http://127.0.0.1:$PORT/api/files?parent_id=$folder_id" -H "Authorization: Bearer $token")"
if [[ "$list_deleted_folder" == *'"name":"nested.txt"'* ]]; then
  echo "ASSERTION FAILED: deleting folder should hide nested files" >&2
  echo "Actual: $list_deleted_folder" >&2
  exit 1
fi
recycle_after_folder_delete="$(curl -fsS "http://127.0.0.1:$PORT/api/recycle" -H "Authorization: Bearer $token")"
assert_contains "$recycle_after_folder_delete" '"name":"docs"' "deleted folder should appear in recycle bin"
assert_contains "$recycle_after_folder_delete" '"name":"nested.txt"' "deleted child file should appear in recycle bin"
curl -fsS -X DELETE "http://127.0.0.1:$PORT/api/recycle/permanent?id=$folder_id" -H "Authorization: Bearer $token" >/dev/null
recycle_after_folder_permanent="$(curl -fsS "http://127.0.0.1:$PORT/api/recycle" -H "Authorization: Bearer $token")"
if [[ "$recycle_after_folder_permanent" == *'"name":"nested.txt"'* ]]; then
  echo "ASSERTION FAILED: permanently deleting a folder should also remove deleted children" >&2
  echo "Actual: $recycle_after_folder_permanent" >&2
  exit 1
fi

echo "HARNESS OK"

