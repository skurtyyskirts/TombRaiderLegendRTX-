"""Claude vision client — routes through Claude Code CLI to use Max plan credits.

Instead of calling the Anthropic API directly (which requires separate billing),
this module shells out to `claude -p --bare --model sonnet` with screenshots saved
as temp files. Claude Code's Read tool can view images natively.
"""
from __future__ import annotations

import json
import subprocess
import tempfile
import time
import os
from enum import Enum
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# Use sonnet for speed/cost. --bare skips CLAUDE.md loading.
# --tools Read allows reading the screenshot image.
CLAUDE_BASE_CMD = [
    "claude", "-p", "--bare",
    "--model", "sonnet",
    "--tools", "Read",
    "--output-format", "json",
]


class GameState(str, Enum):
    SETUP_DIALOG = "setup_dialog"
    MAIN_MENU = "main_menu"
    LOADING = "loading"
    GAMEPLAY = "gameplay"
    PAUSE_MENU = "pause_menu"
    REMIX_MENU = "remix_menu"
    CRASHED = "crashed"
    UNKNOWN = "unknown"


MAX_RETRIES = 2
RETRY_DELAY = 1.0  # seconds between retries


def _call_claude(prompt: str, image_path: str | None = None, timeout: int = 30) -> str:
    """Call Claude CLI and return the text response.

    Retries on transient failures (timeout, non-zero exit) up to MAX_RETRIES times.

    Args:
        prompt: The prompt text to send.
        image_path: Optional path to an image file for Claude to read.
        timeout: Max seconds to wait for response.

    Returns:
        Claude's text response.

    Raises:
        RuntimeError: If all retries exhausted.
    """
    if image_path:
        full_prompt = f"First, read the image at {image_path} and view it. Then:\n\n{prompt}"
    else:
        full_prompt = prompt

    last_error = None
    for attempt in range(1 + MAX_RETRIES):
        try:
            result = subprocess.run(
                CLAUDE_BASE_CMD,
                input=full_prompt,
                capture_output=True,
                text=True,
                timeout=timeout,
                cwd=str(REPO_ROOT),
            )

            if result.returncode != 0:
                stderr = result.stderr.strip()
                last_error = RuntimeError(f"Claude CLI failed (rc={result.returncode}): {stderr[:200]}")
                if attempt < MAX_RETRIES:
                    time.sleep(RETRY_DELAY * (attempt + 1))
                    continue
                raise last_error

            stdout = result.stdout.strip()
            if not stdout:
                return ""

            try:
                data = json.loads(stdout)
                return data.get("result", "")
            except json.JSONDecodeError:
                return stdout

        except subprocess.TimeoutExpired:
            last_error = RuntimeError(f"Claude CLI timed out ({timeout}s) on attempt {attempt + 1}")
            if attempt < MAX_RETRIES:
                time.sleep(RETRY_DELAY)
                continue

    raise last_error or RuntimeError("Claude CLI failed after retries")


def _save_temp_image(image_bytes: bytes) -> str:
    """Save image bytes to a temp file, return the path.

    The caller is responsible for cleanup, but temp files are small
    and the OS will clean them up eventually.
    """
    fd, path = tempfile.mkstemp(suffix=".jpg", prefix="gamepilot_")
    os.write(fd, image_bytes)
    os.close(fd)
    return path


def _extract_json(text: str) -> dict | None:
    """Extract a JSON object from Claude's response text."""
    text = text.strip()

    # Handle markdown code blocks
    if "```" in text:
        parts = text.split("```")
        for part in parts[1::2]:  # odd-indexed parts are inside code blocks
            part = part.strip()
            if part.startswith("json"):
                part = part[4:].strip()
            try:
                return json.loads(part)
            except json.JSONDecodeError:
                continue

    # Try parsing the whole thing
    try:
        return json.loads(text)
    except json.JSONDecodeError:
        pass

    # Try to find JSON object in the text
    start = text.find("{")
    if start >= 0:
        depth = 0
        for i in range(start, len(text)):
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
                if depth == 0:
                    try:
                        return json.loads(text[start:i + 1])
                    except json.JSONDecodeError:
                        break

    return None


