# 8. Assets, save games, offline IAP, audio, AI and LAN — design contracts

These are architecture contracts, **not shipped runtime systems**. See [status matrix](01-architecture.md#implemented-module-by-module).

## Asset import (target)

`source → importer worker → validated intermediate → compression → content hash → APK asset pack`.

- 3D: glTF/GLB (first), OBJ; FBX/USD/DAE/BLEND require licensed importer or external tool. Do not claim support without parsing and test assets.
- Textures: PNG/JPEG → KTX2/ASTC for capable GPUs, ETC2 universal fallback; transcode at build time and select at runtime. ATC optional only on supported devices.
- Audio: WAV/OGG/MP3/FLAC decoded to PCM stream; OpenSL ES/AAudio output; spatializer and Doppler staged.
- Fonts: TTF/OTF rasterized to signed distance fields. Video: MP4/WebM via Android MediaCodec; pack inside APK.
- Every asset has ID, source hash, importer settings, platform format, cache key; traversal-safe paths. Generated binary blobs should be size capped and excluded from Git.

## Local save + high score (target)

- Per-project private Android files directory; versioned data structure, atomic temp-write + fsync + rename, migration and crash recovery.
- High scores indexed in SQLite or SharedPreferences; avoid storing sensitive values in preferences unencrypted.
- **Encrypted save requirement is not implemented.** Proposed: Android Keystore hardware-backed AES-256-GCM key, random 96-bit nonce per write, version/slot as authenticated associated data, no key material serialized, explicit backup/recovery policy. JNI bridge needed. Unit tests must verify wrong-key and tamper failures.
- Do not confuse checksum, XOR, base64 or obfuscation with encryption.

## Offline unlock codes (target)

- No Google Play purchases. **Do not label offline unlock codes “in-app purchases”** without explaining they are independently issued license codes; no payment processing or refunds in this repo.
- Issuer signs `product ID | device-bound-or-portable policy | expiry | nonce` using Ed25519 **offline**. APK contains public key only, never secret signing key. Verify signature locally; store redeemed token in encrypted local state. Portable codes can be shared: cryptography cannot prevent sharing without a central registry. Strong copy protection without an online authority is impossible.
- AdMob is explicitly not present in the default APK (it requires INTERNET and therefore cannot operate fully offline). Optional online integration must be a separate opt-in build and must never change the offline default.

## AI/ML and navigation (target)

Navmesh baker → local A* / flow field → steering → behavior trees / utility AI; runtime inference via independently bundled ONNX/TFLite model + NNAPI capabilities. Ship model licenses and size budget; do not promise an LLM in <20MB base APK without model measurements.

## LAN-only networking (target)

Android permission-gated multicast discovery → local UDP transport → reliable sequence/ack → state snapshot + input prediction/rollback. No cloud, no matchmaking. Wi-Fi Direct/hotspot/Bluetooth transport and LAN voice chat need Android permission and platform tests; none are implemented. Check replay determinism across ARM ABIs before using rollback.

## Other target services

Audio mixer, notifications, haptics, Camera2/CameraX, GPS, NFC, biometrics, share sheet, XR/AR, accessibility, localization and plugin sandbox require separate Android permission/capability adapters. None are included in the current demo; no placeholder Java APIs misrepresent coverage.
