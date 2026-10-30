#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]


class VersionTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kmos-version-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "scripts").mkdir()
        self.script = self.root / "scripts/version.py"
        shutil.copyfile(ROOT / "scripts/version.py", self.script)
        (self.root / "VERSION").write_text("0.2.0\n")
        (self.root / ".gitignore").write_text("src/kernel/generated/\ndist/\n*.elf\n*.iso\n")
        self.git("init", "-q")
        self.git("config", "user.name", "Version Test")
        self.git("config", "user.email", "test@example.invalid")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "tag.gpgsign", "false")
        self.git("config", "core.hooksPath", "/dev/null")
        self.commit()

    def git(self, *arguments):
        return subprocess.run(["git", "-C", str(self.root), *arguments], check=True,
                              text=True, capture_output=True).stdout.strip()

    def commit(self):
        self.git("add", ".")
        self.git("commit", "-qm", "test version")

    def run_version(self, *arguments, success=True, cwd=None):
        result = subprocess.run([sys.executable, str(self.script), *map(str, arguments)],
                                cwd=cwd or self.root, capture_output=True, text=True)
        if success:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout)
        return result

    def info(self):
        return json.loads(self.run_version("--json").stdout)

    def tag(self, name="v0.2.0"):
        self.git("tag", "-a", name, "-m", "test release")

    def test_clean_development_and_archive(self):
        info = self.info()
        revision = self.git("rev-parse", "HEAD")
        self.assertEqual(info["display_version"], f"0.2.0-dev+g{revision[:12]}")
        self.assertEqual(info["revision"], revision)
        self.assertFalse(info["dirty"] or info["release"])
        shutil.rmtree(self.root / ".git")
        self.assertEqual(self.info()["display_version"], "0.2.0-dev+unknown")
        self.run_version("--check-release", "v0.2.0", success=False)

    def test_dirty_tracked_staged_and_untracked(self):
        self.tag()
        path = self.root / "VERSION"
        path.write_text("0.2.1\n")
        self.assertTrue(self.info()["dirty"])
        self.git("add", "VERSION")
        self.assertTrue(self.info()["dirty"])
        self.git("reset", "--hard", "-q", "HEAD")
        untracked = self.root / "new.c"
        untracked.write_text("new source\n")
        self.assertTrue(self.info()["display_version"].endswith(".dirty"))
        self.run_version("--check-release", "v0.2.0", success=False)
        untracked.unlink()
        (self.root / "kernel.elf").write_bytes(b"ignored")
        self.assertEqual(self.info()["display_version"], "0.2.0")

    def test_release_requires_matching_annotated_tag_at_head(self):
        self.run_version("--check-release", "v0.2.0", success=False)
        self.git("tag", "v0.2.0")
        self.assertFalse(self.info()["release"])
        self.run_version("--check-release", "v0.2.0", success=False)
        self.git("tag", "-d", "v0.2.0")
        self.tag()
        self.assertEqual(self.info()["display_version"], "0.2.0")
        self.assertEqual(self.run_version("--check-release", "v0.2.0").stdout.strip(), "0.2.0")
        self.run_version("--check-release", "v0.3.0", success=False)
        (self.root / "change.c").write_text("next change\n")
        self.commit()
        self.assertFalse(self.info()["release"])
        self.run_version("--check-release", "v0.2.0", success=False)

    def test_header_updates_without_touching_unchanged_builds(self):
        header = self.root / "src/kernel/generated/version.h"
        self.run_version("--header", header)
        original = header.read_text()
        timestamp = header.stat().st_mtime_ns
        time.sleep(0.01)
        self.run_version("--header", header, cwd=self.root / "scripts")
        self.assertEqual(header.stat().st_mtime_ns, timestamp)
        self.assertFalse(self.info()["dirty"])
        (self.root / "next.c").write_text("new source\n")
        self.run_version("--header", header)
        self.assertNotEqual(header.read_text(), original)
        self.assertIn(".dirty", header.read_text())
        (self.root / "next.c").unlink()
        self.tag()
        self.run_version("--header", header)
        self.assertIn('#define KERNEL_VERSION "0.2.0"', header.read_text())
        metadata = json.loads(header.with_suffix(".json").read_text())
        self.assertEqual(metadata, self.info())

    def test_ci_checkout_can_restore_annotated_tag(self):
        self.tag()
        with tempfile.TemporaryDirectory(prefix="kmos-checkout-") as target:
            checkout = Path(target) / "repo"
            subprocess.run(["git", "clone", "--quiet", "--no-hardlinks", str(self.root), str(checkout)],
                           check=True, capture_output=True)
            command = [sys.executable, str(checkout / "scripts/version.py"), "--check-release", "v0.2.0"]
            subprocess.run(["git", "-C", str(checkout), "update-ref", "refs/tags/v0.2.0", "HEAD"], check=True)
            rejected = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0)
            self.assertIn("annotated tag", rejected.stderr)
            subprocess.run(["git", "-C", str(checkout), "fetch", "--force", "--no-tags", "origin",
                            "refs/tags/v0.2.0:refs/tags/v0.2.0"], check=True, capture_output=True)
            restored = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(restored.returncode, 0, restored.stderr)
            self.assertEqual(restored.stdout.strip(), "0.2.0")

    def test_version_format(self):
        for value in ("1.0", "01.2.0", "0.02.0", "0.2.00", "v0.2.0", "0.2.0-dev", "1;echo bad", ""):
            with self.subTest(value=value):
                (self.root / "VERSION").write_text(value + "\n")
                self.assertIn("MAJOR.MINOR.PATCH", self.run_version(success=False).stderr)

    def test_package_matches_release_and_checksums(self):
        self.tag()
        header = self.root / "src/kernel/generated/version.h"
        self.run_version("--header", header)
        (self.root / "src/kernel/kernel.elf").write_bytes(b"kernel image")
        (self.root / "kmos.iso").write_bytes(b"boot image")
        self.run_version("--package", "dist")
        directory = self.root / "dist"
        self.assertEqual({path.name for path in directory.iterdir()},
                         {"kmos-0.2.0-kernel.elf", "kmos-0.2.0.iso", "kmos-0.2.0-build.json", "SHA256SUMS"})
        for line in (directory / "SHA256SUMS").read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(hashlib.sha256((directory / name).read_bytes()).hexdigest(), digest)
        manifest = json.loads((directory / "kmos-0.2.0-build.json").read_text())
        self.assertEqual(manifest["revision"], self.git("rev-parse", "HEAD"))
        self.run_version("--package", "dist", success=False)

    def test_package_rejects_stale_missing_and_dirty_builds(self):
        self.tag()
        self.run_version("--package", "dist", success=False)
        header = self.root / "src/kernel/generated/version.h"
        self.run_version("--header", header)
        self.assertIn("missing release artifact", self.run_version("--package", "dist", success=False).stderr)
        (self.root / "src/kernel/kernel.elf").write_bytes(b"kernel image")
        (self.root / "kmos.iso").write_bytes(b"boot image")
        metadata = header.with_suffix(".json")
        contents = json.loads(metadata.read_text())
        contents["revision"] = "stale"
        metadata.write_text(json.dumps(contents))
        self.assertIn("does not match", self.run_version("--package", "dist", success=False).stderr)
        self.run_version("--header", header)
        (self.root / "pending.c").write_text("untracked change\n")
        self.assertIn("clean working tree", self.run_version("--package", "dist", success=False).stderr)


if __name__ == "__main__":
    unittest.main()
