"""Main autonomous loop — diagnose, hypothesize, patch, test, evaluate, iterate."""
from __future__ import annotations

import json
import subprocess
import shutil
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = REPO_ROOT / "Tomb Raider Legend"
MACROS_FILE = Path(__file__).resolve().parent / "macros.json"
SCREENSHOTS_SRC = Path(r"C:\Users\skurtyy\Videos\NVIDIA\Tomb Raider  Legend")

sys.path.insert(0, str(REPO_ROOT))

MAX_ITERATIONS = 10


def _collect_screenshots(after_ts: float, limit: int = 3) -> list[Path]:
    """Collect NVIDIA screenshots created after a given timestamp.

    Args:
        after_ts: Only include files modified after this Unix timestamp.
        limit: Maximum number of screenshots to return.
    """
    if not SCREENSHOTS_SRC.exists():
        return []
    files = sorted(SCREENSHOTS_SRC.iterdir(),
                   key=lambda f: f.stat().st_mtime, reverse=True)
    return [
        f for f in files
        if f.suffix.lower() in (".png", ".jpg", ".bmp")
        and f.stat().st_mtime > after_ts
    ][:limit]


def _kill_game() -> None:
    subprocess.run(["taskkill", "/f", "/im", "trl.exe"], capture_output=True)
    time.sleep(2)


LEVEL_LOAD_FN = 0x00451970   # GAMELOOP_RequestLevelChangeByName
GAME_TRACKER   = "0x010E5370"  # GameTracker global struct address
FRAME_COUNTER  = "0x010E5424"  # g_frameCounter (incremented each frame)
DEFAULT_LEVEL  = "flashback1"  # Peru (Return to Paraiso) — DRM name from ChapterSpec


def _load_level_via_frida(level_name: str) -> bool:
    """Request a level change by calling GAMELOOP_RequestLevelChangeByName.

    Requires livetools to be attached already. Allocates a string buffer in
    the target process, writes the level name, and calls the function.
    """
    from livetools import client

    alloc_resp = client.send_command({"cmd": "mem_alloc", "size": 64})
    if not alloc_resp.get("ok"):
        print(f"  [load_level] Failed to allocate: {alloc_resp.get('msg')}")
        return False

    str_addr = "0x" + alloc_resp["addr"].lstrip("0x")
    name_hex = level_name.encode("ascii").hex() + "00"
    client.send_command({"cmd": "mem_write", "addr": str_addr, "hex": name_hex})

    resp = client.send_command({
        "cmd": "call",
        "addr": hex(LEVEL_LOAD_FN),
        "abi": "default",
        "retType": "void",
        "argTypes": ["pointer", "pointer", "int32"],
        "argValues": [str_addr, GAME_TRACKER, 4],
    })
    if not resp.get("ok"):
        print(f"  [load_level] Call failed: {resp.get('msg')}")
        return False

    print(f"  [load_level] Level load requested: '{level_name}'")
    return True


def _wait_for_level_loaded(timeout: int = 60) -> bool:
    """Wait for a level to finish loading after RequestLevelChangeByName.

    Polls g_pCurrentScene — when it changes from its pre-load value, the
    new level's scene object has been created and gameplay can begin.
    Falls back to a fixed delay if polling fails.
    """
    from livetools import client

    # Read initial scene pointer
    SCENE_PTR = "0x010E537C"  # g_pCurrentScene
    initial = client.send_command({"cmd": "mem_read", "addr": SCENE_PTR, "size": 4})
    initial_hex = initial.get("hex", "") if initial.get("ok") else ""

    deadline = time.time() + timeout
    while time.time() < deadline:
        time.sleep(3)
        current = client.send_command({"cmd": "mem_read", "addr": SCENE_PTR, "size": 4})
        if not current.get("ok"):
            continue
        if current["hex"] != initial_hex and current["hex"] != "00000000":
            print(f"  [load_level] Scene changed: {initial_hex} -> {current['hex']}")
            time.sleep(5)  # extra settle time after scene loads
            return True

    print("  [load_level] Timeout — using fixed delay fallback")
    return True  # proceed anyway, the level may have loaded