CLASSIFY_PROMPT = """\
You are analyzing a screenshot of Tomb Raider Legend running with RTX Remix.
Classify the current game state into exactly one of these categories:

- setup_dialog: Windows settings/configuration dialog (resolution, graphics options)
- main_menu: Title screen or main menu (New Game, Load Game, Options, etc.)
- loading: Loading screen (progress bar, loading text, black screen with logo)
- gameplay: In-game view where Lara is visible or the 3D world is rendered
- pause_menu: In-game pause/ESC menu overlay
- remix_menu: RTX Remix developer menu is open (dark semi-transparent overlay with tabs, sliders, checkboxes — the ImGui debug menu triggered by Alt+X)
- crashed: Error dialog, frozen frame, or Windows error

Respond with ONLY a JSON object: {"state": "<state_name>", "details": "<brief description of what you see>"}
"""

ACTION_PROMPTS: dict[str, str] = {
    GameState.MAIN_MENU: """\
You are controlling Tomb Raider Legend at the main menu. You can see the menu options on screen.

Your goal: {goal}

Available actions:
- key("RETURN") or key("ENTER") — select/confirm
- key("UP") / key("DOWN") — navigate menu items
- key("ESCAPE") — go back
- wait(ms) — wait N milliseconds

The game's main menu typically has: New Game, Load Game, Options, Extras, Exit.
To load a saved game: navigate to "Load Game" (or "Continue"), press Enter, then select a save slot.

Look at the screenshot and decide what action to take next. Consider which menu item
is currently highlighted/selected.

Respond with ONLY a JSON object:
{{"action": "<action_type>", "args": {{...}}, "reasoning": "<why this action>"}}

Action formats:
  {{"action": "key", "args": {{"name": "RETURN"}}, "reasoning": "..."}}
  {{"action": "key", "args": {{"name": "DOWN"}}, "reasoning": "..."}}
  {{"action": "hold", "args": {{"name": "W", "ms": 2000}}, "reasoning": "..."}}
  {{"action": "wait", "args": {{"ms": 1000}}, "reasoning": "..."}}
  {{"action": "click", "args": {{"x": 400, "y": 300}}, "reasoning": "..."}}
  {{"action": "mouse_move", "args": {{"dx": 100, "dy": 0}}, "reasoning": "..."}}
  {{"action": "goal_complete", "args": {{}}, "reasoning": "..."}}
""",

    GameState.LOADING: """\
The game is on a loading screen. Just wait for it to finish.

Respond with: {{"action": "wait", "args": {{"ms": 3000}}, "reasoning": "Loading screen, waiting for level to load"}}
""",

    GameState.GAMEPLAY: """\
You are controlling Lara Croft in Tomb Raider Legend with RTX Remix active.
You have full control of the character and camera.

Your goal: {goal}

Available actions:
- key("name") — tap a key (WASD for movement, SPACE for jump, E for interact)
- hold("name", ms) — hold a key for N milliseconds (for walking/running)
- mouse_move(dx, dy) — move camera (positive dx = look right, positive dy = look down)
- key("ESCAPE") — open pause menu
- key("X") with alt held — open RTX Remix developer menu (Alt+X)
- wait(ms) — wait N milliseconds
- goal_complete — signal that the goal has been achieved

For Remix menu: to open it, use the special action:
  {{"action": "alt_x", "args": {{}}, "reasoning": "Opening Remix developer menu"}}

Movement tips:
- W = forward, S = backward, A = strafe left, D = strafe right
- Hold W for 2000-3000ms to walk a moderate distance
- Mouse look to turn the camera before walking

Look at what's on screen and decide the next action toward the goal.
Describe what you see (Lara's position, surroundings, any notable objects/lights).

Respond with ONLY a JSON object:
{{"action": "<type>", "args": {{...}}, "reasoning": "<what you see and why this action>"}}

Action formats:
  {{"action": "key", "args": {{"name": "W"}}, "reasoning": "..."}}
  {{"action": "hold", "args": {{"name": "W", "ms": 2000}}, "reasoning": "..."}}
  {{"action": "mouse_move", "args": {{"dx": 200, "dy": 0}}, "reasoning": "..."}}
  {{"action": "alt_x", "args": {{}}, "reasoning": "..."}}
  {{"action": "wait", "args": {{"ms": 1000}}, "reasoning": "..."}}
  {{"action": "goal_complete", "args": {{}}, "reasoning": "..."}}
""",

    GameState.PAUSE_MENU: """\
The game's pause menu is open. You can see menu options overlaid on the game.

Your goal: {goal}

IMPORTANT — Tomb Raider Legend pause menu controls:
- UP/DOWN to navigate menu items, ENTER/RETURN to select
- ESCAPE to resume gameplay (close the pause menu)
- To SKIP A CUTSCENE: press UP to highlight "Skip Cutscene", then ENTER to confirm.
  The skip option is typically above the default-highlighted item, so press UP first.

Look at the screenshot to see which menu item is currently highlighted, and what
options are available (Resume, Skip Cutscene, Load Checkpoint, Options, Quit, etc.).

Respond with ONLY a JSON object:
{{"action": "<type>", "args": {{...}}, "reasoning": "..."}}
""",

    GameState.REMIX_MENU: """\
The RTX Remix developer menu (ImGui) is open. This is a dark semi-transparent overlay
with tabs, sliders, checkboxes, and collapsible sections.

Your goal: {goal}

You have FULL control over all Remix settings. The menu is navigated with mouse clicks.

Key areas of the Remix menu:
- **Tabs at the top**: Rendering, Developer, Game Setup, Enhancements, About
- **Developer tab**: Debug views, wireframe, GPU profiling
- **Rendering tab**: Ray tracing settings, denoising, DLSS, upscaling
- **Game Setup tab**: Scene settings, material options, sky, terrain
- **Debug View dropdown**: Shows different visualization modes (geometry hash = 277, normals, albedo, etc.)

Available actions:
- click(x, y) — click at screen coordinates to select tabs, toggle checkboxes, open dropdowns
- mouse_move(dx, dy) — move mouse to scroll or hover
- key("name") — type text into input fields
- alt_x — close the Remix menu (Alt+X again)
- wait(ms) — wait for UI to update
- goal_complete — the Remix menu goal is achieved

When you see a dropdown, click it to expand, then click the desired option.
When you see a checkbox, click it to toggle.
When you see a slider, click and drag to adjust.

Look at the screenshot carefully. Identify which tab is active, what settings are visible,
and what needs to change to achieve the goal.

Respond with ONLY a JSON object:
{{"action": "<type>", "args": {{...}}, "reasoning": "<what you see in the menu and why>"}}
""",

    GameState.SETUP_DIALOG: """\
A Windows setup dialog for Tomb Raider Legend is visible. This will be auto-dismissed
by the existing dialog handler. Just wait.

Respond with: {{"action": "wait", "args": {{"ms": 2000}}, "reasoning": "Setup dialog detected, waiting for auto-dismiss"}}
""",
}


