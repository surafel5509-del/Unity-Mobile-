# 7. Samples and Android touch-control templates

## Bundled native demo

`android/app/src/main/assets/sample.prism` runs in the native VM. The Android GLES 3 host draws a combined 2D platformer and rotating spectrum cubes. Left/right touch pads move; the right pad jumps. This is a technology demo, not a polished or project-imported game.

## Six APK-bundled starter kits

Gradle packages `samples/` into APK assets. The offline Project Hub copies a selected kit into private app storage, where Studio can edit its scene JSON, source script, UI, controls, and imported/generated source assets.

| Kit | Type | Source content |
|---|---|---|
| `skyrail_2d` | 2D precision platformer | PrismScript checkpoint loop, hierarchy, hero/pickup OBJ, SVG albedo/emissive, PBR material, HUD, touch profile |
| `ember_dungeon_2d` | 2D top-down action RPG | player/room/enemy/treasure scene, health/key script, source meshes and textures, UI and controls |
| `chromatic_tiles_2d` | 2D color puzzle | tile-board scene, match/move rules, source art, layout and touch input |
| `neon_circuit_3d` | 3D arcade time trial | track/car/gates hierarchy, lap logic, OBJ/SVG source assets, material and HUD |
| `orbit_foundry_3d` | 3D exploration | modular-room hierarchy, energy/interact logic, source art, UI and touch profile |
| `wildlight_tactics_3d` | 3D turn-based tactics | squad/arena scene, turn-state script, model/material sources, HUD and controls |

These are editable offline project starters, not complete commercial games. `tools/check_starter_kits.py` checks required files and JSON references; `test_starter_kits.cpp` executes all six scripts with the C++20 VM. A selected script runs in the current APK demo, but the game's scene graph, models, textures, input profile and UI layout are not yet imported into the renderer/runtime.

## Touch-control templates

Legacy schemas in `templates/touch-controls/` describe screen-relative hitboxes, normalized coordinates, dead zones and named axes/buttons. The new starter kits each include `controls/touch.json`; Studio can author a project's control definitions. The native game runtime does not yet automatically load these profiles or render their controls.
