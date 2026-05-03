#!/usr/bin/env python3
"""Deploy a historical test build to the game directory for manual testing.

Usage:
    python patches/TombRaiderLegend/deploy_build.py <build_folder_name_or_number>

Examples:
    python patches/TombRaiderLegend/deploy_build.py 064
    python patches/TombRaiderLegend/deploy_build.py build-064-hash-stability-FAIL-lights-missing
    python patches/TombRaiderLegend/deploy_build.py current   # use patches/TombRaiderLegend/proxy/ as-is
"""

import re
import sys
import shutil
import subprocess
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT  = SCRIPT_DIR.parent.parent
GAME_DIR   = REPO_ROOT / "Tomb Raider Legend"
PROXY_DIR  = SCRIPT_DIR / "proxy"
TESTS_DIR  = REPO_ROOT / "TRL tests"

def find_build_dir(arg: str) -> Path | None:
    if arg == "current":
        return None
    # Exact folder name
    candidate = TESTS_DIR / arg
    if candidate.is_dir():
        return candidate
    # Build number prefix (e.g. "064" matches build-064-...)
    prefix = f"build-{arg}-" if not arg.startswith("build-") else arg
    matches = sorted(d for d in TESTS_DIR.iterdir() if d.name.startswith(prefix))
    if matches:
        return matches[0]
    return None

COM_HELPERS = """\
static void com_addref(void *p) {
    if (p) { typedef unsigned long (__stdcall *FN)(void*); ((FN)(*(void***)p)[1])(p); }
}
static void com_release(void *p) {
    if (p) { typedef unsigned long (__stdcall *FN)(void*); ((FN)(*(void***)p)[2])(p); }
}

"""

RELEASE_HELPERS = """\
/* Release COM resources held by a single cache entry and mark it inactive. */
static void DrawCache_ReleaseEntry(CachedDraw *c) {
    if (!c->active) return;
    com_release(c->vb);
    com_release(c->ib);
    com_release(c->decl);
    com_release(c->tex0);
    c->vb   = NULL;
    c->ib   = NULL;
    c->decl = NULL;
    c->tex0 = NULL;
    c->active = 0;
}

/* Release all COM resources held by the draw cache. */
static void DrawCache_ReleaseAll(void) {
    int i;
    for (i = 0; i < DRAW_CACHE_SIZE; i++)
        DrawCache_ReleaseEntry(&g_drawCache[i]);
}

"""

FRAME_RESET_SNIPPET = """\
    /* ----- start of new frame: release stale cache entries ----- */
    DrawCache_ReleaseAll();
    g_drawCacheHead = 0;
    /* ------------------------------------------------------------ */
"""


def _patch_add_com_helpers(src: str) -> str:
    """Insert COM helper stubs before the first static/extern function definition."""
    if "com_release" in src:
        return src  # already patched
    # Insert just before the first top-level function
    m = re.search(r'^(static|extern)\s+\w', src, re.MULTILINE)
    if m:
        return src[:m.start()] + COM_HELPERS + src[m.start():]
    return COM_HELPERS + src


def _patch_add_release_helpers(src: str) -> str:
    """Insert DrawCache_ReleaseEntry / DrawCache_ReleaseAll after COM helpers."""
    if "DrawCache_ReleaseEntry" in src:
        return src  # already patched
    # Insert right after com_release block
    idx = src.find("com_release")
    if idx < 0:
        return src
    # Find end of com_release function
    end_brace = src.find("\n}\n", idx)
    if end_brace < 0:
        return src
    insert_pos = end_brace + 3  # after closing brace + newline
    return src[:insert_pos] + RELEASE_HELPERS + src[insert_pos:]


def _patch_frame_reset(src: str) -> str:
    """Inject DrawCache_ReleaseAll() at the start of Present/BeginScene."""
    if "DrawCache_ReleaseAll" in src:
        return src  # already patched

    # Strategy: look for the body of Present or BeginScene
    # Heuristic: find a line like "HRESULT STDMETHODCALLTYPE ... Present(" or "BeginScene("
    for fn_name in ("Present", "BeginScene"):
        pattern = rf'HRESULT\s+STDMETHODCALLTYPE[^{{]+{fn_name}\s*\([^)]*\)\s*\{{'
        m = re.search(pattern, src, re.DOTALL)
        if m:
            insert_pos = m.end()
            return src[:insert_pos] + FRAME_RESET_SNIPPET + src[insert_pos:]
    return src


