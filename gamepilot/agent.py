"""Main GamePilot agent — vision-driven game control loop."""
from __future__ import annotations

import signal
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = REPO_ROOT / "Tomb Raider Legend"

sys.path.insert(0, str(REPO_ROOT))

from gamepilot.capture import capture, image_to_bytes
from gamepilot.vision import GameState, classify_state, decide_action
from gamepilot.controller import execute_action
from gamepilot.states.handlers import pre_process
from gamepilot.session import Session
from livetools.gamectl import find_hwnd_by_exe

MAX_STEPS = 200
CAPTURE_INTERVAL = 0.5  # seconds between captures when idle
STUCK_THRESHOLD = 5     # same state+action this many times = stuck
MAX_CONSECUTIVE_ERRORS = 10  # abort after this many consecutive failures
MAX_UNKNOWN_STREAK = 8  # abort after this many consecutive UNKNOWN classifications


def _find_or_launch_game(session: Session | None = None) -> int | None:
    """Find the TRL game window, or launch it if not running.

    Returns hwnd or None.
    """
    hwnd = find_hwnd_by_exe("trl.exe")
    if hwnd:
        print(f"[agent] Found running game (hwnd={hwnd})")
        if session:
            session.log("game_found", hwnd=hwnd)
        return hwnd

    print("[agent] Game not running, launching...")
    if session:
        session.log("game_launching")

    launcher = GAME_DIR / "NvRemixLauncher32.exe"
    game_exe = GAME_DIR / "trl.exe"

    if launcher.exists():
        subprocess.Popen([str(launcher), str(game_exe)], cwd=str(GAME_DIR))
    else:
        subprocess.Popen([str(game_exe)], cwd=str(GAME_DIR))

    for attempt in range(90):
        hwnd = find_hwnd_by_exe("trl.exe")
        if hwnd:
            print(f"[agent] Game window found (hwnd={hwnd})")
            if session:
                session.log("game_started", hwnd=hwnd, wait_s=attempt)
            return hwnd
        time.sleep(1)

    print("[agent] ERROR: Game did not start within 90s")
    if session:
        session.log("game_launch_timeout")
    return None


def _detect_stuck(history: list[dict], threshold: int = STUCK_THRESHOLD) -> bool:
    """Detect if the agent is stuck repeating the same action."""
    if len(history) < threshold:
        return False
    recent = history[-threshold:]
    actions = [(h.get("action"), str(h.get("args", {}))) for h in recent]
    return len(set(actions)) == 1


def _kill_game_safe() -> None:
    """Kill trl.exe if running, suppressing errors."""
    try:
        subprocess.run(
            ["taskkill", "/f", "/im", "trl.exe"],
            capture_output=True, timeout=10,
        )
    except Exception:
        pass


def run_agent(
    goal: str,
    launch: bool = True,
    max_steps: int = MAX_STEPS,
    prefer_nvidia: bool = False,
    verbose: bool = True,
    session_dir: str | Path | None = None,
    dry_run: bool = False,
) -> dict:
    """Run the vision-controlled game agent.

    Args:
        goal: Natural language goal (e.g., "navigate to gameplay and open Remix menu").
        launch: If True, launch the game if not already running.
        max_steps: Maximum number of action steps before giving up.
        prefer_nvidia: Always use NVIDIA capture instead of GDI.
        verbose: Print step-by-step progress.
        session_dir: Directory for session artifacts. Auto-generated if None.
        dry_run: If True, skip game launch and use placeholder captures.

    Returns:
        Result dict with "success", "steps_taken", "final_state", "history", "session_dir".
    """
    session = Session(goal=goal, session_dir=session_dir, dry_run=dry_run)
    _shutdown_requested = False

    def _signal_handler(signum, frame):
        nonlocal _shutdown_requested
        if _shutdown_requested:
            sys.exit(1)  # second signal = force exit
        _shutdown_requested = True
        print("\n[agent] Shutdown requested — finishing current step...")
        if session:
            session.log("shutdown_requested", signal=signum)

    old_handler = signal.signal(signal.SIGINT, _signal_handler)

    try:
        return _run_agent_inner(
            goal=goal,
            launch=launch,
            max_steps=max_steps,
            prefer_nvidia=prefer_nvidia,
            verbose=verbose,
            dry_run=dry_run,
            session=session,
            is_shutdown=lambda: _shutdown_requested,
        )
    except Exception as e:
        session.log("agent_exception", error=str(e), type=type(e).__name__)
        result = {
            "success": False,
            "error": f"Exception: {e}",
            "steps_taken": session.step_count,
            "final_state": "exception",
            "history": [],
            "session_dir": str(session.session_dir),
        }
        session.write_summary(result)
        raise
    finally:
        signal.signal(signal.SIGINT, old_handler)
        session.close()


