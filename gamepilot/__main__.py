"""CLI entry point: python -m gamepilot <goal> [OPTIONS]"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))


def main():
    parser = argparse.ArgumentParser(
        description="GamePilot — Vision-controlled game agent using Claude API",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
Examples:
  python -m gamepilot "load the Bolivia level and walk to the stage"
  python -m gamepilot "open Remix menu and enable debug view 277"
  python -m gamepilot "switch to debug runtime and relaunch"
  python -m gamepilot "navigate to gameplay" --no-launch
  python -m gamepilot "check if stage lights are visible" --nvidia

Utility commands (no goal needed):
  python -m gamepilot --health            Run preflight health checks
  python -m gamepilot --swap-debug        Switch to debug Remix runtime
  python -m gamepilot --swap-regular      Switch to regular Remix runtime
  python -m gamepilot --runtime-info      Show active runtime info

Stability options:
  python -m gamepilot "goal" --dry-run    Test pipeline without game
  python -m gamepilot "goal" --session-dir ./my_session
""",
    )
    parser.add_argument(
        "goal", nargs="?", default=None,
        help="Natural language goal for the agent",
    )
    parser.add_argument(
        "--no-launch", action="store_true",
        help="Don't auto-launch the game (require it to already be running)",
    )
    parser.add_argument(
        "--nvidia", action="store_true",
        help="Always use NVIDIA capture (slower but guaranteed accurate)",
    )
    parser.add_argument(
        "--max-steps", type=int, default=200,
        help="Maximum action steps before giving up (default: 200)",
    )
    parser.add_argument(
        "--quiet", action="store_true",
        help="Suppress step-by-step output",
    )
    parser.add_argument(
        "--dry-run", action="store_true",
        help="Test the agent pipeline without launching the game or calling Claude",
    )
    parser.add_argument(
        "--session-dir", type=str, default=None,
        help="Directory for session artifacts (auto-generated if not set)",
    )
    parser.add_argument(
        "--health", action="store_true",
        help="Run preflight health checks and exit",
    )
    parser.add_argument(
        "--swap-debug", action="store_true",
        help="Switch to debug Remix runtime",
    )
    parser.add_argument(
        "--swap-regular", action="store_true",
        help="Switch to regular Remix runtime",
    )
    parser.add_argument(
        "--runtime-info", action="store_true",
        help="Show which Remix runtime is active",
    )

    args = parser.parse_args()

    # Health check command
    if args.health:
        from gamepilot.health import run_all_checks
        _, all_passed = run_all_checks(verbose=True)
        sys.exit(0 if all_passed else 1)

    # Runtime management commands
    if args.swap_debug:
        from gamepilot.remix import swap_to_debug
        success = swap_to_debug()
        sys.exit(0 if success else 1)

    if args.swap_regular:
        from gamepilot.remix import swap_to_regular
        success = swap_to_regular()
        sys.exit(0 if success else 1)

    if args.runtime_info:
        from gamepilot.remix import get_active_runtime, GAME_DIR, DEBUG_RUNTIME
        runtime = get_active_runtime()
        print(f"Active runtime: {runtime}")
        print(f"Game directory:  {GAME_DIR}")
        print(f"Debug source:    {DEBUG_RUNTIME}")
        sys.exit(0)

    if not args.goal and not args.dry_run:
        parser.print_help()
        print("\nError: a goal is required (or use --dry-run, --health, --swap-debug, etc.)")
        sys.exit(1)

    goal = args.goal or "dry-run test"

    # Run preflight checks before starting (non-blocking warnings only)
    if not args.dry_run:
        from gamepilot.health import run_all_checks
        _, all_passed = run_all_checks(verbose=True)
        if not all_passed:
            print("Fix the above errors before running the agent.")
            print("Use --dry-run to test the pipeline without the game.")
            sys.exit(1)

    from gamepilot.agent import run_agent
    result = run_agent(
        goal=goal,
        launch=not args.no_launch,
        max_steps=args.max_steps,
        prefer_nvidia=args.nvidia,
        verbose=not args.quiet,
        session_dir=args.session_dir,
        dry_run=args.dry_run,
    )

    print(f"\n{'=' * 60}")
    print(f"  Result: {'SUCCESS' if result['success'] else 'FAILED'}")
    print(f"  Steps: {result['steps_taken']}")
    print(f"  Final state: {result.get('final_state', 'unknown')}")
    if result.get("session_dir"):
        print(f"  Session: {result['session_dir']}")
    if not result["success"]:
        print(f"  Error: {result.get('error', 'unknown')}")
    print(f"{'=' * 60}")

    sys.exit(0 if result["success"] else 1)


if __name__ == "__main__":
    main()
