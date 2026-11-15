"""Deterministic local package; --dev permits the explicit uncommitted sources.

Release mode uses git-tracked files plus the executable. --tag additionally
requires matching version/changelogs and a clean worktree. Never publishes.

Produces <id>-<version>[.wsplugin] + .sha256 + package.json + release notes in
--out (default dist/development). The contents are byte-stable for the same
inputs, so a rebuilt package hashes identically.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import pathlib
import re
import subprocess
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]

# --dev packages an explicit list: everything the plugin needs at runtime, plus
# the sources a reviewer reads. Build outputs and scratch are excluded by hand.
DEV_FILES = [
    "plugin.json",
    "packages.exe",
    "CMakeLists.txt",
    "winshun.h",
    "LICENSE",
    "app.ico",
    "packages.rc",
    "README.md",
    "README.en.md",
    "CHANGELOG.md",
    "CHANGELOG.en.md",
    "THIRD_PARTY.md",
    "THIRD_PARTY.en.md",
    "docs/implementation.md",
    "docs/implementation.en.md",
    "docs/validation.md",
    "docs/validation.en.md",
    "docs/validation-theme.md",
    "docs/validation-theme.en.md",
    "docs/validation-queue.md",
    "docs/validation-queue.en.md",
    "docs/dev-harness.md",
    "docs/dev-harness.en.md",
    "src/main.cpp",
    "src/core/identity.h",
    "src/core/identity.cpp",
    "src/core/version.h",
    "src/core/version.cpp",
    "src/core/model.h",
    "src/core/model.cpp",
    "src/core/plan.h",
    "src/core/plan.cpp",
    "src/core/queue.h",
    "src/core/queue.cpp",
    "src/core/redact.h",
    "src/core/redact.cpp",
    "src/core/json_lite.h",
    "src/core/json_lite.cpp",
    "src/core/cache.h",
    "src/core/cache.cpp",
    "src/core/info_cache.h",
    "src/core/info_cache.cpp",
    "src/core/theme_tokens.h",
    "src/core/theme_tokens.cpp",
    "src/adapters/adapter.h",
    "src/adapters/winget_table.h",
    "src/adapters/winget_table.cpp",
    "src/adapters/winget_adapter.h",
    "src/adapters/winget_adapter.cpp",
    "src/adapters/scoop_adapter.h",
    "src/adapters/scoop_adapter.cpp",
    "src/platform/process.h",
    "src/platform/process.cpp",
    "src/ui/models.h",
    "src/ui/models.cpp",
    "src/ui/theme_resolver.h",
    "src/ui/theme_resolver.cpp",
    "src/ui/window.h",
    "src/ui/window.cpp",
    "src/ui/qml_window.cpp",
    "src/ui/qml/Main.qml",
    "src/ui/qml/Theme.qml",
    "src/ui/qml/RowDelegate.qml",
    "src/ui/qml/DetailPane.qml",
    "src/ui/qml/ReviewDialog.qml",
    "src/ui/qml/TasksPage.qml",
    "src/ui/qml/ThemedButton.qml",
    "src/ui/qml/ThemedField.qml",
    "src/ui/qml/ThemedCheckBox.qml",
    "src/ui/qml/RowMenu.qml",
    "src/ui/qml/PackageInfoWindow.qml",
    "tests/core_tests.cpp",
    "tests/theme_tokens_tests.cpp",
    "tests/process_tests.cpp",
    "tests/protocol_tests.cpp",
    "tests/fixture_tests.cpp",
    "tests/scoop_tests.cpp",
    "tests/validate.py",
    "tools/package.py",
]


def version_notes(filename: str, version: str) -> str:
    text = (ROOT / filename).read_text(encoding="utf-8")
    section = re.search(
        r"^## \[" + re.escape(version) + r"\][^\n]*(?:\n(?!## \[)[^\n]*)*", text, re.MULTILINE
    )
    if not section:
        raise ValueError("Missing release changelog: " + filename)
    return section.group(0).strip()


def archive(files: list[str]) -> bytes:
    # Fixed timestamps and permissions: the same inputs produce the same bytes.
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as z:
        for name in sorted(files):
            source = (ROOT / name).resolve()
            if not source.is_relative_to(ROOT.resolve()) or not source.is_file():
                raise ValueError("Invalid package source: " + name)
            info = zipfile.ZipInfo(name, (2020, 1, 1, 0, 0, 0))
            info.create_system = 3
            info.external_attr = 0o100644 << 16
            z.writestr(info, source.read_bytes())
    return output.getvalue()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dev", action="store_true")
    parser.add_argument("--tag")
    parser.add_argument("--out", type=pathlib.Path, default=ROOT / "dist/development")
    args = parser.parse_args()

    m = json.loads((ROOT / "plugin.json").read_text(encoding="utf-8-sig"))
    version = m["version"]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Version must be X.Y.Z")
    if args.dev and args.tag:
        raise ValueError("--dev cannot be used with --tag")
    if args.tag:
        if args.tag != "v" + version:
            raise ValueError("Tag does not match manifest")
        if subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT, text=True).strip():
            raise ValueError("Release requires a clean worktree")
        for name in ["CHANGELOG.md", "CHANGELOG.en.md"]:
            if f"## [{version}]" not in (ROOT / name).read_text(encoding="utf-8"):
                raise ValueError("Missing release changelog: " + name)

    program = m["runtime"]["program"]
    if args.dev:
        files = set(DEV_FILES)
    else:
        tracked = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT, text=True).split("\0")
        files = {name for name in tracked if name and not any(part.startswith(".") for part in pathlib.PurePosixPath(name).parts)}
        if "plugin.json" not in files:
            raise ValueError("No tracked manifest; use --dev for an uncommitted preview")
        files.add(program)

    data = archive(sorted(files))
    if data != archive(sorted(files)):
        raise ValueError("Package changed while being written")
    if len(data) > 64 * 1024 * 1024:
        raise ValueError("Package exceeds host size limit")
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        if json.loads(z.read("plugin.json")) != m:
            raise ValueError("Manifest mismatch")
        if z.read(program) != (ROOT / program).read_bytes():
            raise ValueError("Binary mismatch")
        if z.testzip():
            raise ValueError("Invalid archive CRC")

    digest = hashlib.sha256(data).hexdigest()
    binary = hashlib.sha256((ROOT / program).read_bytes()).hexdigest()
    args.out.mkdir(parents=True, exist_ok=True)
    name = f"{m['id']}-{version}{'-dev' if args.dev else ''}.wsplugin"
    (args.out / name).write_bytes(data)
    (args.out / (name + ".sha256")).write_text(f"{digest}  {name}\n", encoding="ascii")

    if args.dev:
        # The dev notes are generated from the manifest, so a permission or
        # command change cannot leave the note claiming something else. Permissions
        # mix plain strings with {"scope": [paths]} objects, so flatten both.
        flat_perms: list[str] = []
        for p in m.get("permissions", []):
            if isinstance(p, str):
                flat_perms.append(p)
            elif isinstance(p, dict):
                flat_perms.extend(str(k) for k in p)
        perms = "、".join(flat_perms)
        perms_en = ", ".join(flat_perms)
        # The manifest stores `keyword` as either a string or a list.
        def first_keyword(command: dict) -> str:
            kw = command.get("keyword", "")
            if isinstance(kw, list):
                return kw[0] if kw else ""
            return str(kw)

        commands_zh = "、".join(f"`{first_keyword(c)}`" for c in m.get("commands", []))
        commands_en = ", ".join(f"`{first_keyword(c)}`" for c in m.get("commands", []))
        notes = (
            f"# 软件包 {version} 开发包\n\n[English](release-notes.en.md)\n\n"
            f"本地开发预览，未正式发布。安装后在 Win顺 输入 {commands_zh} 使用；"
            "更新后需重启插件进程。\n\n"
            "开发包不代表已通过完整发布验收。请查看包内 docs/validation.md 总览及两个专项记录"
            "（风格一致性、任务干预与卸载入口）中的验证结果、失败项与未测边界。\n\n"
            f"权限见 plugin.json（{perms}）。`elevated` 只跟随 Win顺 的管理员状态，"
            "插件不弹 UAC、不自行提权。\n\n"
            f"文件：`{name}`\n\nSHA256：`{digest}`\n"
        )
        english = (
            f"# Packages {version} development package\n\n[中文](release-notes.md)\n\n"
            "Local development preview, not a published release. After installation, "
            f"use any of {commands_en} in Winshun; restart the plugin process after updating.\n\n"
            "A development package does not imply full release acceptance. See the "
            "overview in the included docs/validation.en.md and the two focused records "
            "(style consistency; task intervention and the uninstall entry) for the "
            "results, failures and coverage limits.\n\n"
            f"Permissions are declared in plugin.json ({perms_en}). "
            "`elevated` only follows Winshun's administrator state; the plugin never "
            "raises UAC and never elevates itself.\n\n"
            f"File: `{name}`\n\nSHA256: `{digest}`\n"
        )
    else:
        notes = f"# 软件包 {version}\n\n[English](release-notes.en.md)\n\n" + version_notes("CHANGELOG.md", version) + f"\n\n文件：`{name}`\n\nSHA256：`{digest}`\n"
        english = f"# Packages {version}\n\n[中文](release-notes.md)\n\n" + version_notes("CHANGELOG.en.md", version) + f"\n\nFile: `{name}`\n\nSHA256: `{digest}`\n"

    (args.out / "release-notes.md").write_text(notes, encoding="utf8")
    (args.out / "release-notes.en.md").write_text(english, encoding="utf8")
    (args.out / "package.json").write_text(
        json.dumps(
            {
                "file": name,
                "sha256": digest,
                "exe_sha256": binary,
                "bytes": len(data),
                "files": sorted(files),
                "deterministic": True,
                "mode": "development" if args.dev else "release",
            },
            indent=2,
        )
        + "\n",
        encoding="utf8",
    )
    print(str(args.out / name))
    print("SHA256 " + digest)


if __name__ == "__main__":
    main()
