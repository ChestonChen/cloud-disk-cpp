#!/usr/bin/env python3
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import time
from typing import Optional, Tuple
import urllib.error
import urllib.request


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
BIN = BUILD / ("cloud-disk.exe" if os.name == "nt" else "cloud-disk")


def pick_free_port() -> int:
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


PORT = int(os.environ.get("CLOUD_DISK_TEST_PORT") or pick_free_port())
BASE = f"http://127.0.0.1:{PORT}"


def content_hash(text: str) -> str:
    mask = (1 << 64) - 1
    h = 1469598103934665603
    for ch in text.encode():
        h ^= ch
        h = (h * 1099511628211) & mask
    parts = []
    for i in range(4):
        mixed = (h + 0x9E3779B97F4A7C15 * (i + 1)) & mask
        mixed ^= mixed >> 30
        mixed = (mixed * 0xBF58476D1CE4E5B9) & mask
        mixed ^= mixed >> 27
        mixed = (mixed * 0x94D049BB133111EB) & mask
        mixed ^= mixed >> 31
        parts.append(f"{mixed:016x}")
    return "".join(parts)


def build_backend() -> None:
    subprocess.run(["cmake", "-S", str(ROOT / "backend"), "-B", str(BUILD)], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(BUILD)], check=True, stdout=subprocess.DEVNULL)


def request(method: str, path: str, body: Optional[bytes] = None, token: Optional[str] = None,
            content_type: str = "application/json") -> Tuple[int, bytes]:
    headers = {}
    if token:
        headers["Authorization"] = f"Bearer {token}"
    if body is not None:
        headers["Content-Type"] = content_type
    req = urllib.request.Request(BASE + path, data=body, method=method, headers=headers)
    try:
        with urllib.request.urlopen(req, timeout=10) as resp:
            return resp.status, resp.read()
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read()


def json_request(method: str, path: str, payload: Optional[dict] = None,
                 token: Optional[str] = None) -> dict:
    body = json.dumps(payload or {}).encode() if payload is not None else None
    status, raw = request(method, path, body, token)
    doc = json.loads(raw.decode())
    if status >= 400 or doc.get("code") != 0:
        raise AssertionError(f"{method} {path} failed: status={status}, body={doc}")
    return doc["data"]


def wait_health() -> None:
    for _ in range(80):
        try:
            status, raw = request("GET", "/health")
            if status == 200 and b"healthy" in raw:
                return
        except Exception:
            pass
        time.sleep(0.1)
    raise RuntimeError("server did not become healthy")


def main() -> int:
    build_backend()
    with tempfile.TemporaryDirectory() as tmp:
        env = os.environ.copy()
        env["CLOUD_DISK_STORAGE"] = str(pathlib.Path(tmp) / "storage")
        env["CLOUD_DISK_PORT"] = str(PORT)
        proc = subprocess.Popen([str(BIN)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_health()
            username = f"tester_{int(time.time() * 1000)}"
            password = "secret123"
            json_request("POST", "/api/user/register", {"username": username, "password": password})
            token = json_request("POST", "/api/user/login", {"username": username, "password": password})["token"]

            folder = json_request("POST", "/api/folders", {"parent_id": "0", "name": "docs"}, token)
            folder_id = folder["id"]
            nested = json.loads(request("POST", f"/api/files/upload?parent_id={folder_id}&name=nested.txt",
                                        b"nested content", token, "application/octet-stream")[1].decode())["data"]

            status, raw = request("POST", "/api/files/upload?parent_id=0&name=hello.txt",
                                  b"hello cloud disk", token, "application/octet-stream")
            upload = json.loads(raw.decode())["data"]
            assert status == 201 and upload["sha256"], "直接上传应返回内容哈希"

            instant = json_request("POST", "/api/files/instant", {
                "parent_id": "0",
                "name": "hello-copy.txt",
                "sha256": upload["sha256"],
                "size_bytes": "16",
            }, token)
            assert instant["ref_count"] == "2", "秒传应增加对象引用计数"

            share = json_request("POST", "/api/shares", {
                "file_id": instant["id"],
                "access_code": "1234",
                "allow_download": "true",
            }, token)
            assert "/share?token=" in share["url"], "分享应返回可直接打开的页面链接"
            status, raw = request("GET", f"/share?token={share['token']}&code=1234")
            assert status == 200 and "下载文件" in raw.decode(), "分享页应展示下载入口"
            public_meta = json_request("GET", f"/api/public/share?token={share['token']}&code=1234")
            assert public_meta["name"] == "hello-copy.txt", "分享元数据应返回文件名"
            status, raw = request("GET", f"/api/public/download?token={share['token']}&code=1234")
            assert status == 200 and raw == b"hello cloud disk", "分享下载内容应正确"

            other = f"other_{int(time.time() * 1000)}"
            json_request("POST", "/api/user/register", {"username": other, "password": password})
            other_token = json_request("POST", "/api/user/login", {"username": other, "password": password})["token"]
            status, _ = request("GET", f"/api/files/download?id={instant['id']}", token=other_token)
            assert status == 404, "其他账号不能通过私有文件 id 下载"

            chunk_content = "chunk-onechunk-two"
            session = json_request("POST", "/api/uploads/init", {
                "parent_id": "0",
                "name": "chunked.txt",
                "sha256": content_hash(chunk_content),
                "size_bytes": str(len(chunk_content)),
                "chunk_size": "9",
                "total_chunks": "2",
            }, token)
            upload_id = session["upload_id"]
            request("POST", f"/api/uploads/chunk?upload_id={upload_id}&chunk_index=0",
                    b"chunk-one", token, "application/octet-stream")
            progress = json_request("GET", f"/api/uploads/progress?upload_id={upload_id}", token=token)
            assert progress[0]["chunk_index"] == "0", "进度应包含已上传分片"
            request("POST", f"/api/uploads/chunk?upload_id={upload_id}&chunk_index=1",
                    b"chunk-two", token, "application/octet-stream")
            completed = json_request("POST", f"/api/uploads/complete?upload_id={upload_id}", token=token)
            status, raw = request("GET", f"/api/files/download?id={completed['id']}", token=token)
            assert status == 200 and raw.decode() == chunk_content, "分片合并下载内容应正确"

            request("DELETE", f"/api/files?id={instant['id']}", token=token)
            recycle = json_request("GET", "/api/recycle", token=token)
            assert any(item["name"] == "hello-copy.txt" for item in recycle), "回收站应包含删除文件"
            restored = json_request("POST", f"/api/recycle/restore?id={instant['id']}", token=token)
            assert restored["restored"] == "true", "恢复应成功"
            request("DELETE", f"/api/files?id={instant['id']}", token=token)
            permanent = json_request("DELETE", f"/api/recycle/permanent?id={instant['id']}", token=token)
            assert permanent["permanently_deleted"] == "true", "永久删除应成功"

            request("DELETE", f"/api/files?id={folder_id}", token=token)
            recycle = json_request("GET", "/api/recycle", token=token)
            assert any(item["id"] == nested["id"] for item in recycle), "删除文件夹应把子文件放入回收站"
            json_request("DELETE", f"/api/recycle/permanent?id={folder_id}", token=token)
            recycle = json_request("GET", "/api/recycle", token=token)
            assert not any(item["id"] == nested["id"] for item in recycle), "永久删除文件夹应移除子文件"
            print("FUNCTIONAL TEST OK")
            return 0
        finally:
            proc.terminate()
            proc.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())