def _launch_game() -> int | None:
    """Launch TRL directly into Peru via TR7.arg, skip cutscene, return hwnd."""
    from livetools.gamectl import find_hwnd_by_exe, get_window_info, send_keys

    sys.path.insert(0, str(REPO_ROOT / "patches" / "TombRaiderLegend"))
    from run import set_graphics_config, dismiss_setup_dialog, write_tr7_arg
    set_graphics_config()
    write_tr7_arg(chapter=4)

    launcher = GAME_DIR / "NvRemixLauncher32.exe"
    game_exe = GAME_DIR / "trl.exe"

    if launcher.exists():
        subprocess.Popen([str(launcher), str(game_exe)], cwd=str(GAME_DIR))
    else:
        subprocess.Popen([str(game_exe)], cwd=str(GAME_DIR))

    # Wait for game window, dismissing setup dialog if it appears
    print("  Waiting for game window...")
    hwnd = None
    setup_dismissed = False
    for i in range(90):
        if not setup_dismissed and dismiss_setup_dialog():
            setup_dismissed = True
            print("  Setup dialog configured and dismissed")
            time.sleep(3)
            continue

        hwnd = find_hwnd_by_exe("trl.exe")
        if hwnd:
            info = get_window_info(hwnd)
            if "Setup" not in info["title"]:
                break
            else:
                dismiss_setup_dialog()
                hwnd = None
        time.sleep(1)

    if not hwnd:
        print("  ERROR: Game did not start within 90s")
        return None

    print(f"  Game window found (hwnd={hwnd})")

    # Wait for game to load (D3D init, Remix init, shader compilation)
    print("  Waiting 20s for game to initialize...")
    time.sleep(20)

    # Skip the Peru cutscene (3s wait, then ESC → W → ENTER)
    print("  Skipping cutscene...")
    time.sleep(3)
    send_keys(hwnd, "ESCAPE WAIT:1550 W WAIT:1550 RETURN")

    # Wait for gameplay to start
    print("  Waiting 5s for gameplay to start...")
    time.sleep(5)

    # Verify game is still alive
    if not find_hwnd_by_exe("trl.exe"):
        print("  ERROR: Game exited during level load")
        return None

    print("  [navigate] Peru level loaded, Lara in gameplay")
    return hwnd


def _run_eval_macro(hwnd: int) -> list[Path]:
    """Run the 3-position evaluation macro with crash detection between steps."""
    from livetools.gamectl import send_keys, load_macros, find_hwnd_by_exe

    macros = load_macros(str(MACROS_FILE))
    if "eval_3pos" not in macros:
        print("[orchestrator] ERROR: eval_3pos macro not found")
        return []

    before_ts = time.time()
    steps = macros["eval_3pos"]["steps"]

    # Run steps individually so we can bail early on crash
    for step in steps:
        if find_hwnd_by_exe("trl.exe") is None:
            print("  [macro] Game died mid-macro, aborting remaining steps")
            return _collect_screenshots(after_ts=before_ts, limit=3)
        send_keys(hwnd, step, delay_ms=0)

    time.sleep(3)
    return _collect_screenshots(after_ts=before_ts, limit=3)


