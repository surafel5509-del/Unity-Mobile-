# 1. System architecture & implementation status

PRISM ENGINE is an Android-APK-only engine **in early development**, not a finished product. The codebase is original to this repository. The native core is C++20. The editor CLI is .NET 8. Production editor, Vulkan, NativeAOT, AI/ML and advanced render features require substantial future work.

## Full system architecture — target

```text
                       ┌───────────────────────────────────────────────┐
                       │ Offline C# editor (future full IDE)           │
                       │ hierarchy • inspector • asset DB • scene view │
                       │ code • anim • UI • terrain • graphs • profiler│
                       └──────────────────┬────────────────────────────┘
                                          │ project.prism.json / scene JSON
                                          ▼
                       ┌───────────────────────────────────────────────┐
                       │ APK exporter (Gradle + NDK + CMake; offline)  │
                       │ pack native .so + dex + scenes + assets + sig │
                       └──────────────────┬────────────────────────────┘
                                          │  one installable APK
                                          ▼
┌────────────────────────────────────────────────────────────────────────────────┐
│ Android Activity → GLSurfaceView → JNI → C++20 Engine                         │
│                                      │                                         │
│   ┌──────────────────────────────────┴─────────────────────────────────────┐   │
│   │ Typed runtime EventBus + ServiceRegistry + Clock + Quality governor   │   │
│   └─┬────────┬──────────┬──────────┬────────┬────────┬───────┬───────────┘   │
│     │        │          │          │        │        │       │               │
│    ECS     Jobs      PrismScript  Physics2D  Input   GLES  Logs             │
│     │        │          │          │        │        │       │               │
│     └────────┴──────────┴──────────┴────────┴────────┴───────┴───────────┐   │
│  Future: Vulkan, 3D physics, audio, nav/AI, LAN, asset pipeline, XR, ML  │   │
└───────────────────────────────────────────────────────────────────────────┘   │
                                                                                │
                               Private Android files / offline project assets ─┘
```

## Implemented module-by-module

| Module | Code | Operational scope | Not yet implemented |
|---|---|---|---|
| Engine core | `engine/core/{include,src}/prism/core` | lifecycle, fixed-step clock, thermal quality profiles, RNG, log ring, arena/pool, typed events, registry | calibrated GPU bench, profiler instrumentation |
| ECS | `ecs/ecs.h` | sparse-set component pools, generation IDs, ordered systems, transform snapshot hash | serialization, prefab merge, incremental scene graph |
| Jobs | `jobs/job_system.*` | worker threads, fan-out, wait and queued work | affinity-aware big.LITTLE scheduler, graph dependency resolution |
| PrismScript | `script/*` | lexer/parser/tree-walking interpreter, functions, closures, classes, arrays/maps, stdlib, host-native hooks | .NET 8 AOT/JIT, real debugger, hot reload |
| 2D physics | `physics2d/*` | circle/box collision, AABB broadphase, impulses/friction, gravity, sleep, raycast | joints solver, capsule/polygon narrowphase, full CCD, deterministic guarantees |
| Input | `input/*`, Android Activity | multi-touch events, tap/pinch/swipe recognition, virtual-control mapping, gamepad/sensors data model | complete Android gamepad/sensor bridge |
| Rendering | `android/app/src/main/cpp/prism_android.cpp` | GLES 3 shader, spectrum-colored 3D geometry, orthographic 2D, depth and blend state | Vulkan, PBR/GI, batching, SDFGI, all requested effects |
| Android | `android/app` | API 26–35, arm64-v8a + armeabi-v7a, landscape, JNI, no INTERNET | permission-gated device APIs, Vulkan detection |
| Editor | `editor/src/PrismEditor` | .NET 8 CLI project create / inspect | graphical IDE and custom-project export |
| APK exporter | `tools/build-apk.sh` | offline Gradle debug/release APK, optional ADB install | bundled toolchain, custom assets/projects, Wi-Fi QR |
| Audio, AI, Net, UI, Assets, Save, IAP, XR | not yet | design contracts in docs | implementation, testing, integration |

**Important:** `Physics2D::Joint` is currently a descriptor; `add_joint` stores it but no joint solver is run. The native 3D cube is not PBR or physically ray-traced spectral rendering. There is no 3D physics engine.

## Repository structure

```text
Unity-Mobile-/
  README.md  LICENSE  .gitignore
  .github/workflows/android-apk.yml
  android/{build.gradle,settings.gradle,app/src/main/{assets,cpp,java,AndroidManifest.xml}}
  engine/{core/{include/prism/{core,math,ecs,jobs,input,physics2d,script},src},tests}
  editor/src/PrismEditor/{PrismEditor.csproj,Program.cs}
  samples/{platformer2d,fps3d,rpg,hypercasual}
  templates/{touch-controls,save-unlock.md}
  brand/{logo,mascot,tokens}
  docs/{01..10}
  tools/build-apk.sh  build_tests.sh
```

## Brand

Colors: violet `#7C3AED`, cyan `#00D9FF`, gold `#FFC93C`, magenta `#FF3D9A`, dark `#0A0A12`. Fonts: Inter for interface, JetBrains Mono for code (not redistributed; respect font licenses). Logo: [prism beam SVG](../brand/logo/prism-logo.svg); Ray mascot: [light-beam SVG](../brand/mascot/ray.svg). Tagline: **Every Angle. Every World. One File.** Intended domain `prismengine.dev` (ownership not asserted). MIT license, commercial-friendly.