def apply_drawcache_crash_fix(device_c_path: Path) -> None:
    """Patch d3d9_device.c in-place with the DrawCache crash fix."""
    src = device_c_path.read_text(encoding="utf-8")
    original = src

    src = _patch_add_com_helpers(src)
    src = _patch_add_release_helpers(src)
    src = _patch_frame_reset(src)

    if src != original:
        device_c_path.write_text(src, encoding="utf-8")
        print(f"  Patched {device_c_path.name} with DrawCache crash fix")
    else:
        print(f"  {device_c_path.name} already patched (no changes)")


def deploy(arg: str) -> None:
    build_dir = find_build_dir(arg)

    if build_dir is not None:
        if not build_dir.is_dir():
            print(f"ERROR: Build directory not found: {build_dir}")
            sys.exit(1)

        print(f"\n=== Deploying build: {build_dir.name} ===")

        # Copy proxy source files -> patches/TRL/proxy/
        for src_file in build_dir.glob("*.c"):
            shutil.copy2(src_file, PROXY_DIR / src_file.name)
            print(f"  Copied {src_file.name}")
        for src_file in build_dir.glob("*.h"):
            shutil.copy2(src_file, PROXY_DIR / src_file.name)
            print(f"  Copied {src_file.name}")
        for src_file in build_dir.glob("*.def"):
            shutil.copy2(src_file, PROXY_DIR / src_file.name)
            print(f"  Copied {src_file.name}")

        # Copy proxy.ini if present
        src_ini = build_dir / "proxy.ini"
        if src_ini.exists():
            shutil.copy2(src_ini, PROXY_DIR / "proxy.ini")
            print("  Copied proxy.ini")

        # Copy rtx.conf if present in build root -> patches/TRL/rtx.conf (run.py deploys from there)
        src_rtx = build_dir / "rtx.conf"
        if src_rtx.exists():
            shutil.copy2(src_rtx, SCRIPT_DIR / "rtx.conf")
            print("  Copied rtx.conf")

        print(f"\n>>> Sources staged from: {build_dir.name}")

        # Apply crash fix to the staged d3d9_device.c
        print("\n--- Applying DrawCache crash fix ---")
        apply_drawcache_crash_fix(PROXY_DIR / "d3d9_device.c")
    else:
        print(">>> Using current proxy sources (no copy)")

    # Build -- try build.bat first; if it fails due to /GL+memcpy, fall back to _build.bat (no /GL)
    build_bat = PROXY_DIR / "build.bat"
    alt_bat   = PROXY_DIR / "_build.bat"
    print("\n=== Building ===")
    r = subprocess.run(["cmd.exe", "/c", build_bat.name], capture_output=True, text=True,
                       shell=False, cwd=str(PROXY_DIR))
    print(r.stdout)
    if r.returncode != 0:
        if "compiler predefined library helper" in r.stdout or "LNK1257" in r.stdout:
            print("  /GL+memcpy conflict -- retrying with _build.bat (no /GL)...")
            r = subprocess.run(["cmd.exe", "/c", alt_bat.name], capture_output=True, text=True,
                               shell=False, cwd=str(PROXY_DIR))
            print(r.stdout)
        if r.returncode != 0:
            print(f"BUILD FAILED:\n{r.stderr}")
            sys.exit(1)

    # Deploy
    dll_src = PROXY_DIR / "d3d9.dll"
    ini_src = PROXY_DIR / "proxy.ini"
    rtx_src = SCRIPT_DIR / "rtx.conf"

    shutil.copy2(dll_src, GAME_DIR / "d3d9.dll")
    shutil.copy2(ini_src, GAME_DIR / "proxy.ini")
    deployed = "d3d9.dll + proxy.ini"

    # Only seed rtx.conf when game dir has none; preserve runtime texture tags
    # (sky / UI / animatedWater / smoothNormals) the user set via Remix menu.
    game_rtx_conf = GAME_DIR / "rtx.conf"
    if rtx_src.exists() and not game_rtx_conf.exists():
        shutil.copy2(rtx_src, game_rtx_conf)
        deployed += " + rtx.conf (seeded)"
    elif game_rtx_conf.exists():
        deployed += " (preserved existing rtx.conf)"

    print(f"\n=== Deployed: {deployed} -> {GAME_DIR.name}/ ===")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    deploy(sys.argv[1])
