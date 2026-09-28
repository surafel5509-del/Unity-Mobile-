# 9. Testing and airplane-mode plan

## Implemented host checks

`./build_tests.sh` compiles all C++ runtime sources and tests with C++20, threads and `-Wall -Wextra`. Current tests cover math, palette, memory allocators, ECS generations, typed bus, jobs, gestures, 2D falling/raycast, deterministic RNG, PrismScript functions/classes/collections, interpreter error reports, and the exact bundled script. Tests must pass before Android CI builds.

## APK CI

`.github/workflows/android-apk.yml` (GitHub-hosted Ubuntu) executes host tests, provisions Android SDK/NDK/CMake and Gradle, builds `:app:assembleDebug`, checks for `libprism_runtime.so` and `sample.prism`, uploads `prism-engine-debug-apk`. This is an **online CI provisioning process**, not an offline installer. See CI run status in the PR; do not mark the APK built until that workflow passes.

## On-device test matrix (not automated yet)

| Device | Android | ABI | GPU | Expected test |
|---|---|---|---|---|
| Emulator | API 26 | arm64 or x86_64* | SwiftShader | cold boot, shader compile, touch |
| Budget handset | API 26–29 | armv7 | Mali/Adreno | 2D/3D render, landscape, thermal |
| Mid-range | API 30–34 | arm64 | Mali/Adreno | 60 FPS target, pause/resume |
| Flagship | API 35 | arm64 | Xclipse/Adreno | 120 FPS target (not promised) |

*Default APK only includes arm64-v8a and armeabi-v7a; an x86_64 emulator requires a separate developer build ABI filter. Real performance targets have **not** been measured. Require reproducible profiling before publishing numbers.

## Failure injection

1. Enable airplane mode on dev machine + device, delete network routes, build with Gradle `--offline` after full provisioning.
2. Deny all optional permissions; game must launch (default manifest requests none).
3. Kill activity mid-frame; reopen. Surface/context must recreate shaders and buffers.
4. Throttle/low battery: log quality change; verify no crash. Current Android layer does not yet bridge ThermalManager to native governor.
5. Corrupt a save file and wrong-key decrypt (once encrypted save exists).
6. Run 2D physics with fixed inputs on both ABIs, compare snapshots and resolve drift before enabling rollback.
7. Test APK signature via `apksigner`; test on clean device with Wi-Fi disabled and no Google Play Services.
