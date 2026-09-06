# Shaniu Android companion A1

This directory contains the native Kotlin companion app for Shaniu. The
`0.5.0-a1` production entry point uses the concrete HTTPS/WSS `console-v1`
client; the deterministic fake Gateway remains only as a unit-test fixture.
No endpoint, device identity, access token or certificate pin is baked into the
APK.

The overview page provisions an explicit HTTPS Gateway root, device ID,
optional canonical `sha256/...` SPKI pins and a bearer token. Non-secret
connection metadata is stored in private app preferences. The token is stored
separately with AES-GCM under a non-exportable Android Keystore key and is never
rendered or logged. This is an at-rest guarantee only and does not claim
StrongBox or hardware-backed key storage.

After an authenticated snapshot succeeds, the app opens a cursor-bound WSS
event stream. Sequence gaps fail closed and trigger a full snapshot refresh.
All network and Keystore work runs off the UI thread. Disconnect/reconnect uses
a connection generation so late callbacks cannot update the new session.

The active UI supports:

- reported device, turn, emotion, battery, firmware and update state;
- remote turn start/cancel without capturing phone audio;
- volume and persona mutations;
- locally confirmed L3 privacy mutations;
- immutable-manifest firmware update requests with local confirmation.

The UI never applies a mutation optimistically. An accepted receipt means only
that Gateway admitted the request; the device must report the resulting state.
For OTA, only a later `CONFIRMED` report is completion.

The client freezes these relative endpoints:

- `GET /console/v1/devices/{device_id}/snapshot`;
- `GET /console/v1/devices/{device_id}/firmware/releases?generation={generation}`;
- `POST /console/v1/devices/{device_id}/mutations`;
- `WSS /console/v1/devices/{device_id}/events?generation={generation}&after_sequence={sequence}`.

`OkHttpConsoleGatewaySession` injects the token only inside the authenticated
transport, rejects cross-origin requests, disables redirects and transparent
retries, bounds decoded bodies to 64 KiB, and installs no logging interceptor.
The manifest permits Internet access, forbids cleartext traffic and trusts only
system CAs. Clearing local binding data removes both ordinary configuration and
the encrypted credential.

Only Xiaomi 10 / Android 10 is currently in device-acceptance scope. The app
uses `minSdk 29` and compiles/targets SDK 35:

```bash
./gradlew :app:testDebugUnitTest :app:assembleDebug
```

The debug APK is emitted at
`app/build/outputs/apk/debug/app-debug.apk`. Host unit tests cover the strict
wire contract, state reducer, mutation policy, secure transport construction,
failure mapping and the isolated fake lifecycle. Android Keystore behavior,
real TLS/WSS interoperability and Xiaomi 10 installation remain device tests;
a host build alone is not evidence for those gates.

BLE provisioning, phone media upload, the production Gateway service and
device-bound OTA execution are outside this Android module.