def classify_state(image_bytes: bytes) -> tuple[GameState, str]:
    """Send a screenshot to Claude and classify the game state.

    Saves the image to a temp file, calls Claude CLI to read and classify it.

    Returns (state, details) tuple.
    """
    img_path = _save_temp_image(image_bytes)
    try:
        text = _call_claude(CLASSIFY_PROMPT, image_path=img_path)
    except Exception as e:
        return GameState.UNKNOWN, f"Claude call failed: {e}"
    finally:
        try:
            os.unlink(img_path)
        except OSError:
            pass

    data = _extract_json(text)
    if not data:
        return GameState.UNKNOWN, f"Failed to parse: {text[:200]}"

    state_str = data.get("state", "unknown")
    details = data.get("details", "")

    try:
        state = GameState(state_str)
    except ValueError:
        state = GameState.UNKNOWN

    return state, details


def decide_action(
    image_bytes: bytes,
    state: GameState,
    goal: str,
    history: list[dict] | None = None,
) -> dict:
    """Ask Claude what action to take given current state and goal.

    Args:
        image_bytes: Current screenshot as JPEG bytes.
        state: Current classified game state.
        goal: The high-level goal string.
        history: Last N actions taken (for context).

    Returns:
        Action dict with "action", "args", "reasoning" keys.
    """
    prompt_template = ACTION_PROMPTS.get(state)
    if not prompt_template:
        return {
            "action": "wait",
            "args": {"ms": 2000},
            "reasoning": f"No handler for state {state.value}",
        }

    prompt = prompt_template.format(goal=goal)

    # Add recent history for context
    if history:
        recent = history[-5:]
        history_text = "\n".join(
            f"  Step {i+1}: {h.get('action')} {h.get('args', {})} — {h.get('reasoning', '')}"
            for i, h in enumerate(recent)
        )
        prompt += f"\n\nRecent actions taken:\n{history_text}\n\nDo NOT repeat the same action if it didn't make progress. Try something different."

    img_path = _save_temp_image(image_bytes)
    try:
        text = _call_claude(prompt, image_path=img_path, timeout=45)
    except Exception as e:
        return {
            "action": "wait",
            "args": {"ms": 2000},
            "reasoning": f"Claude call failed: {e}",
        }
    finally:
        try:
            os.unlink(img_path)
        except OSError:
            pass

    data = _extract_json(text)
    if data:
        return data

    return {
        "action": "wait",
        "args": {"ms": 1000},
        "reasoning": f"Failed to parse action response: {text[:200]}",
    }
