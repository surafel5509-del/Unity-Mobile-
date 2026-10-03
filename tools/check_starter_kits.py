#!/usr/bin/env python3
"""Offline content check for the six distributable PRISM starter kits."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
KITS = {
    "skyrail_2d": ("Skyrail Dash", "2D"),
    "ember_dungeon_2d": ("Ember Dungeon", "2D"),
    "chromatic_tiles_2d": ("Chromatic Tiles", "2D"),
    "neon_circuit_3d": ("Neon Circuit", "3D"),
    "orbit_foundry_3d": ("Orbit Foundry", "3D"),
    "wildlight_tactics_3d": ("Wildlight Tactics", "3D"),
}
REQUIRED = (
    "project.prism.json", "README.md", "scenes/main.scene.json",
    "scripts/main.prism", "assets/models/hero_source.obj",
    "assets/models/pickup_source.obj", "assets/textures/hero_albedo.svg",
    "assets/textures/world_emissive.svg", "assets/materials/hero.material.json",
    "ui/main.ui.json", "controls/touch.json",
)

def main() -> int:
    failures = []
    for slug, (name, dimension) in KITS.items():
        base = ROOT / "samples" / slug
        files = {rel: base / rel for rel in REQUIRED}
        for rel, path in files.items():
            if not path.is_file() or path.stat().st_size == 0:
                failures.append(f"{slug}: missing/empty {rel}")
        if failures and any(not p.is_file() for p in files.values()):
            continue
        try:
            manifest = json.loads(files["project.prism.json"].read_text())
            scene = json.loads(files["scenes/main.scene.json"].read_text())
            ui = json.loads(files["ui/main.ui.json"].read_text())
            controls = json.loads(files["controls/touch.json"].read_text())
            material = json.loads(files["assets/materials/hero.material.json"].read_text())
            assert manifest["name"] == name and manifest["dimension"] == dimension
            assert manifest["export"]["only"] == "android-apk" and manifest["offline"] is True
            assert scene["entities"] and all("name" in e and "components" in e for e in scene["entities"])
            assert ui["nodes"] and controls["controls"]
            assert material["shader"] == "prism/pbr_spectral"
            assert "func start()" in files["scripts/main.prism"].read_text()
            for svg in (files["assets/textures/hero_albedo.svg"], files["assets/textures/world_emissive.svg"]):
                assert "<svg" in svg.read_text()
            assert "v " in files["assets/models/hero_source.obj"].read_text()
        except (AssertionError, KeyError, json.JSONDecodeError) as exc:
            failures.append(f"{slug}: invalid starter-kit content ({exc})")
    if failures:
        print("Starter-kit validation failed:", *failures, sep="\n  ", file=sys.stderr)
        return 1
    print(f"[prism] validated {len(KITS)} offline starter kits, source assets, scenes, UI, controls and materials")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
