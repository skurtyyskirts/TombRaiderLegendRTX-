"""Tracked anchor manifest refresh and mod.usda generation."""
from __future__ import annotations

import copy
import json
from pathlib import Path
from typing import Any

from patches.TombRaiderLegend import usd_analyze

from config import GAME_DIR

from .paths import ANCHOR_MANIFEST_PATH


def load_anchor_manifest() -> dict[str, Any]:
    return json.loads(ANCHOR_MANIFEST_PATH.read_text(encoding="utf-8"))


def save_anchor_manifest(payload: dict[str, Any]) -> None:
    ANCHOR_MANIFEST_PATH.write_text(json.dumps(payload, indent=2), encoding="utf-8")


def _select_hashes(available: list[str], anchor_group: dict[str, Any]) -> tuple[list[str], list[str]]:
    existing = list(anchor_group.get("mesh_hashes", []))
    selection = dict(anchor_group.get("selection", {}))
    mode = selection.get("mode", "explicit_only")
    required_matches = int(selection.get("minimum_matches", min(len(existing), 1)))
    reasons: list[str] = []

    if mode == "intersection":
        chosen = [mesh_hash for mesh_hash in existing if mesh_hash in available]
    elif mode == "explicit_only":
        chosen = [mesh_hash for mesh_hash in existing if mesh_hash in available]
    else:
        return existing, [f"anchor group '{anchor_group.get('id', 'unknown')}' requested unsupported selection mode '{mode}'"]

    if len(chosen) < required_matches:
        reasons.append(
            f"anchor group '{anchor_group.get('id', 'unknown')}' matched {len(chosen)}/{len(existing)} explicit anchor hashes; "
            f"requires at least {required_matches}"
        )

    if mode == "intersection":
        return chosen, reasons
    return existing, reasons


def refresh_anchor_hashes(capture_path: str | Path, *, persist: bool = False) -> dict[str, Any]:
    """Validate tracked anchor hashes against a capture without auto-selecting new anchors."""
    capture = Path(capture_path)
    manifest = copy.deepcopy(load_anchor_manifest())
    mesh_hashes = usd_analyze.extract_mesh_hashes(capture)
    requirements = dict(manifest.get("capture_requirements", {}))
    min_mesh_hash_count = int(requirements.get("min_mesh_hash_count", 1))
    reasons: list[str] = []

    if len(mesh_hashes) < min_mesh_hash_count:
        reasons.append(
            f"capture referenced only {len(mesh_hashes)} mesh hashes; requires at least {min_mesh_hash_count} for anchor validation"
        )

    manifest["last_capture"] = {
        "path": str(capture),
        "mesh_hash_count": len(mesh_hashes),
    }
    for group in manifest.get("anchor_groups", []):
        selected_hashes, group_reasons = _select_hashes(mesh_hashes, group)
        group["capture_match_count"] = len([mesh_hash for mesh_hash in group.get("mesh_hashes", []) if mesh_hash in mesh_hashes])
        group["validated_mesh_hashes"] = [mesh_hash for mesh_hash in group.get("mesh_hashes", []) if mesh_hash in mesh_hashes]
        group["mesh_hashes"] = selected_hashes
        reasons.extend(group_reasons)

    manifest["anchor_validation"] = {
        "status": "blocked" if reasons else "validated",
        "used_tracked_manifest": True,
        "reasons": reasons,
    }
    if persist and not reasons:
        save_anchor_manifest(manifest)
    return manifest


def render_mod_usda(payload: dict[str, Any]) -> str:
    """Generate a minimal mod.usda from tracked anchor data."""
    lines = [
        "#usda 1.0",
        "(",
        '    defaultPrim = "RootNode"',
        ")",
        "",
        'def Xform "RootNode" {',
    ]

    for group in payload.get("anchor_groups", []):
        hashes = group.get("mesh_hashes", [])
        for mesh_hash in hashes:
            lines.append(f'    over "mesh_{mesh_hash}" {{')
            lines.append("        int preserveOriginalDrawCall = 1")
            for light in payload.get("lights", []):
                if light.get("anchor_group") != group["id"]:
                    continue
                lines.extend(
                    [
                        f'        def SphereLight "{light["id"]}" {{',
                        f'            float intensity = {light["intensity"]}',
                        f'            color3f color = ({light["color"][0]}, {light["color"][1]}, {light["color"][2]})',
                        f'            float radius = {light["radius"]}',
                        "        }",
                    ]
                )
            lines.append("    }")
    lines.append("}")
    return "\n".join(lines) + "\n"


def write_live_mod(payload: dict[str, Any]) -> Path:
    relative_output = payload.get("generator", {}).get(
        "output_relative_to_game",
        "rtx-remix/mods/trl-nightly/mod.usda",
    )
    output_path = GAME_DIR / relative_output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render_mod_usda(payload), encoding="utf-8")
    return output_path
