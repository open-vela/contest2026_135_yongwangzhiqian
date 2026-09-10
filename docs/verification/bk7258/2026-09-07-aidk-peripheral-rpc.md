# AIDK peripheral RPC verification

Date: 2026-09-07. Target: AIDK AI Toy on COM8, firmware `18.6.327+387`.
T5-Board and T5AI-Core were not operated; neither has a connected motor in
this setup.

## Changes and source checks

Motion and NFC requests are bound to a connection epoch. Device teardown and
namespace unbind invalidate pending requests and replay caches. Old I/O still
closes its device, but cannot publish a stale result or clear a new request.
Clients retry `RPMSG_ERR_NO_BUFF` within their existing send deadline and fail
an old exchange after reconnection. Device I/O remains in the service worker.

The actual core, client and service sources passed 19 deterministic host
scenarios per peripheral with UBSan and compiler warnings treated as errors:

```sh
make -C tests/host/bk7258 run-motion-rpc run-nfc-rpc
python3 tests/host/bk7258/test_mfrc522_read_errors.py
```

The MFRC522 maintenance patch initializes the UID and propagates card-selection
errors from the existing NuttX read implementation. The test applies the real
patch to its documented baseline and compiles the read body. It covers no
card, two selection errors, normal formatting and existing NULL/zero-buffer
behavior. This is fault injection, not an RF/card reliability measurement.
The patch was applied only to the isolated validation NuttX tree. Official
NuttX and apps checkouts had no tracked changes.

## Image and board observations

The AIDK drivercheck CP/AP pair built cleanly. Required peripheral options
were present in the resolved configurations. The build manifest, package and
complete BL1/BL2/CP/AP signatures passed verification. The 8-MiB operator image
SHA256 is `911f3cac2ffd37fda8c553e5a1334e81ab9cd58e79f33fb2a97d71d573415404`.
Its device-specific tail matched the accepted same-unit backup.

HIL software-reset takeover, erase and write passed. Fresh UART evidence
confirmed version `18.6.327+387`, counter 387, AP READY, CPU2 online and RPMsg
connected. This was an integrated working-tree build, not a clean published
HEAD image. Temporary independent signing keys were removed after the bounded
hardware checks.

| Check | Observation | Acceptance limit |
| --- | --- | --- |
| `bkmotion sample`, three requests | Increasing timestamps; resting acceleration norm 9.873–9.898 m/s² | Pose unconfirmed; six faces and motion/recovery not checked |
| `bknfc scan`, two requests | Both reported `present=no`; no UID exported | Known-card insertion/removal and repeated recognition not checked |
| `bkhealth status`, two requests | Charging; 4059/4060 mV; temperature raw 541/542 | No reference instrument, charging transition or temperature calibration |
| Official `fb /dev/fb0` and `fb /dev/fb1` via RPMsg rexec | Both 160×160, 16 bpp; open/ioctl/mmap/draw/close completed | Visible panel quality and physical left/right mapping unconfirmed |
| Standard audio input via rexec | Five seconds, 16 kHz, mono, 16-bit PCM to `/dev/null`; one CMocka case passed | No saved audio or acoustic/AEC measurement |
| Final `apctl health` and `bkota status` | Confirmed v327; faults/recoveries/consecutive = 0/0/0 | No long-duration product stress claim |

Uncalibrated temperature remained raw; Celsius and battery percentage were
not invented. The display examples did not read assets or write SD storage.

PTT/Gateway integration, AEC effectiveness, physical peripheral acceptance,
and the previously identified storage-consistency issue remain open. These
peripheral changes were retained in the working tree pending their separate
acceptance gates; this note does not declare those workstreams complete.
