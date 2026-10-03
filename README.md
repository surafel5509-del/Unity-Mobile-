# ◈ PRISM ENGINE

**Every Angle. Every World. One File.** · Android APK only · MIT · offline-first

> **Status: early engine foundation, not a finished Unity-equivalent.** The C++20 core now includes host-tested 2D/3D physics, animation, particles, terrain, scene data, PrismScript, networking and asset systems. The Kotlin APK now opens an offline project manager and Studio shell with persistent scene transforms, a JSON UI-layout editor, PNG texture painter, touch-control profiles, OBJ source authoring, and native PrismScript syntax validation. Six new 2D/3D starter kits ship with scene/script/model/SVG/material/UI/control sources. The runtime renderer remains a GLES 3 demo, and Studio's terrain/foliage/animation/modeling backends, GPU asset pipeline, and per-project APK export are not yet integrated. This is not a complete Unity-equivalent or commercial-ready game engine. See [feature status](docs/01-architecture.md#implemented-module-by-module).

![PRISM logo](brand/logo/prism-logo.svg)

## Quick start

Host C++20 tests (GCC 12+):

```bash
./build_tests.sh
```

Build the **bundled 2D + 3D demo APK** after preinstalling JDK 17, Gradle 8.9, Android SDK 35, NDK 27.2.12479018 and CMake 3.22.1:

```bash
tools/build-apk.sh debug              # --offline; never downloads
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
# or: tools/build-apk.sh debug --install
```

The GitHub Actions [Android APK](.github/workflows/android-apk.yml) workflow validates the six starter kits, runs C++ tests, then builds and uploads `prism-engine-debug-apk`. **No APK is checked into Git.** CI provisioning is online; local game projects and authoring tools work offline. The APK requests no INTERNET permission. Launch the APK to open the project manager; create a project from a source kit to open Prism Studio. `RUN` loads and executes that project's PrismScript in the native VM, while rendering still uses the current GLES 3 demo scene. Standalone custom-scene APK export is not yet implemented.

## Repository

| Directory | Purpose |
|---|---|
| `engine/core` | Native C++20 runtime (~15,200 lines): core, math, ECS, jobs, input, script VM, crypto, JSON, render planning, audio, AI, networking, assets, UI, 2D + 3D physics, animation, particles, terrain, scene graph, Android GPU profiling |
| `engine/tests` | Zero-dependency C++ host tests (182 tests, 6,992 checks) |
| `android` | Kotlin offline project manager and Prism Studio + JNI + GLES 3 runtime demo; builds Android APK |
| `editor/src/PrismEditor` | .NET 8 CLI project creation/inspection starter |
| `samples` | Six source-rich 2D/3D starter kits plus legacy sample concepts; kit files are bundled into the APK |
| `templates` | Touch layout and offline save/unlock design templates |
| `brand` | SVG logo, Ray mascot, color and typography tokens |
| `docs` | Offline architecture, feature status, diagrams, build & test plan |

## How to contribute

Start with [architecture](docs/01-architecture.md), [APK workflow](docs/03-apk-workflow.md), and [roadmap](docs/10-roadmap.md). Keep Android APK as the sole runtime export. New subsystems use the typed `EventBus` and `ServiceRegistry`; add host tests for every feature. All code MIT; do not add a phone-home or mandatory online SDK.
