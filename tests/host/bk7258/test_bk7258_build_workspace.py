#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Exercise isolated BK7258 build workspace ownership checks."""

import stat
import sys
import tempfile
import unittest
from pathlib import Path


REPOSITORY = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPOSITORY / "tools/bk7258"))

from _lib import build as build_domain  # noqa: E402


class BuildWorkspaceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="bk7258-workspace-")
        self.root = Path(self.temporary.name)
        self.workspace = self.root / "workspace"
        self.repository = self.workspace / "contest"
        for relative in ("boards/bk7258", "chips/bk7258", "nuttx", "prebuilt"):
            (self.repository / relative).mkdir(parents=True, exist_ok=True)
        self._wire(self.workspace)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _wire(self, workspace: Path) -> None:
        workspace.mkdir(parents=True, exist_ok=True)
        entry = workspace / "build.sh"
        entry.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        entry.chmod(entry.stat().st_mode | stat.S_IXUSR)
        for relative in ("boards/bk7258", "chips/bk7258", "nuttx", "prebuilt"):
            target = workspace / "vendor/beken" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.symlink_to(self.repository / relative, target_is_directory=True)

    def test_default_and_explicit_workspace_are_compatible(self) -> None:
        self.assertEqual(build_domain._build_workspace(self.repository, None),
                         self.workspace.resolve())
        isolated = self.root / "isolated"
        self._wire(isolated)
        self.assertEqual(build_domain._build_workspace(self.repository, isolated),
                         isolated.resolve())

    def test_workspace_rejects_foreign_vendor_source(self) -> None:
        isolated = self.root / "isolated"
        self._wire(isolated)
        link = isolated / "vendor/beken/nuttx"
        link.unlink()
        foreign = self.root / "foreign-nuttx"
        foreign.mkdir()
        link.symlink_to(foreign, target_is_directory=True)
        with self.assertRaisesRegex(build_domain.BuildError, "does not resolve"):
            build_domain._build_workspace(self.repository, isolated)

    def test_absolute_manifest_infers_only_a_valid_isolated_root(self) -> None:
        isolated = self.root / "isolated"
        self._wire(isolated)
        manifest = (isolated / "out/bk7258/aidk_ai_toy/cp__ap/layout"
                    / "releases/direct/build-manifest.json")
        manifest.parent.mkdir(parents=True)
        manifest.write_text("{}\n", encoding="utf-8")
        self.assertEqual(
            build_domain._manifest_workspace(self.repository, manifest, manifest),
            isolated.resolve(),
        )
        self.assertEqual(
            build_domain._manifest_workspace(self.repository,
                                              Path("out/bk7258/manifest"), manifest),
            self.workspace.resolve(),
        )
        malformed = isolated / "out/bk7258/aidk_ai_toy/build-manifest.json"
        malformed.parent.mkdir(parents=True, exist_ok=True)
        malformed.write_text("{}\n", encoding="utf-8")
        with self.assertRaisesRegex(build_domain.BuildError,
                                    "no OpenVela workspace|does not identify"):
            build_domain._manifest_workspace(self.repository, malformed, malformed)


if __name__ == "__main__":
    unittest.main()
