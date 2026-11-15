"""validate.py — L0: manifest, bilingual docs and packaging, stdlib only.

The host validates the manifest itself at load time; this checks the same shape
so a bad manifest fails here instead of on the user's machine, and keeps the
documentation contract (bilingual files, cross links) mechanical.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
import subprocess
import sys
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def check_manifest(quick: bool) -> dict:
    m = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8-sig"))
    problems: list[str] = []

    if m.get("manifest") != 1:
        problems.append("manifest must be 1")
    if not re.fullmatch(r"[a-z0-9_-]([a-z0-9._-]*[a-z0-9_-])?", m["id"]):
        problems.append("id shape")
    if m["id"] != ROOT.name:
        problems.append("id must equal the folder name")
    if not re.fullmatch(r"\d+\.\d+\.\d+", m["version"]):
        problems.append("version must be X.Y.Z")

    for key in ("name", "description"):
        text = m.get(key, {})
        if not (isinstance(text, dict) and text.get("zh") and text.get("en")):
            problems.append(f"{key} needs zh and en")

    runtime = m.get("runtime", {})
    if runtime.get("mode") != "resident":
        problems.append("mode must be resident")
    if runtime.get("sandbox") != "normal":
        problems.append("sandbox must be normal")
    if runtime.get("start") != "lazy":
        problems.append("start must be lazy")
    if runtime.get("qt") != "host":
        problems.append("qt must be host (shared Qt contract)")
    if not re.fullmatch(r"\d+\.\d+(\.\d+)?", runtime.get("qtVersion", "")):
        problems.append("qtVersion shape")
    if runtime.get("qt") and not runtime.get("qtVersion"):
        problems.append("qt requires qtVersion")
    # `runtime.elevated` is a different field (an old shape) and stays forbidden;
    # This plugin must stay in the user's ordinary execution context.
    if runtime.get("elevated"):
        problems.append("this plugin must not set runtime.elevated")

    perms = m.get("permissions", [])
    if "elevated" in perms:
        problems.append("packages must not inherit the host's administrator token")
    if "clipboard:write" not in perms:
        problems.append("clipboard_copy requires clipboard:write")
    if "ui" not in perms or "processes" not in perms:
        problems.append("ui and processes are required for this plugin")

    if not m.get("commands"):
        problems.append("commands must not be empty")
    # At least one instant command so a keyword alone (no trailing space) opens
    # the window; a list-only plugin needs "keyword " and reads as broken.
    modes = [c.get("mode", "list") for c in m.get("commands", [])]
    if "instant" not in modes:
        problems.append("at least one command must be instant so the keyword alone works")
    for command in m.get("commands", []):
        for key in ("title", "hint"):
            text = command.get(key, {})
            if not (isinstance(text, dict) and text.get("zh") and text.get("en")):
                problems.append(f"command {command.get('id')} {key} needs zh and en")

    return {"manifest": m, "problems": problems}


def check_docs() -> list[str]:
    problems: list[str] = []
    # Every *.md at the root and under docs/ must have its bilingual counterpart,
    # and the first ten lines must link to it. docs/ is included because a
    # validation record that only exists in one language is not the deliverable.
    for doc in list(ROOT.glob("*.md")) + list((ROOT / "docs").glob("*.md")):
        if doc.name.endswith(".en.md") or doc.name == "README.en.md":
            continue
        counterpart = doc.with_name(doc.stem + ".en.md")
        if not counterpart.exists():
            problems.append(f"missing counterpart: {counterpart.name}")
            continue
        head = "\n".join(doc.read_text(encoding="utf-8").splitlines()[:10])
        if f"({counterpart.name})" not in head:
            problems.append(f"{doc.name} does not link {counterpart.name} in the first ten lines")
    return problems


def check_binary(runtime_program: str) -> list[str]:
    problems: list[str] = []
    path = ROOT / runtime_program
    if not path.exists():
        problems.append(f"missing binary: {runtime_program}")
        return problems
    data = path.read_bytes()
    if data[:2] != b"MZ":
        problems.append("binary is not PE")
        return problems
    offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[offset : offset + 4] != b"PE\0\0":
        problems.append("PE signature missing")
        return problems
    machine = struct.unpack_from("<H", data, offset + 4)[0]
    if machine != 0x8664:
        problems.append("binary must be x64")
    return problems


def check_package(m: dict) -> list[str]:
    problems: list[str] = []
    program = m["runtime"]["program"]
    dist = ROOT / "dist" / "development"
    package_json = dist / "package.json"
    if not package_json.exists():
        problems.append("no dist/development/package.json; run tools/package.py --dev")
        return problems
    package = json.loads(package_json.read_text(encoding="utf-8"))
    data = (dist / package["file"]).read_bytes()
    if hashlib.sha256(data).hexdigest() != package["sha256"]:
        problems.append("package sha256 mismatch")
    with zipfile.ZipFile(dist / package["file"]) as z:
        if z.read("plugin.json") != (ROOT / "plugin.json").read_bytes():
            problems.append("packaged manifest differs")
        if z.read(program) != (ROOT / program).read_bytes():
            problems.append("packaged binary differs")
    return problems


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true", help="skip packaging checks")
    parser.add_argument("--package-dir", type=pathlib.Path, default=ROOT / "dist/development")
    args = parser.parse_args()

    checks = check_manifest(args.quick)
    problems: list[str] = list(checks["problems"])
    problems += check_docs()
    problems += check_binary(checks["manifest"]["runtime"]["program"])
    if not args.quick:
        problems += check_package(checks["manifest"])

    report = {
        "manifest_id": checks["manifest"]["id"],
        "version": checks["manifest"]["version"],
        "problems": problems,
        "ok": not problems,
    }
    out = ROOT / "build" / "verification"
    out.mkdir(parents=True, exist_ok=True)
    (out / "static.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    if problems:
        for p in problems:
            print("FAIL:", p, file=sys.stderr)
        return 1
    print("PASS manifest shape, bilingual documents and x64 binary")
    return 0


if __name__ == "__main__":
    sys.exit(main())
