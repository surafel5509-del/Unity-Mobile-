# 5. Editor UI mockup and starter code

The only editor code currently shipped is `editor/src/PrismEditor` — a .NET 8 **CLI** that creates and inspects a JSON project skeleton. It does **not** have a viewport, drag/drop, scene serialization, or custom APK export. `build-apk` explicitly refuses to pretend it exported a custom game.

## Proposed dark-theme IDE wireframe (not implemented)

```text
┌──────────────────────────── PRISM ENGINE ────────────────────────────────┐
│ File  Edit  Scene  Assets  Window  Help        [▶ Play] [◈ Build APK] │
├───────────────┬───────────────────────────────────────┬──────────────────┤
│ HIERARCHY     │ VIEWPORT         [2D] [3D] [Game]     │ INSPECTOR        │
│ ▾ Main        │                                       │ Transform        │
│  ▸ Player     │    ◇ move gizmo  • camera frustum    │ Position [x y z] │
│  ▸ World      │    grid, guides, mobile safe area    │ Scale    [x y z] │
│  ▸ UI         │                                       │ [+ component]    │
├───────────────┴───────────────────────────────────────┴──────────────────┤
│ ASSETS     │ TIMELINE / SCRIPT EDITOR / SHADER GRAPH │ PROFILER / LOG   │
│ scenes/    │ Code: PrismScript, completion, errors    │ CPU GPU RAM FPS  │
│ scripts/   │ Animation keyframes / visual graph       │ local logcat     │
└───────────────────────────────────────────────────────────────────────────┘
```

Theme tokens: dark `#0A0A12`, active violet `#7C3AED`, interactive cyan `#00D9FF`, warning gold `#FFC93C`, error magenta `#FF3D9A`. Target keyboard shortcuts: Ctrl+S save, Ctrl+B Build APK, F5 run, F9 break. Proposed responsive modes: beginner (template/guided) and pro (graph/code), offline translations EN/AM/ES/ZH/FR/AR/HI. None of these UI functions or translations are currently shipped.

## CLI starter

With .NET 8 preinstalled: `dotnet run --project editor/src/PrismEditor -- new MyGame /tmp/MyGame`; `... -- inspect /tmp/MyGame`. CLI is for authoring on a development host; **the only game export remains Android APK**. The project skeleton format is a starting contract; it is not consumed by the Android APK builder yet.
