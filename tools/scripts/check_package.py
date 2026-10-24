#!/usr/bin/env python3
"""Checks a release package (ADR-0037) the way a player would receive it:

    python3 tools/scripts/check_package.py build/<preset>/package/Release/AnimalEngine-<v>-<platform>.zip \
        [--forbid PATH ...]

1. extracts the zip into a temporary folder (away from the build tree);
2. the manifest lists exactly the files present, with their sizes and SHA-256 hashes;
3. no file contains any --forbid path (the build machine's source / build folders);
4. the packaged game starts from there, reads its data.pak and runs headless for two seconds.
Exit code 0 when everything holds.
"""

import argparse
import hashlib
import json
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import zipfile


def extract(archive, into):
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            path = pathlib.Path(z.extract(info, into))
            mode = (info.external_attr >> 16) & 0o777
            if mode & stat.S_IXUSR:
                path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("zip", type=pathlib.Path)
    parser.add_argument("--forbid", action="append", default=[], help="a path that must not appear in any file")
    args = parser.parse_args()
    problems = []

    with tempfile.TemporaryDirectory() as tmp:
        extract(args.zip, tmp)
        roots = [p for p in pathlib.Path(tmp).iterdir() if p.is_dir()]
        if len(roots) != 1:
            print("the zip must contain exactly one folder")
            return 1
        root = roots[0]
        manifest = json.loads((root / "manifest.json").read_text())
        listed = {f["path"]: f for f in manifest["files"]}
        present = {p.relative_to(root).as_posix() for p in root.rglob("*") if p.is_file()} - {"manifest.json"}
        for missing in sorted(set(listed) - present):
            problems.append(f"listed but missing: {missing}")
        for extra in sorted(present - set(listed)):
            problems.append(f"present but not listed: {extra}")

        forbidden = set()
        for path in args.forbid:
            forbidden.update({path, path.replace("\\", "/"), path.replace("/", "\\")})
        forbidden = [f.encode() for f in forbidden if len(f) > 3]
        for rel in sorted(present & set(listed)):
            data = (root / rel).read_bytes()
            if len(data) != listed[rel]["size"] or hashlib.sha256(data).hexdigest() != listed[rel]["sha256"]:
                problems.append(f"size or hash differs from the manifest: {rel}")
            for f in forbidden:
                if f in data:
                    problems.append(f"{rel} contains the build machine path {f.decode()}")

        if manifest["platform"] == "macos":
            game = root / "AnimalEngine.app" / "Contents" / "MacOS" / "AnimalGame"
        else:
            game = root / "AnimalGame.exe"
        # --deterministic: no save or settings are read or written.
        result = subprocess.run([str(game), "--headless", "--deterministic", "--strict", "--frames=120"],
                                capture_output=True, text=True, timeout=120, cwd=tempfile.gettempdir())
        log = result.stdout + result.stderr
        if result.returncode != 0 or "(resource pack)" not in log:
            problems.append(f"the packaged game did not run from its data.pak (exit {result.returncode}):\n{log[-3000:]}")

    for p in problems:
        print("PROBLEM:", p)
    if not problems:
        print(f"{args.zip.name}: {len(listed)} files match the manifest, no build paths, the game runs from the package")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