def run(skip_diagnosis: bool = False, dry_run: bool = False) -> None:
    """Run the autonomous patch loop.

    Args:
        skip_diagnosis: Skip the diagnostic capture phase (use existing data).
        dry_run: Validate components without patching or launching the game.
    """
    from autopatch.evaluator import calibrate, evaluate_screenshots
    from autopatch.knowledge import KnowledgeBase, IterationRecord
    from autopatch.safety import backup_proxy
    from autopatch.hypothesis import generate_from_diagnostic
    from autopatch.patcher import (
        attach_livetools, detach_livetools,
        apply_runtime, revert_runtime,
        promote_to_source, build_and_deploy,
    )
    from autopatch.diagnose import run_diagnostic

    print("=" * 60)
    print("  AUTOPATCH — Autonomous Light Visibility Solver")
    print("=" * 60)

    # Step 1: Load knowledge base
    kb = KnowledgeBase.load()
    print(f"\n[1] Knowledge base loaded: {len(kb.iterations)} iterations, "
          f"{len(kb.tried_addrs)} tried addresses")

    # Step 2: Validate evaluator
    print("\n[2] Calibrating screenshot evaluator...")
    if not calibrate():
        print("ABORT: Evaluator calibration failed")
        return

    if dry_run:
        print("\n[DRY RUN] All components validated. Exiting.")
        return

    # Step 3: Diagnostic capture (or load existing)
    diag_report_path = Path(__file__).resolve().parent / "diagnostic_captures" / "diagnostic_report.json"
    if not skip_diagnosis or not diag_report_path.exists():
        print("\n[3] Running diagnostic capture (near vs far)...")
        _kill_game()
        diagnostic_result = run_diagnostic()
        if "error" in diagnostic_result:
            print(f"ABORT: Diagnostic failed — {diagnostic_result['error']}")
            return
        kb.record_diagnostic(diagnostic_result)
    else:
        print("\n[3] Loading existing diagnostic data...")
        diagnostic_result = json.loads(diag_report_path.read_text())

    missing_count = diagnostic_result.get("total_missing_draws", 0)
    caller_count = len(diagnostic_result.get("unique_caller_addrs", []))
    print(f"    Missing draws at distance: {missing_count}")
    print(f"    Unique caller addresses: {caller_count}")

    if missing_count == 0:
        print("\nNo draws missing at distance — geometry is not being culled.")
        print("The lights may disappear for a different reason (hash instability?).")
        return

    # Step 4: Generate hypotheses
    print("\n[4] Generating patch hypotheses...")
    hypotheses = generate_from_diagnostic(
        diagnostic_result,
        tried_addrs=kb.tried_addrs,
        blacklisted_addrs=kb.blacklisted_addrs,
    )
    print(f"    Generated {len(hypotheses)} hypotheses")

    if not hypotheses:
        print("No new hypotheses to try — all candidate addresses exhausted.")
        print("Consider manual analysis of the diagnostic data.")
        return

    for h in hypotheses:
        print(f"    {h.id}: {h.description} (confidence={h.confidence:.2f})")

    # Step 5: Patch & test loop
    print(f"\n[5] Starting patch loop (max {MAX_ITERATIONS} iterations)...")
    _kill_game()
    session_failures = 0

    for i, hypothesis in enumerate(hypotheses[:MAX_ITERATIONS]):
        if session_failures >= MAX_ITERATIONS:
            print(f"\n  {MAX_ITERATIONS} consecutive failures this session "
                  f"— pausing for human review.")
            break

        iter_id = kb.next_iteration_id()
        print(f"\n{'=' * 50}")
        print(f"  Iteration {iter_id}: {hypothesis.description}")
        print(f"{'=' * 50}")

        # 5a: Launch game with proxy
        print("  Launching game...")
        hwnd = _launch_game()
        if not hwnd:
            print("  ERROR: Game failed to launch")
            kb.record_iteration(IterationRecord(
                id=iter_id, timestamp=time.time(),
                hypothesis_id=hypothesis.id,
                description=hypothesis.description,
                target_addr=hypothesis.target_addr,
                patch_bytes=hypothesis.patch_bytes.hex(),
                patch_type="runtime", passed=False, crashed=True,
                confidence=hypothesis.confidence,
                notes="Game failed to launch",
            ))
            session_failures += 1
            continue

        # 5b: Attach livetools
        if not attach_livetools():
            print("  ERROR: Failed to attach livetools")
            _kill_game()
            kb.record_iteration(IterationRecord(
                id=iter_id, timestamp=time.time(),
                hypothesis_id=hypothesis.id,
                description=hypothesis.description,
                target_addr=hypothesis.target_addr,
                patch_bytes=hypothesis.patch_bytes.hex(),
                patch_type="runtime", passed=False, crashed=True,
                confidence=hypothesis.confidence,
                notes="Failed to attach livetools",
            ))
            session_failures += 1
            continue

        # 5c: Apply runtime patch
        print(f"  Applying patch: {hex(hypothesis.target_addr)} <- "
              f"{hypothesis.patch_bytes.hex()}")
        patch_ok = apply_runtime(hypothesis.target_addr, hypothesis.patch_bytes)
        if not patch_ok:
            detach_livetools()
            _kill_game()
            kb.record_iteration(IterationRecord(
                id=iter_id, timestamp=time.time(),
                hypothesis_id=hypothesis.id,
                description=hypothesis.description,
                target_addr=hypothesis.target_addr,
                patch_bytes=hypothesis.patch_bytes.hex(),
                patch_type="runtime", passed=False, crashed=False,
                confidence=hypothesis.confidence,
                notes="Failed to write patch bytes",
            ))
            session_failures += 1
            continue

        # 5d: Run evaluation macro
        time.sleep(2)
        print("  Running 3-position evaluation macro...")
        screenshots = _run_eval_macro(hwnd)

        # 5e: Check if game crashed
        from livetools.gamectl import find_hwnd_by_exe
        game_alive = find_hwnd_by_exe("trl.exe") is not None

        if not game_alive:
            print("  CRASH — game exited during macro")
            kb.record_iteration(IterationRecord(
                id=iter_id, timestamp=time.time(),
                hypothesis_id=hypothesis.id,
                description=hypothesis.description,
                target_addr=hypothesis.target_addr,
                patch_bytes=hypothesis.patch_bytes.hex(),
                patch_type="runtime", passed=False, crashed=True,
                confidence=hypothesis.confidence,
                notes="Game crashed during evaluation",
            ))
            _kill_game()
            session_failures += 1
            continue

        # 5f: Detach and evaluate
        detach_livetools()

        if not screenshots:
            print("  WARNING: No screenshots collected")
            _kill_game()
            kb.record_iteration(IterationRecord(
                id=iter_id, timestamp=time.time(),
                hypothesis_id=hypothesis.id,
                description=hypothesis.description,
                target_addr=hypothesis.target_addr,
                patch_bytes=hypothesis.patch_bytes.hex(),
                patch_type="runtime", passed=False, crashed=False,
                confidence=hypothesis.confidence,
                notes="No screenshots captured",
            ))
            session_failures += 1
            continue

        verdict = evaluate_screenshots(screenshots)
        print(f"  Verdict: passed={verdict.passed}, "
              f"red={verdict.red_visible}, green={verdict.green_visible}, "
              f"confidence={verdict.confidence:.2f}")

        _kill_game()

        # 5g: Record result
        kb.record_iteration(IterationRecord(
            id=iter_id, timestamp=time.time(),
            hypothesis_id=hypothesis.id,
            description=hypothesis.description,
            target_addr=hypothesis.target_addr,
            patch_bytes=hypothesis.patch_bytes.hex(),
            patch_type="runtime",
            passed=verdict.passed,
            crashed=False,
            confidence=float(verdict.confidence),
            notes=f"red={verdict.red_visible} green={verdict.green_visible}",
        ))

        if not verdict.passed:
            session_failures += 1

        # 5h: If PASS — promote and finalize
        if verdict.passed:
            print(f"\n  *** PASS — {hypothesis.description} ***")
            print("  Promoting to proxy source and rebuilding...")

            backup_proxy(iter_id)
            promote_to_source(
                hypothesis.target_addr,
                hypothesis.patch_bytes,
                hypothesis.description,
            )
            if build_and_deploy():
                print("  Proxy rebuilt and deployed with winning patch.")
                print("\n  Run full verification:")
                print("    python patches/TombRaiderLegend/run.py test --build --randomize")
            else:
                print("  WARNING: Proxy build failed after promotion")
            return

    # Summary
    print("\n" + "=" * 60)
    print("  AUTOPATCH COMPLETE — No winning patch found")
    print("=" * 60)
    print(f"\n  Iterations run: {len(kb.iterations)}")
    print(f"  Diagnostic data: {diag_report_path}")
    print(f"  Knowledge base: {kb.KNOWLEDGE_FILE}")
    print("\n  Next steps:")
    print("  1. Review diagnostic_captures/diagnostic_report.json")
    print("  2. Manually inspect the caller addresses with retools")
    print("  3. Add manual hypotheses and re-run")
