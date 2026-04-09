"""Session management — structured logging and artifact collection per run."""
from __future__ import annotations

import json
import shutil
import time
from datetime import datetime, timezone
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SESSION_ROOT = REPO_ROOT / "gamepilot" / "sessions"


class Session:
    """Manages a single gamepilot run — logging, screenshots, and artifacts.

    Creates a timestamped directory for the session and writes a JSONL event
    log. Screenshots and other artifacts are saved alongside the log.

    Usage:
        with Session(goal="navigate to gameplay") as s:
            s.log("capture", image_path="step_001.jpg", state="main_menu")
            s.save_screenshot(pil_image, "step_001")
            s.log("action", action="key", args={"name": "RETURN"})
    """

    def __init__(
        self,
        goal: str,
        session_dir: Path | str | None = None,
        dry_run: bool = False,
    ):
        self.goal = goal
        self.dry_run = dry_run
        self.start_time = time.monotonic()
        self.step_count = 0
        self._events: list[dict] = []

        ts = datetime.now(timezone.utc).strftime("%Y%m%d_%H%M%S")
        slug = goal[:40].replace(" ", "_").replace("/", "_")
        dir_name = f"{ts}_{slug}"

        if session_dir:
            self.session_dir = Path(session_dir)
        else:
            self.session_dir = DEFAULT_SESSION_ROOT / dir_name

        self.session_dir.mkdir(parents=True, exist_ok=True)
        self.screenshots_dir = self.session_dir / "screenshots"
        self.screenshots_dir.mkdir(exist_ok=True)

        self._log_path = self.session_dir / "events.jsonl"
        self._log_file = open(self._log_path, "a", encoding="utf-8")

        # Write session header
        self.log("session_start", goal=goal, dry_run=dry_run, timestamp=ts)

    def __enter__(self) -> Session:
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close(error=str(exc_val) if exc_val else None)

    def log(self, event_type: str, **data) -> None:
        """Write a structured event to the session log."""
        entry = {
            "t": round(time.monotonic() - self.start_time, 3),
            "type": event_type,
            **data,
        }
        self._events.append(entry)
        self._log_file.write(json.dumps(entry, default=str) + "\n")
        self._log_file.flush()

    def save_screenshot(self, img, name: str) -> Path | None:
        """Save a PIL Image to the session screenshots directory.

        Returns the saved path, or None if saving failed.
        """
        if img is None:
            return None
        path = self.screenshots_dir / f"{name}.jpg"
        try:
            img.save(str(path), format="JPEG", quality=85)
            self.log("screenshot_saved", path=str(path), name=name)
            return path
        except Exception as e:
            self.log("screenshot_save_failed", name=name, error=str(e))
            return None

    def save_artifact(self, source: Path | str, dest_name: str | None = None) -> Path | None:
        """Copy an artifact file into the session directory."""
        source = Path(source)
        if not source.exists():
            self.log("artifact_missing", source=str(source))
            return None
        dest = self.session_dir / (dest_name or source.name)
        try:
            shutil.copy2(str(source), str(dest))
            self.log("artifact_saved", source=str(source), dest=str(dest))
            return dest
        except Exception as e:
            self.log("artifact_save_failed", source=str(source), error=str(e))
            return None

    def write_summary(self, result: dict) -> Path:
        """Write a summary JSON file with the run result."""
        summary = {
            "goal": self.goal,
            "success": result.get("success", False),
            "steps_taken": result.get("steps_taken", self.step_count),
            "final_state": result.get("final_state", "unknown"),
            "error": result.get("error"),
            "duration_s": round(time.monotonic() - self.start_time, 1),
            "dry_run": self.dry_run,
            "event_count": len(self._events),
        }
        path = self.session_dir / "summary.json"
        path.write_text(json.dumps(summary, indent=2, default=str))
        return path

    def close(self, error: str | None = None) -> None:
        """Close the session log."""
        self.log("session_end", error=error, steps=self.step_count)
        if self._log_file and not self._log_file.closed:
            self._log_file.close()
