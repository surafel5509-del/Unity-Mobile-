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
│   │ Typed runtime EventBus + ServiceRegistry + Clock + Quality governor    │   │
│   └─┬─────┬─────┬───────┬─────────┬──────┬───────┬──────┬──────┬─────────┘   │
│     │     │     │       │         │      │       │      │      │             │
│    ECS  Jobs  Script  Physics2D Input  Audio    AI    Net   Assets/UI       │
│     │     │     │       │         │      │       │      │      │             │
│     └─────┴─────┴───────┴─────────┴──────┴───────┴──────┴──────┴─────────┐   │
│  Not yet: Vulkan backend, 3D physics, animation, particles, terrain,      │   │
│           profiler UI, plugin loader, XR, ML agents                       │   │
└───────────────────────────────────────────────────────────────────────────┘   │
                                                                                │
                               Private Android files / offline project assets ─┘
```

## Implemented module-by-module

| Module | Code | Operational scope | Not yet implemented |
|---|---|---|---|
| Engine core | `prism/core` | lifecycle, fixed-step clock, thermal quality profiles, RNG, log ring, arena/pool, typed events, registry | calibrated GPU bench, profiler instrumentation |
| Math | `prism/math` | Vec2/3/4, Mat4, Quat, AABB, Rect, Color, brand palette | SIMD, dual quaternions |
| ECS | `prism/ecs` | sparse-set component pools, generation IDs, ordered systems, transform snapshot hash | serialization, prefab merge, incremental scene graph |
| Jobs | `prism/jobs` | worker threads, fan-out, wait and queued work | affinity-aware big.LITTLE scheduler, graph dependency resolution |
| PrismScript | `prism/script` | lexer/parser/tree-walking interpreter, functions, closures, classes, arrays/maps, stdlib, host-native hooks | .NET 8 AOT/JIT, real debugger, hot reload |
| Crypto | `prism/core/hash.h` | SHA-256, HMAC, HKDF, ChaCha20, Poly1305 (AEAD), CRC-32, FNV-1a, offline unlock codes | hardware-backed keystore |
| JSON | `prism/core/json.h` | full parser/serializer, UTF-8 + surrogate pairs, deterministic key order | schema validation |
| 2D physics | `prism/physics2d` | circle/box collision, AABB broadphase, impulses/friction, gravity, sleep, raycast | joints solver, capsule/polygon narrowphase, full CCD |
| Input | `prism/input` + Android Activity | multi-touch, tap/pinch/swipe recognition, virtual-control mapping, gamepad/sensor data model | complete Android gamepad/sensor bridge |
| Rendering | `prism/render` | GPU-profile detection and quality tiers, spectral rendering (dispersion, IOR, thin film), tonemapping, frame graph, light grid, LOD, adaptive resolution, post-process budgeting | the actual Vulkan/GLES backends that execute the graph |
| Audio | `prism/audio` | WAV decode/encode, 64-voice mixer, voice stealing, bus hierarchy, ADSR, Schroeder reverb, 3D attenuation (6 models), Doppler, constant-power pan, procedural synth | OpenSL/AAudio device output, compressed codecs |
| Game AI | `prism/ai` | grid A*, navmesh + funnel string pulling, flow fields, behaviour trees, state machines, GOAP, utility AI, Reynolds steering, formations, RVO-lite crowd | ML agents, perception/sensing systems |
| Networking | `prism/net` | bit packing, reliable-over-unreliable, RFC 6298 RTT, LAN discovery, host/client sessions, rollback + resimulation, lag compensation, snapshot interpolation | Bluetooth transport, LAN voice |
| Assets | `prism/assets` | `.prism` container with content addressing and tamper detection, dependency graph, atlas packing, meshes + primitives, ETC1/ETC2 codec, mip chains | texture streaming, async import, ASTC encoder |
| UI | `prism/ui` | flexbox-subset layout, 7 widgets, draw-command output, focus/hit-testing, 7-language offline localisation, touch-control binding | text shaping, scroll views, IME input |
| Android | `android/app` | API 26–35, arm64-v8a + armeabi-v7a, landscape, JNI, no INTERNET | permission-gated device APIs, Vulkan detection |
| Editor | `editor/src/PrismEditor` | .NET 8 CLI project create / inspect | graphical IDE and custom-project export |
| APK exporter | `tools/build-apk.sh` | offline Gradle debug/release APK, optional ADB install | bundled toolchain, custom assets/projects, Wi-Fi QR |
| 3D physics, Animation, Particles, Terrain, Profiler UI, Plugins, Save/IAP, XR, ML | not yet | design contracts in docs | implementation, testing, integration |

**Important:** `Physics2D::Joint` is currently a descriptor; `add_joint` stores it but no joint solver is run. `prism/render` builds and validates render pipelines but does not submit GPU work — the APK still draws with a hand-written GLES 3 renderer in `prism_android.cpp`. There is no 3D physics engine and no Vulkan backend. The audio mixer produces samples but nothing writes them to an Android audio device yet.

## Repository structure

```text
Unity-Mobile-/
  README.md  LICENSE  .gitignore
  .github/workflows/android-apk.yml
  android/{build.gradle,settings.gradle,app/src/main/{assets,cpp,java,AndroidManifest.xml}}
  engine/{core/{include/prism/{core,math,ecs,jobs,input,physics2d,script,
         render,audio,ai,net,assets,ui,platform},src},tests}
  editor/src/PrismEditor/{PrismEditor.csproj,Program.cs}
  samples/{platformer2d,fps3d,rpg,hypercasual}
  templates/{touch-controls,save-unlock.md}
  brand/{logo,mascot,tokens}
  docs/{01..10}
  tools/build-apk.sh  build_tests.sh
```

## Brand

Colors: violet `#7C3AED`, cyan `#00D9FF`, gold `#FFC93C`, magenta `#FF3D9A`, dark `#0A0A12`. Fonts: Inter for interface, JetBrains Mono for code (not redistributed; respect font licenses). Logo: [prism beam SVG](../brand/logo/prism-logo.svg); Ray mascot: [light-beam SVG](../brand/mascot/ray.svg). Tagline: **Every Angle. Every World. One File.** Intended domain `prismengine.dev` (ownership not asserted). MIT license, commercial-friendly.
