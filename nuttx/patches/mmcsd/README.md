# MMCSD SDIO read limit patch

Base: OpenVela NuttX `76354c637858ecb0aa4601629327acb6f44a26bb`.
License: Apache-2.0, matching the upstream files.

`0001-sdio-separate-read-limit.patch` retains the global transfer ceiling and
adds `CONFIG_MMCSD_READ_MULTIBLOCK_LIMIT` as an optional stricter read ceiling.
Zero preserves existing behavior. Raw read ioctls also obey the ceiling.

Apply to an isolated NuttX checkout with `git apply --check` followed by
`git apply`. The main official checkout remains unmodified. AIDK validation
uses global limit 16, read limit 1 and `CONFIG_SDIO_BLOCKSETUP=y`, through the
existing `tools/bk7258/bk7258.py` build/release interface in an isolated workspace.

Validation: seven AIDK ARM translation-unit configurations passed. Isolated
v18.6.240+300 built and booted; short-recording full payload readback passed;
60-second recording completed 1088 frames without SDIO timeout. This is one
hardware run, not a general multi-board acceptance claim. Detailed evidence:
`out/bkvision-perf-v240/` in the team checkout.
