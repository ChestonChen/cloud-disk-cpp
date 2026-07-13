#!/usr/bin/env python3
import pathlib
import shutil
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD_DIR = ROOT / "build"
BINARY_DIR = ROOT / "src-tauri" / "binaries"


def host_triple() -> str:
    output = subprocess.check_output(["rustc", "-vV"], text=True)
    for line in output.splitlines():
        if line.startswith("host:"):
            return line.split(":", 1)[1].strip()
    raise RuntimeError("failed to detect rust host triple")


def main() -> int:
    subprocess.run(["cmake", "-S", str(ROOT / "backend"), "-B", str(BUILD_DIR)], check=True)
    subprocess.run(["cmake", "--build", str(BUILD_DIR)], check=True)

    source = BUILD_DIR / "cloud-disk"
    if not source.exists():
        source = BUILD_DIR / "cloud-disk.exe"
    if not source.exists():
        raise RuntimeError("cloud-disk binary was not produced")

    BINARY_DIR.mkdir(parents=True, exist_ok=True)
    suffix = ".exe" if source.suffix == ".exe" else ""
    target = BINARY_DIR / f"cloud-disk-{host_triple()}{suffix}"
    shutil.copy2(source, target)
    target.chmod(0o755)
    print(f"Prepared Tauri sidecar: {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