def _run_agent_inner(
    goal: str,
    launch: bool,
    max_steps: int,
    prefer_nvidia: bool,
    verbose: bool,
    dry_run: bool,
    session: Session,
    is_shutdown,
) -> dict:
    print("=" * 60)
    print(f"  GAMEPILOT — Vision-Controlled Game Agent")
    print(f"  Goal: {goal}")
    if dry_run:
        print(f"  Mode: DRY RUN (no game interaction)")
    print(f"  Session: {session.session_dir}")
    print("=" * 60)

    # Step 1: Find or launch game
    hwnd = None
    if not dry_run:
        if launch:
            hwnd = _find_or_launch_game(session)
        else:
            hwnd = find_hwnd_by_exe("trl.exe")

        if not hwnd:
            result = {"success": False, "error": "Game not running", "steps_taken": 0,
                      "history": [], "session_dir": str(session.session_dir)}
            session.write_summary(result)
            return result

    history: list[dict] = []
    consecutive_errors = 0
    unknown_streak = 0
    last_state = GameState.UNKNOWN

    for step in range(max_steps):
        if is_shutdown():
            print(f"\n[agent] Shutdown at step {step}")
            session.log("shutdown", step=step)
            result = {
                "success": False,
                "error": "Shutdown requested",
                "steps_taken": step,
                "final_state": last_state.value,
                "history": history,
                "session_dir": str(session.session_dir),
            }
            session.write_summary(result)
            return result

        session.step_count = step

        # Check game is still alive
        if not dry_run:
            hwnd_check = find_hwnd_by_exe("trl.exe")
            if not hwnd_check:
                print(f"\n[agent] Step {step}: Game window lost — crashed or closed")
                session.log("game_lost", step=step)
                result = {
                    "success": False,
                    "error": "Game window lost",
                    "steps_taken": step,
                    "final_state": GameState.CRASHED.value,
                    "history": history,
                    "session_dir": str(session.session_dir),
                }
                session.write_summary(result)
                return result
            hwnd = hwnd_check

        # Capture screenshot
        img = None
        if not dry_run:
            use_nvidia = prefer_nvidia or last_state in (GameState.GAMEPLAY, GameState.REMIX_MENU)
            try:
                img = capture(hwnd, prefer_nvidia=use_nvidia)
            except Exception as e:
                session.log("capture_error", step=step, error=str(e))
                consecutive_errors += 1

            if img is None:
                if verbose:
                    print(f"  Step {step}: Capture failed, retrying with NVIDIA...")
                try:
                    img = capture(hwnd, prefer_nvidia=True)
                except Exception as e:
                    session.log("capture_retry_error", step=step, error=str(e))

            if img is None:
                print(f"  Step {step}: All capture methods failed")
                session.log("capture_failed", step=step)
                consecutive_errors += 1
                if consecutive_errors >= MAX_CONSECUTIVE_ERRORS:
                    print(f"[agent] {MAX_CONSECUTIVE_ERRORS} consecutive errors — aborting")
                    session.log("too_many_errors", count=consecutive_errors)
                    result = {
                        "success": False,
                        "error": f"{consecutive_errors} consecutive errors",
                        "steps_taken": step,
                        "final_state": last_state.value,
                        "history": history,
                        "session_dir": str(session.session_dir),
                    }
                    session.write_summary(result)
                    return result
                time.sleep(2)
                continue

        # Save screenshot for the session record
        if img is not None:
            session.save_screenshot(img, f"step_{step:03d}")

        # Classify state
        if img is not None:
            img_bytes = image_to_bytes(img)
            try:
                state, details = classify_state(img_bytes)
                consecutive_errors = 0
            except Exception as e:
                session.log("classify_error", step=step, error=str(e))
                state, details = GameState.UNKNOWN, f"Classification failed: {e}"
                consecutive_errors += 1
        else:
            # Dry run: cycle through states for testing
            state = GameState.UNKNOWN
            details = "dry run — no capture"
            img_bytes = b""

        # Track unknown streaks
        if state == GameState.UNKNOWN:
            unknown_streak += 1
            if unknown_streak >= MAX_UNKNOWN_STREAK:
                print(f"[agent] {MAX_UNKNOWN_STREAK} consecutive UNKNOWN states — aborting")
                session.log("unknown_streak", count=unknown_streak)
                result = {
                    "success": False,
                    "error": f"{unknown_streak} consecutive UNKNOWN classifications",
                    "steps_taken": step,
                    "final_state": state.value,
                    "history": history,
                    "session_dir": str(session.session_dir),
                }
                session.write_summary(result)
                return result
        else:
            unknown_streak = 0

        session.log("classify", step=step, state=state.value, details=details)

        if verbose:
            state_changed = state != last_state
            marker = " ***" if state_changed else ""
            print(f"\n  Step {step}: State={state.value}{marker}")
            print(f"    Details: {details}")

        last_state = state

        # Stuck detection
        if _detect_stuck(history):
            print(f"[agent] Stuck — same action repeated {STUCK_THRESHOLD} times")
            session.log("stuck_detected", step=step, last_action=history[-1] if history else None)
            # Force a different action: try ESCAPE to unstick
            unstick_action = {
                "action": "key",
                "args": {"name": "ESCAPE"},
                "reasoning": "Stuck detection — trying ESCAPE to change state",
            }
            if not dry_run:
                execute_action(hwnd, unstick_action)
            history.append(unstick_action)
            session.log("unstick_attempt", action=unstick_action)
            time.sleep(1)
            continue

        # Pre-process: some states are handled without Claude
        pre_action = pre_process(state, hwnd)
        if pre_action:
            if verbose:
                print(f"    Auto: {pre_action.get('reasoning', '')}")
            session.log("pre_action", step=step, action=pre_action)
            if not dry_run:
                execute_action(hwnd, pre_action)
            history.append(pre_action)

            if state == GameState.LOADING:
                # Don't count loading waits against stuck detection
                pass
            continue

        # Ask Claude for action
        if dry_run:
            action = {
                "action": "wait",
                "args": {"ms": 500},
                "reasoning": "Dry run — no Claude call",
            }
        else:
            try:
                action = decide_action(img_bytes, state, goal, history)
                consecutive_errors = 0
            except Exception as e:
                session.log("decide_error", step=step, error=str(e))
                action = {
                    "action": "wait",
                    "args": {"ms": 2000},
                    "reasoning": f"Decision failed: {e}",
                }
                consecutive_errors += 1

        if verbose:
            print(f"    Action: {action.get('action')} {action.get('args', {})}")
            print(f"    Reason: {action.get('reasoning', '')}")

        session.log("action", step=step, **action)

        # Check for goal completion
        if action.get("action") == "goal_complete":
            print(f"\n  GOAL COMPLETE at step {step}")
            print(f"    Reason: {action.get('reasoning', '')}")
            result = {
                "success": True,
                "steps_taken": step,
                "final_state": state.value,
                "history": history,
                "session_dir": str(session.session_dir),
            }
            session.write_summary(result)
            return result

        # Execute action
        if not dry_run:
            try:
                action_result = execute_action(hwnd, action)
                action["result"] = action_result
            except Exception as e:
                session.log("execute_error", step=step, error=str(e))
                action["result"] = {"ok": False, "error": str(e)}
                consecutive_errors += 1

        history.append(action)
        time.sleep(CAPTURE_INTERVAL)

    print(f"\n[agent] Reached max steps ({max_steps}) without completing goal")
    session.log("max_steps_reached", max_steps=max_steps)
    result = {
        "success": False,
        "error": "Max steps reached",
        "steps_taken": max_steps,
        "final_state": last_state.value,
        "history": history,
        "session_dir": str(session.session_dir),
    }
    session.write_summary(result)
    return result
