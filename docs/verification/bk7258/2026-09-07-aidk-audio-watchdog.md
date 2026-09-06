# AIDK raw microphone and watchdog acceptance

Date: 2026-09-07. Physical target: AIDK AI Toy, CH340 UART0 at COM8.
T5-Board and T5AI-Core received build regression only; neither board has a
connected motor in this setup.

## Firmware and scope

The AIDK `drivercheck_cp` / `drivercheck_ap` pair uses the standard NuttX
RPMsg rexec transport to invoke AP driver tests. AP startup remains
`bk7258_ap_main`. The microphone is configured for raw PCM; AEC is disabled.

Accepted firmware: `18.6.326+386`, MCUboot, confirmed CP/AP pair.
The 8-MiB operator image SHA256 is
`a44b108cc255af2390b991c8ee1204c778b1e08cf42cc97e185f74e290554df4`.
The package and trust checks passed, and the device-specific tail remained
identical to the accepted same-unit base. HIL software-reset takeover,
erase and write passed; the new version was read back over UART.
The temporary independent signing keys were removed after acceptance.

This image was built from the integrated development tree. It validates the
listed paths, but is not a claim that every unrelated working-tree feature
or the complete published branch received hardware acceptance.

## Raw capture

The official `cmocka_driver_audio` input case was invoked through CP:

```text
rexec -r -H ap "cmocka_driver_audio -a1 -t5 -p/dev/null -s16000 -c1"
```

The program defaults to `/dev/audio/pcm0c`, 16-bit PCM. The command explicitly
selects capture, 16 kHz, mono, five seconds, and a discard sink. The test
reported one passing case. It passed on v325 and again on v326 after the
temporary AUDIO PM process logs were removed. AP remained healthy afterward.

Two earlier long commands emitted NSH argument-count warnings and are excluded
from exact-format acceptance. Use the quoted compact command above. Captured
audio was discarded; these results do not establish sound quality, AEC
effectiveness, or a measured frame-loss rate. The standard test checks its
duration after dequeuing a buffer, so five seconds is not a hard timeout for
a stalled driver.

## Watchdog

On the idle board, the standard watchdog example was invoked:

```text
wdog -i/dev/watchdog0 -shard -t2000 -d1000 -p200
```

It fed for approximately one second at 200-ms intervals, then stopped. The
board rebooted after the final hardware timeout. After reboot, `resetcause`
reported `rtc_watchdog(0)`; the board mapping logged `raw=16 mapped=2`.
`bkota status` confirmed `18.6.326+386`, and `apctl status` reported AP READY,
CPU2 online, RPMsg connected, and zero supervisor faults/recoveries.

The lower-half feed function uses a mutex, so automatic feeding must execute
in LPWORK or HPWORK task context. The chip-level build check rejects interrupt
methods. Profiles select LPWORK explicitly; Kconfig select/imply cannot force
an individual choice member.

## Source regression and remaining gates

The complete host `make run` suite passed, including four audio-session
ownership/recovery scenarios and the public-header checks. Tests ran outside
the ptrace sandbox with sanitizer checks retained. The initial failures were
missing `CODE` and motor-resource definitions in host mocks; both were repaired.
The package/layer checks and locked OpenAMP/libmetal dependency test passed.
The official NuttX and apps checkouts had no tracked edits.

Clean CP/AP builds passed for all three normal board pairs, all three xTS
pairs, and the AIDK and T5AI-Core drivercheck pairs. Each resulting CP
configuration selected LPWORK for watchdog automonitoring.

PTT-to-Gateway wiring, AEC timing/effectiveness, acoustic quality, storage
consistency, and the remaining peripheral acceptance are separate unfinished
gates. An attempted microphone/motor overlap check missed the active capture
window and is not evidence of the physical interlock or acoustic isolation.
