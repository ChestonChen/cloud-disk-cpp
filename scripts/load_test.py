#!/usr/bin/env python3
import argparse
import concurrent.futures
import json
import os
import pathlib
import socket
import statistics
import subprocess
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


def build_backend() -> None:
    subprocess.run(["cmake", "-S", str(ROOT / "backend"), "-B", str(BUILD)], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", str(BUILD)], check=True, stdout=subprocess.DEVNULL)


class Client:
    def __init__(self, base: str, token: Optional[str] = None):
        self.base = base
        self.token = token

    def request(self, method: str, path: str, body: Optional[bytes] = None,
                content_type: str = "application/json") -> Tuple[int, bytes, float]:
        headers = {}
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        if body is not None:
            headers["Content-Type"] = content_type
        req = urllib.request.Request(self.base + path, data=body, method=method, headers=headers)
        start = time.perf_counter()
        try:
            with urllib.request.urlopen(req, timeout=20) as resp:
                raw = resp.read()
                return resp.status, raw, time.perf_counter() - start
        except urllib.error.HTTPError as exc:
            return exc.code, exc.read(), time.perf_counter() - start
        except Exception as exc:
            return 0, str(exc).encode(), time.perf_counter() - start

    def json_request(self, method: str, path: str, payload: Optional[dict] = None) -> dict:
        body = json.dumps(payload or {}).encode() if payload is not None else None
        status, raw, _ = self.request(method, path, body)
        doc = json.loads(raw.decode())
        if status >= 400 or doc.get("code") != 0:
            raise RuntimeError(f"{method} {path} failed: status={status}, body={doc}")
        return doc["data"]


def wait_health(base: str) -> None:
    client = Client(base)
    for _ in range(80):
        status, raw, _ = client.request("GET", "/health")
        if status == 200 and b"healthy" in raw:
            return
        time.sleep(0.1)
    raise RuntimeError("server did not become healthy")


def percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, int((len(ordered) - 1) * pct))
    return ordered[index]


def run_case(base: str, token: str, index: int, payload_size: int) -> dict:
    client = Client(base, token)
    payload = (f"payload-{index}-".encode() + b"x" * payload_size)
    result = {"ok": False, "latencies": [], "error": ""}

    status, raw, latency = client.request("POST", f"/api/files/upload?parent_id=0&name=load-{index}.txt",
                                          payload, "application/octet-stream")
    result["latencies"].append(latency)
    if status != 201:
        result["error"] = f"upload failed: {status} {raw[:120]!r}"
        return result
    file_id = json.loads(raw.decode())["data"]["id"]

    status, raw, latency = client.request("GET", f"/api/files/download?id={file_id}")
    result["latencies"].append(latency)
    if status != 200 or raw != payload:
        result["error"] = f"download mismatch: {status}"
        return result

    status, raw, latency = client.request("GET", "/api/files?parent_id=0")
    result["latencies"].append(latency)
    if status != 200 or f"load-{index}.txt".encode() not in raw:
        result["error"] = f"list failed: {status}"
        return result

    result["ok"] = True
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Cloud Disk local load test")
    parser.add_argument("--requests", type=int, default=100, help="number of upload/download/list flows")
    parser.add_argument("--concurrency", type=int, default=10, help="parallel workers")
    parser.add_argument("--payload-size", type=int, default=1024, help="bytes appended to each uploaded file")
    parser.add_argument("--port", type=int, default=int(os.environ.get("CLOUD_DISK_LOAD_PORT") or pick_free_port()))
    args = parser.parse_args()

    build_backend()
    base = f"http://127.0.0.1:{args.port}"

    with tempfile.TemporaryDirectory() as tmp:
        env = os.environ.copy()
        env["CLOUD_DISK_STORAGE"] = str(pathlib.Path(tmp) / "storage")
        env["CLOUD_DISK_PORT"] = str(args.port)
        proc = subprocess.Popen([str(BIN)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            wait_health(base)
            client = Client(base)
            username = f"load_{int(time.time() * 1000)}"
            password = "secret123"
            client.json_request("POST", "/api/user/register", {"username": username, "password": password})
            token = client.json_request("POST", "/api/user/login", {"username": username, "password": password})["token"]

            start = time.perf_counter()
            results = []
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
                futures = [
                    pool.submit(run_case, base, token, i, args.payload_size)
                    for i in range(args.requests)
                ]
                for future in concurrent.futures.as_completed(futures):
                    results.append(future.result())
            elapsed = time.perf_counter() - start

            ok = sum(1 for item in results if item["ok"])
            failed = len(results) - ok
            latencies = [latency for item in results for latency in item["latencies"]]
            total_http_requests = len(latencies)
            success_rate = ok / len(results) * 100 if results else 0
            rps = total_http_requests / elapsed if elapsed > 0 else 0

            print("LOAD TEST RESULT")
            print(f"flows={len(results)} success={ok} failed={failed} success_rate={success_rate:.2f}%")
            print(f"http_requests={total_http_requests} elapsed_sec={elapsed:.3f} rps={rps:.2f}")
            print(f"latency_ms_avg={statistics.mean(latencies) * 1000:.2f}")
            print(f"latency_ms_p50={percentile(latencies, 0.50) * 1000:.2f}")
            print(f"latency_ms_p95={percentile(latencies, 0.95) * 1000:.2f}")
            print(f"latency_ms_p99={percentile(latencies, 0.99) * 1000:.2f}")
            if failed:
                first_error = next((item["error"] for item in results if not item["ok"]), "")
                print(f"first_error={first_error}")
                return 1
            return 0
        finally:
            proc.terminate()
            proc.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())

