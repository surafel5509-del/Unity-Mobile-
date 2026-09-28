# 10. Explicit delivery roadmap / non-goals

The request describes a multi-year professional engine. This repository is a working **foundation and Android demo**, not a complete implementation of every requested feature. Shipping a misleading “full engine” would create a fragile codebase. Work should advance in gated milestones:

1. **M0 — foundation (this PR):** C++20 runtime + bus, ECS/jobs, scripted math/gameplay, 2D contact physics, touch input, GLES 3 Android demo, debug APK CI, offline build script. Host tests passing; APK CI result must be checked.
2. **M1 — custom APK export:** project JSON → robust scene serialization → asset packer → Gradle variant generation → release keystore UI → ADB install. Add Android emulator integration tests and an actual GUI scene editor MVP.
3. **M2 — production 2D:** sprite batching/tilemap/atlas, skeletal 2D, 2D light, complete joint/CCD/polygon solver, touch/gamepad/sensors, local audio, save/SQLite, perf profiling.
4. **M3 — 3D mobile renderer:** Vulkan capability negotiation + GLES fallback, glTF importer, PBR forward+, shadow maps, occlusion/LOD, post-processing, quality and thermal measurement.
5. **M4 — production 3D physics and tooling:** rigid bodies, convex mesh collision, joints, vehicles, ragdolls, terrain, animation, particle/shader graphs.
6. **M5 — optional systems:** LAN and Bluetooth netcode, offline signed unlock tokens, ONNX/TFLite, AR/XR, local AI assistant, editor translations and accessibility. Each system behind a permission/size gate and offline tests.
7. **M6 — distribution hardening:** reproducible fully offline installer **including SDK/NDK/Gradle/AGP licenses**, sample assets with provenance, versioned schema migrations, ABI/device lab, crash/trace/profiler tooling, signed release and license audit.

**Incompatibility notes:** Android API 26 devices cannot be assumed to support Vulkan 1.3 or GLES 3.2. A universal Vulkan-first requirement needs feature negotiation and a GLES 3.0 fallback to remain API 26 compatible. A <20 MB APK and an on-device LLM bundled in the same APK are mutually constrained by model size; measure and choose distribution strategy. Truly offline AdMob is impossible. “One file” refers to the final installable APK, not the development toolchain footprint.
