# 5. Mobile project manager and editor

The APK launches the Kotlin **Project Hub** (`ProjectManagerActivity`) first. It searches local projects, creates a blank project, creates/duplicates/deletes a project in private app storage, and installs bundled source kits without a network request. Opening a project launches `PrismStudioActivity` with its local project root.

## Shipped mobile tools

- **Scene / Inspector:** reads and writes the project's `scenes/main.scene.json`; creates hierarchy entities and persists position/rotation/scale edits.
- **UI Builder:** adds Panel, Label, Button, Image, Progress Bar and Touch Stick components to `ui/main.ui.json`, renders a canvas preview, and persists layouts.
- **Texture Painter:** touch-paints pixels, uses a brand-color palette and adjustable brush radius, and saves a real PNG in the project's texture directory.
- **Touch Controls:** adds joystick/action/pause/interact definitions to `controls/touch.json`, previews their safe-area positions and persists a dead-zone value.
- **PrismScript editor:** loads and saves the project's script. Validate invokes the native C++ lexer and parser without executing code. Run passes the selected script to the native interpreter.
- **Model source tool:** emits editable Wavefront OBJ files for basic cube, pyramid, plane and prism-pickup geometry.
- **Assets:** local Android document-picker import copies asset bytes into the selected project's private folder; no network or account.

The project manager and source editor are mobile-first, not a replacement for a desktop Unity/Unreal-scale IDE. UI/controls/scene file edits persist locally. The Android run action executes project PrismScript, but the current runtime still renders the hard-coded GLES 3 prototype scene rather than importing the project's scene or assets.

## Six bundled offline starter kits

`skyrail_2d`, `ember_dungeon_2d`, `chromatic_tiles_2d`, `neon_circuit_3d`, `orbit_foundry_3d`, and `wildlight_tactics_3d` each include `project.prism.json`, scene hierarchy JSON, a PrismScript entry file, OBJ model sources, SVG texture sources, a PBR/spectral material descriptor, a UI layout, and a touch-control profile. They are starter projects with source content—not finished commercial games. `tools/check_starter_kits.py` validates kit completeness, and the host suite executes every included PrismScript entry point.

## Architecture and current gaps

The native editor rendering/import pipeline, Android device viewport integration, visual drag/resize handles, shader/UV authoring, skeletal playback, terrain/foliage persistence, mesh Boolean/CSG, profiler UI and standalone per-project APK exporter are not yet implemented. These gaps are tracked in [the feature matrix](01-architecture.md) and [roadmap](10-roadmap.md). Keep all project files local/offline and Android APK as the sole game export target.

## Desktop CLI starter

With .NET 8 installed: `dotnet run --project editor/src/PrismEditor -- new MyGame /tmp/MyGame`; `... -- inspect /tmp/MyGame`. The CLI creates/inspects a JSON project skeleton; the Android APK build does not yet export arbitrary custom scenes as standalone games.
