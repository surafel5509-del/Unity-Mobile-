# 9. Testing and airplane-mode plan

## Implemented host checks

`./build_tests.sh` compiles every C++ runtime source and test with C++20,
`-Wall -Wextra` and threads, then runs `build-host/prism_tests`. Passing an
argument filters by substring (`./build_tests.sh net_`). The suite is
**182 tests / 6,992 checks / 0 failures** and must pass before Android CI
builds. The CI workflow also runs `python3 tools/check_starter_kits.py` before C++ compilation.

| File | Tests | What it pins down |
|---|---|---|
| `test_core.cpp` | 6 | engine lifecycle, fixed-step clock, pools, log ring, typed bus, registry |
| `test_math.cpp` | 9 | vectors, Mat4, Quat, transforms, brand palette |
| `test_script.cpp` | 6 | PrismScript functions, closures, classes, collections, error reports, the bundled `sample.prism` |
| `test_starter_kits.cpp` | 1 | Executes all six bundled 2D/3D starter-kit PrismScript entry points in the native interpreter |
| `test_crypto.cpp` | 9 | SHA-256, HMAC-SHA256, ChaCha20 and Poly1305 against RFC 8439 vectors, CRC-32, unlock codes, plus a 500-case AEAD fuzz |
| `test_json.cpp` | 7 | parser/serializer round trips, escapes, surrogate pairs, error reporting |
| `test_render.cpp` | 15 | GPU-profile detection and quality tiers, spectral dispersion, tonemapping, frame-graph ordering, light grid, LOD, adaptive resolution |
| `test_audio.cpp` | 12 | WAV round trip, envelopes, mixer voices and stealing, bus gain, attenuation, Doppler, panning, reverb tail, synth |
| `test_ai.cpp` | 17 | A*, navmesh funnel, flow fields, behaviour trees, state machines, GOAP, utility curves, steering, formations, crowd separation |
| `test_net.cpp` | 28 | bit packing, packet codec, ack bitmask, retransmission, RFC 6298 RTT, discovery, sessions, rollback resimulation, lag compensation, snapshot interpolation, a real UDP loopback round trip |
| `test_assets.cpp` | 21 | `.prism` container round trip and tamper detection, dependency graph and build order, atlas packing, mesh primitives and winding, ETC1/ETC2 known-answer vectors, mip chains |
| `test_ui.cpp` | 18 | flexbox layout and grow shares, alignment, hit testing, button/slider/toggle interaction, draw-command ordering, 7-language localisation, touch bindings |
| `test_anim.cpp` | 7 | skeleton transforms, pose blend/additive, sorted keyframe sampling, clip wrap/events, crossfade, two-bone IK |
| `test_particles.cpp` | 5 | curve/gradient interpolation, emitter shapes, pool/lifetime, ground bounce/render instances, seeded determinism |
| `test_physics3d.cpp` | 9 | collider bounds/contact, sphere and box rest/sleep, capsule, elastic/inelastic impulses, momentum, raycasts, determinism |
| `test_terrain.cpp` | 7 | raise/smooth/flatten brushes, bilinear sample, normals, terrain raycast/mesh, deterministic painted foliage scatter |
| `test_scene.cpp` | 5 | hierarchy/world transform, scale composition, lookup/destroy/reparent, component inspector bags, JSON round-trip |

Coverage is behavioural rather than line-based: each subsystem has at least one
test that would fail if the algorithm regressed, and several were written to
catch bugs the implementation actually had (unclamped audio attenuation, an
out-of-bounds reverb write, a Mali tier threshold, a frame-graph false cycle, a
mip chain that read a moved-from buffer, an inside-out sphere winding, a
double-fired UI tap).

Known gaps: no GPU-side tests (nothing submits Vulkan or GLES work), no
on-device automation, no performance regression gate. The new 3D physics,
animation, particle, terrain and scene tests exercise their host-side C++
algorithms only; those subsystems are not yet connected to the Android demo's
runtime loop or GPU renderer.

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
