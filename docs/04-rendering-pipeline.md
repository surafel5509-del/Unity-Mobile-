# 4. Rendering — current pipeline and target architecture

## Implemented demo pipeline (GLES 3)

```text
Android GLSurfaceView (EGL 3 context, depth 24)
  → JNI nativeSurfaceCreated: compile/link GLSL ES 3.0
  → JNI nativeFrame(dt): Engine clock + 2D physics
  → clear HDR-like dark BG (#0A0A12)
  → perspective MVP → six-colored rotating 3D cube (per-face spectrum)
  → disable depth/cull → orthographic MVP → 2D platformer + Ray rays
  → GLSurfaceView swap → Canvas HUD label / FPS
```

The fragment shader performs simple tone mapping (`color/(1+color)`) and gamma transform. No physical spectral refraction, Vulkan, PBR, GI or production batching exists yet. GLES 3.0 is a compatibility baseline on API 26; do not label it GLES 3.2.

## Target Vulkan mobile + spectral path (NOT implemented)

```text
GPU probe (Adreno/Mali/PowerVR/Xclipse) → Vulkan >=1.3?
    ├ yes → VkSwapchain/VkRenderGraph, timeline semaphores, async transfer
    └ no  → GLES 3.2 if supported, else GLES 3.0 fallback
          ↓
ASTC/ETC2 asset upload → mesh cull + LOD + instancing
          ↓
Depth prepass → clustered Forward+ (Low/Medium) or Deferred (High/Ultra)
          ↓
PBR BRDF, Light2D and sprite batch → DDGI/SDFGI optional budgeted
          ↓
SPECTRAL PASS: prism-indexed wavelength samples (6–12 bands) →
  Sellmeier refraction + dispersion → recombine through CIE XYZ → linear RGB
          ↓
Shadows → bloom → AO → color grade/ACES → UI → swapchain

Thermal/battery governor → quality tier → scale render targets (0.6..1.0)
```

Budget targets of 60 FPS mid-range, 120 FPS flagship, <20 MB base APK, <2 s cold boot are **goals, unmeasured**. To keep a Vulkan renderer honest, implement feature detection and physical-device capability tests before enabling it. Golden-image GPU regression tests and a performance lab are required.
