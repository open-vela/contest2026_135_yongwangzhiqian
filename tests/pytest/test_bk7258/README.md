# BK7258 official pytest integration

The manifest links this directory to
`<workspace>/tests/scripts/script/test_bk7258`.  Run tests from the official
OpenVela tests script directory so the parent UART0 fixture, raw capture and
utilities remain the only serial owner.

## XTS board baseline

Use the board's `xts` CP image paired with its normal `openvela_ap` image:

```bash
cd <workspace>/tests/scripts
pytest script/test_bk7258/test_bk7258_board.py \
  -D <UART0-device> -B <t5_board|t5ai_core|aidk_ai_toy> -U cp \
  -P <directory-containing-the-CP-.config> \
  -L <log-directory> -F /data -R target -M serial
```

`-D` is the selected board's UART0 CP NuttShell/debug port.  The test contains
one BK7258-wide boot contract and a board-keyed marker table; it does not infer
peripherals from one board.  Other UARTs and AP-owned peripheral transports are
never opened by the official fixture.  This suite is for a board's `xts` CP
image paired with that same board's normal `openvela_ap` image; it is not the
contract for `drivercheck`, performance, or production-only profiles.

## AIDK normal-product HIL

`test_aidk_app.py` drives the normal AIDK `openvela_cp` + `openvela_ap` pair.
The default subset is read-only or observational: AP supervisor, native Wi-Fi
lease plus configured-gateway ping, BKVoice/BKDisplay status, ETA4322 battery
plus chip-temperature status, two live SC7A20H samples, and two NFC presence
reads.  The Wi-Fi case first runs `bkwifi status`, which also reconciles a CP
lease with a stopped AP netdev, then requires `bkwifi ping` to traverse the
native NuttX route.  Preserve a
machine-readable report with
`--junitxml`; the UART bytes remain in the directory selected by `-L`:

```bash
cd <workspace>/tests/scripts
pytest script/test_bk7258/test_aidk_app.py \
  -D <Linux-visible-UART0-device> -B aidk_ai_toy -U cp \
  -P <directory-containing-the-openvela_cp-.config> \
  -L <new-log-directory> -F /data -R target -M serial \
  --junitxml=<new-log-directory>/aidk-app-safe.xml
```

Camera capture and a `happy -> neutral` eye-expression cycle are opt-in device actions.
Run them only with an operator present:

```bash
BK7258_AIDK_HIL_ACTIVE=1 pytest \
  script/test_bk7258/test_aidk_app.py \
  -D <Linux-visible-UART0-device> -B aidk_ai_toy -U cp \
  -P <directory-containing-the-openvela_cp-.config> \
  -L <new-log-directory> -F /data -R target -M serial \
  --junitxml=<new-log-directory>/aidk-app-full.xml
```

The production contract does not call temporary `capture-test` or `tone-test`
commands. Microphone/AEC/PTT acceptance belongs to the real voice-session path.

Each successful command is recorded only as `FUNCTION_PASS`.  JUnit properties
retain the remaining physical gate: audible quality/PA timing, microphone/AEC
quality, real scene and privacy indicator, dual-screen content and left/right
mapping, known-card/no-card NFC response, and battery/temperature comparison
with reference instruments.  SD NAND capacity is outside this UART suite: the
HIL download/USBmode operator must perform a separate MSC READ CAPACITY or
host Get-Disk check and record 126877696 bytes (247808 sectors of 512 bytes).
That result does not prove full-media read/write or cold-power retention.

The XTS boot baseline covers SC7A20H, GC2145, MFRC522, ETA4322, SDIO and both
LCD registrations.  The normal-product suite additionally requires two
successful `bkmotion sample` operations.  They prove the AP-owned standard
sensor path returned fresh timestamped telemetry, but axis direction, scale
and motion response still require physical comparison.  CN10's motor is connected and P9 is its active-high control. P9 must never
be swept by a generic LED/GPIO test. No motor action is part of the default
suite; a bounded active test requires the motor driver's separate acceptance.

The suite never resets or flashes the target and never changes USB0 to MSC.
In particular, AIDK's CH340E RTS/DTR lines are not used for reset.  If COM8 is
owned by Windows rather than exposed as a Linux serial device, use the sibling
`tools/windows-hardware-debug` capture tool for individual cases; do not run a
second serial owner concurrently.  Firmware download/reboot remains the
separate `tools/bk7258-hil-download` workflow.
