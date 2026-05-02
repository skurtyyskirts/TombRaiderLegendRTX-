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

/* Release all cached resources and empty the cache. */
static void DrawCache_Clear(void) {
    int i;
    for (i = 0; i < s_drawCacheCount; i++)
        DrawCache_ReleaseEntry(&s_drawCache[i]);
    s_drawCacheCount = 0;
    s_cacheLogOnce   = 0;
}

"""

def apply_drawcache_crash_fix(src_path: Path) -> bool:
    """Patch d3d9_device.c with the build-077 DrawCache use-after-free fix.

    Returns True if any changes were made, False if already fixed or no DrawCache.
    """
    text = src_path.read_text(encoding='utf-8', errors='replace')
    changed = False

    # Skip files that have no DrawCache (builds 069-071)
    if 'DrawCache_Record' not in text:
        print("  [crash-fix] No DrawCache found — skipping (builds 069-071 don't have it)")
        return False

    # Skip if already patched
    if 'DrawCache_ReleaseEntry' in text:
        print("  [crash-fix] Already patched — skipping")
        return False

    # 1. Add com_addref/com_release if missing (builds 064-065)
    if 'com_addref' not in text:
        anchor = '/* ---- Draw call cache ---- */'
        if anchor in text:
            text = text.replace(anchor, COM_HELPERS + anchor)
            changed = True
            print("  [crash-fix] Inserted com_addref/com_release")

    # 2. Insert DrawCache_ReleaseEntry + DrawCache_Clear before DrawCache_Record
    anchor = 'static void DrawCache_Record('
    if anchor in text and 'DrawCache_ReleaseEntry' not in text:
        text = text.replace(anchor, RELEASE_HELPERS + anchor)
        changed = True
        print("  [crash-fix] Inserted DrawCache_ReleaseEntry + DrawCache_Clear")

    # 3. Remove the immediate IB release in DrawCache_Record
    old_ib = (
        '    /* Get current index buffer */\n'
        '    {\n'
        '        typedef int (__stdcall *FN_GetIndices)(void*, void**);\n'
        '        ((FN_GetIndices)RealVtbl(self)[SLOT_GetIndices])(self->pReal, &ib);\n'
        '        if (ib) {\n'
        '            /* GetIndices AddRefs, release the extra ref */\n'
        '            typedef unsigned long (__stdcall *FN_Rel)(void*);\n'
        '            ((FN_Rel)(*(void***)ib)[2])(ib);\n'
        '        }\n'
        '    }'
    )
    new_ib = (
        '    /* Get current index buffer \u2014 GetIndices AddRefs; keep that ref as our cache ref */\n'
        '    {\n'
        '        typedef int (__stdcall *FN_GetIndices)(void*, void**);\n'
        '        ((FN_GetIndices)RealVtbl(self)[SLOT_GetIndices])(self->pReal, &ib);\n'
        '    }'
    )
    if old_ib in text:
        text = text.replace(old_ib, new_ib)
        changed = True
        print("  [crash-fix] Removed immediate IB release in DrawCache_Record")

    # 4. Handle cache-full: release IB before returning
    old_full = '    if (slot < 0) return; /* cache full */'
    new_full = (
        '    if (slot < 0) {\n'
        '        /* Cache full \u2014 ib was AddRef\u2019d by GetIndices, must release */\n'
        '        com_release(ib);\n'
        '        return;\n'
        '    }'
    )
    if old_full in text:
        text = text.replace(old_full, new_full)
        changed = True
        print("  [crash-fix] Fixed cache-full path to release IB")

    # 5. Add DrawCache_ReleaseEntry call + AddRef vb/ib on slot write
    old_slot = (
        '        CachedDraw *c = &s_drawCache[slot];\n'
        '        float wvp_row[16];\n'
        '        c->vb = vb;\n'
        '        c->ib = ib;'
    )
    new_slot = (
        '        CachedDraw *c = &s_drawCache[slot];\n'
        '        float wvp_row[16];\n\n'
        '        /* Release previously held resources before overwriting the slot */\n'
        '        if (c->active) DrawCache_ReleaseEntry(c);\n\n'
        '        c->vb = vb;   com_addref(c->vb);\n'
        '        c->ib = ib;   /* already AddRef\u2019d by GetIndices above */'
    )
    if old_slot in text:
        text = text.replace(old_slot, new_slot)
        changed = True
        print("  [crash-fix] Added ReleaseEntry + com_addref(vb/ib) on slot write")

    # 6. AddRef decl and tex0
    old_decl = (
        '        c->decl = self->lastDecl;\n'
        '        c->tex0 = self->curTexture[self->albedoStage];'
    )
    new_decl = (
        '        c->decl = self->lastDecl; com_addref(c->decl);\n'
        '        c->tex0 = self->curTexture[self->albedoStage]; com_addref(c->tex0);'
    )
    if old_decl in text:
        text = text.replace(old_decl, new_decl)
        changed = True
        print("  [crash-fix] Added com_addref(decl/tex0) on slot write")

    # 7. Fix stale eviction in DrawCache_Replay
    old_stale = '            c->active = 0; /* stale, evict */'
    new_stale = '            DrawCache_ReleaseEntry(c); /* stale \u2014 release COM refs and mark inactive */'
    if old_stale in text:
        text = text.replace(old_stale, new_stale)
        changed = True
        print("  [crash-fix] Fixed stale eviction to call DrawCache_ReleaseEntry")

    # 8a. WD_Release: add DrawCache_Clear for builds that have no cleanup yet
    old_heap = '        HeapFree(GetProcessHeap(), 0, self);'
    new_heap = (
        '#if DRAW_CACHE_ENABLED\n'
        '        DrawCache_Clear();\n'
        '#endif\n'
        '        HeapFree(GetProcessHeap(), 0, self);'
    )
    if old_heap in text and 'DrawCache_Clear' not in text.split(old_heap)[0].split('static unsigned long __stdcall WD_Release')[-1]:
        text = text.replace(old_heap, new_heap, 1)
        changed = True
        print("  [crash-fix] Added DrawCache_Clear to WD_Release")

    # 8b. WD_Release/WD_Reset/WD_BeginScene: replace raw s_drawCacheCount=0 blocks (build-076 pattern)
    old_raw_release = (
        '        /* Clear static draw cache \u2014 raw COM pointers dangle after device destroy */\n'
        '        s_drawCacheCount = 0;\n'
        '        s_cacheLogOnce = 0;'
    )
    new_raw_release = (
        '        /* Clear draw cache \u2014 releases COM refs held for anchor mesh replay */\n'
        '#if DRAW_CACHE_ENABLED\n'
        '        DrawCache_Clear();\n'
        '#endif'
    )
    if old_raw_release in text:
        text = text.replace(old_raw_release, new_raw_release)
        changed = True
        print("  [crash-fix] Replaced raw s_drawCacheCount=0 in WD_Release with DrawCache_Clear")

    old_raw_reset = (
        '    /* Clear static draw cache \u2014 raw COM pointers are invalidated by Reset */\n'
        '    s_drawCacheCount = 0;\n'
        '    s_cacheLogOnce = 0;'
    )
    new_raw_reset = (
        '    /* Clear draw cache \u2014 releases COM refs before Reset invalidates device resources */\n'
        '#if DRAW_CACHE_ENABLED\n'
        '    DrawCache_Clear();\n'
        '#endif'
    )
    if old_raw_reset in text:
        text = text.replace(old_raw_reset, new_raw_reset)
        changed = True
        print("  [crash-fix] Replaced raw s_drawCacheCount=0 in WD_Reset with DrawCache_Clear")

    old_raw_begin = (
        '        /* Flush any draws cached during loading screens \u2014 they carry\n'
        '         * wrong world matrices for the gameplay scene and would render\n'
        '         * as deformed geometry for ~120 frames until naturally evicted. */\n'
        '        s_drawCacheCount = 0;\n'
        '        s_cacheLogOnce   = 0;'
    )
    new_raw_begin = (
        '        /* Flush draws cached during loading screens \u2014 they carry wrong world matrices\n'
        '         * for the gameplay scene. Also releases their COM refs so freed menu resources\n'
        '         * aren\u2019t replayed after the transition. */\n'
        '#if DRAW_CACHE_ENABLED\n'
        '        DrawCache_Clear();\n'
        '#endif'
    )
    if old_raw_begin in text:
        text = text.replace(old_raw_begin, new_raw_begin)
        changed = True
        print("  [crash-fix] Replaced raw s_drawCacheCount=0 in WD_BeginScene with DrawCache_Clear")

    if changed:
        src_path.write_text(text, encoding='utf-8')
        print(f"  [crash-fix] Patched {src_path.name}")
    else:
        print("  [crash-fix] No matching patterns found — file may already be patched or use different code")

    return changed


def deploy(build_arg: str):
    build_dir = find_build_dir(build_arg)

    if build_dir is not None:
        proxy_src = build_dir / "proxy"
        if not proxy_src.is_dir():
            print(f"ERROR: No proxy/ subfolder in {build_dir.name}")
            sys.exit(1)

        # Copy .c source files
        for f in proxy_src.glob("*.c"):
            shutil.copy2(f, PROXY_DIR / f.name)
            print(f"  Copied {f.name}")

        # Copy proxy.ini if present in build's proxy folder
        src_ini = proxy_src / "proxy.ini"
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

    # Build — try build.bat first; if it fails due to /GL+memcpy, fall back to _build.bat (no /GL)
    build_bat = PROXY_DIR / "build.bat"
    alt_bat   = PROXY_DIR / "_build.bat"
    print("\n=== Building ===")
    r = subprocess.run(["cmd.exe", "/c", build_bat.name], capture_output=True, text=True,
                       shell=False, cwd=str(PROXY_DIR))
    print(r.stdout)
    if r.returncode != 0:
        if "compiler predefined library helper" in r.stdout or "LNK1257" in r.stdout:
            print("  /GL+memcpy conflict — retrying with _build.bat (no /GL)...")
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

    print(f"\n=== Deployed {deployed} to {GAME_DIR.name}/ ===")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    deploy(sys.argv[1])
