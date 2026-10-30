#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def git(*arguments):
    try:
        result = subprocess.run(["git", "-C", str(ROOT), *arguments], capture_output=True, text=True)
    except FileNotFoundError:
        return None
    return result.stdout.strip() if result.returncode == 0 else None


def build_info():
    version = (ROOT / "VERSION").read_text().strip()
    if not re.fullmatch(r"(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)", version):
        raise ValueError("VERSION must contain MAJOR.MINOR.PATCH without leading zeroes")
    revision = None
    dirty = False
    tagged = False
    top = git("rev-parse", "--show-toplevel")
    if top and Path(top).resolve() == ROOT:
        revision = git("rev-parse", "--verify", "HEAD")
        status = git("status", "--porcelain", "--untracked-files=all")
        if status is None:
            raise ValueError("cannot determine whether the working tree is clean")
        dirty = bool(status)
        tag = f"refs/tags/v{version}"
        tagged = bool(revision and git("cat-file", "-t", tag) == "tag" and
                      git("rev-parse", "--verify", tag + "^{commit}") == revision)
    short = revision[:12] if revision else "unknown"
    release = tagged and not dirty
    display = version if release else f"{version}-dev+" + (f"g{short}" if revision else short)
    if dirty:
        display += ".dirty"
    return {"version": version, "display_version": display, "revision": revision or "unknown",
            "short_revision": short, "dirty": dirty, "release": release,
            "tag": f"v{version}" if tagged else None}


def write_changed(path, contents):
    path = Path(path)
    if path.exists() and path.read_text() == contents:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False) as output:
            temporary = Path(output.name)
            output.write(contents)
        os.replace(temporary, path)
    finally:
        if temporary and temporary.exists():
            temporary.unlink()


def header(info, path):
    contents = ("#ifndef KMOS_BUILD_VERSION_H\n#define KMOS_BUILD_VERSION_H\n\n"
                f"#define KERNEL_VERSION {json.dumps(info['display_version'])}\n"
                f"#define KERNEL_REVISION {json.dumps(info['short_revision'])}\n\n#endif\n")
    write_changed(path, contents)
    write_changed(Path(path).with_suffix(".json"), json.dumps(info, indent=2, sort_keys=True) + "\n")


def check_release(info, tag):
    if tag != f"v{info['version']}":
        raise ValueError(f"release tag must match VERSION: v{info['version']}")
    if info["tag"] != tag:
        raise ValueError("release requires an annotated tag pointing at HEAD")
    if info["dirty"]:
        raise ValueError("release requires a clean working tree, including untracked files")
    if not info["release"]:
        raise ValueError("cannot identify a release commit")


def package(info, directory):
    check_release(info, f"v{info['version']}")
    metadata = ROOT / "src/kernel/generated/version.json"
    if not metadata.is_file() or json.loads(metadata.read_text()) != info:
        raise ValueError("kernel build metadata does not match this release; rebuild before packaging")
    sources = {f"kmos-{info['version']}-kernel.elf": ROOT / "src/kernel/kernel.elf",
               f"kmos-{info['version']}.iso": ROOT / "kmos.iso"}
    for source in sources.values():
        if not source.is_file():
            raise ValueError(f"missing release artifact: {source.relative_to(ROOT)}")
    directory = Path(directory).resolve()
    if directory == ROOT or (ROOT in directory.parents and directory != ROOT / "dist"):
        raise ValueError("release output inside the repository must be dist/")
    if directory.exists() and any(directory.iterdir()):
        raise ValueError("release output directory must be empty")
    directory.mkdir(parents=True, exist_ok=True)
    for name, source in sources.items():
        shutil.copyfile(source, directory / name)
    manifest = directory / f"kmos-{info['version']}-build.json"
    manifest.write_text(json.dumps(info, indent=2, sort_keys=True) + "\n")
    checksums = []
    for path in sorted(directory.iterdir()):
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        checksums.append(f"{digest}  {path.name}\n")
    (directory / "SHA256SUMS").write_text("".join(checksums))


def main():
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--json", action="store_true")
    action.add_argument("--header", type=Path)
    action.add_argument("--check-release", metavar="TAG")
    action.add_argument("--package", type=Path, metavar="DIRECTORY")
    args = parser.parse_args()
    try:
        info = build_info()
        if args.header:
            header(info, args.header)
        elif args.check_release:
            check_release(info, args.check_release)
            print(info["version"])
        elif args.package:
            package(info, args.package)
        elif args.json:
            print(json.dumps(info, sort_keys=True))
        else:
            print(info["display_version"])
    except (OSError, ValueError) as error:
        parser.exit(1, f"version: {error}\n")


if __name__ == "__main__":
    main()
