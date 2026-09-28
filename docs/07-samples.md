# 7. Samples and Android touch-control templates

## Bundled, runnable APK sample

`android/app/src/main/assets/sample.prism` runs on boot inside the native VM. The Android host draws a combined **2D platformer + 3D spectrum cube** in one GLES 3 surface. Left/right touch pads move; right pad jumps when grounded. Both physics and renderer use the same C++ core build target. This is a demo, not a polished game.

## Example project manifests (not yet imported into APK)

- `samples/platformer2d` — Ray jumping between platforms using touch D-pad.
- `samples/fps3d` — proposed mobile FPS with joystick/look region, no weapon implementation.
- `samples/rpg` — proposed mobile RPG with buttons, quests, saves; not implemented.
- `samples/hypercasual` — proposed one-tap arcade loop; not implemented.

The three latter manifests are design samples only; they are not playable projects. Scene importer and custom APK export are future milestones.

## Touch-control library (layout schemas; not yet loaded by runtime)

JSON templates in `templates/touch-controls/` specify screen-relative hitboxes, normalized 0..1 coordinates, dead zones and named axes/buttons. Runtime `InputMap` can be populated from these values manually today; automatic JSON loading is not implemented.

| Template | Input map | Intended use |
|---|---|---|
| `platformer.json` | left/right horizontal, jump | one- or two-thumb platformer |
| `fps.json` | movement joystick, swipe look, fire/jump | mobile first person |
| `rpg.json` | move joystick, interact/attack/menu | RPG/action |
| `one-tap.json` | full-screen tap zone | hypercasual |

Android pointer actions are bridged to the native demo but multi-pointer tracking for gameplay is not complete; `GestureRecognizer` can track 10 IDs independently in host code.
