# MFRC522 frame transport overlay

The complete driver and private register header derive from OpenVela NuttX
commit `76354c637858ecb0aa4601629327acb6f44a26bb`, preserving its Apache license.
The driver incorporates `nuttx/patches/contactless/0001-*` and `0002-*`.
Only the public extension header include and SPI-frequency configuration name
differ from the patched upstream source. `test_mfrc522_exchange.py` checks that
relationship against the pinned source before exercising the exchange function.

`CL_MFRC522_FRAME` replaces `CL_MFRC522` in the AIDK profile. The existing
directory-level vendor mapping supplies the source, and BK7258 CMake/Make
integration builds one implementation. The board still supplies only transport
and wiring. Official source checkouts and SDK bundles are unchanged.

The new ioctl exchanges selected-card byte-aligned CRC_A frames. It is not an
ISO-DEP implementation: selection/session ownership, negotiated frame waiting,
WTX, chaining and APDU handling must be supplied before Android HCE can work.
Callers must serialize all access to the same selected card.

`timeout_ms = 0` retains the legacy hardware timeout. A caller-supplied wait
of 1..300000 ms temporarily disables automatic hardware timing and uses a
software deadline, including 20 ms for transmitting a full FIFO. The polling
loop yields for 1 ms between reads. The driver stops the command and timer,
then restores the previous timer mode on both success and transport failure.
This supports long negotiated waits without overflowing the hardware counter;
it does not itself parse ATS or process WTX. Application session deadlines must
still bound repeated WTX requests.

Timer behavior follows NXP MFRC522 Rev. 3.9, sections 8.5 and 9.3.3.10:
<https://www.nxp.com/docs/en/data-sheet/MFRC522.pdf>.

## ISO-DEP activation

`CL_ISODEP` builds the independent `isodep.c` protocol module. The current
entry point activates a selected Type A target, advertises FSD 64, validates
ATS lengths and optional fields, and applies the advertised startup guard time.
All activation failures release the caller-owned RF field. The transport is a
callback interface and has no dependency on MFRC522, BK7258 or Android.

Activation parsing follows [ISO/IEC 14443-4:2018, section 5](https://cdn.standards.iteh.ai/samples/73599/1c24245bb3cb4a749fdd467b0c68acc9/ISO-IEC-14443-4-2018.pdf)
and [NXP AN12057](https://www.nxp.com/docs/en/application-note/AN12057.pdf).
The host test covers all FWI/SFGI combinations, all FSCI values, missing optional
fields, opaque historical bytes, transport errors and interrupted guard waits.
APDU exchange now supports both directions of chaining, bounded retransmission,
WTX within a caller deadline and failure cleanup. The AP service's `bknfc hce`
command activates a selected target and selects the Shaniu phone AID. It reports
presence only for the exact protocol-version response. No UID or APDU payload
crosses RPMsg, and the command cannot grant ownership. Physical phone acceptance
is still required. `MFRC522IOC_SET_RF` allows the adapter to restart the field
before selection and drop it on exit; a subsequent scan re-enables the field.
