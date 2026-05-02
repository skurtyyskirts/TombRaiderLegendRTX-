import os
import json
import subprocess
from unittest.mock import patch, MagicMock
import pytest
from gamepilot.vision import _extract_json, _save_temp_image, _call_claude, classify_state, decide_action, GameState

def test_extract_json_pure():
    data = {"key": "value"}
    assert _extract_json(json.dumps(data)) == data

def test_extract_json_markdown():
    data = {"key": "value"}
    # With json prefix
    text = f"Here is the result:\n```json\n{json.dumps(data)}\n```"
    assert _extract_json(text) == data

    # Without json prefix
    text = f"```\n{json.dumps(data)}\n```"
    assert _extract_json(text) == data

def test_extract_json_embedded():
    data = {"state": "main_menu", "details": "The game is at the main menu."}
    text = f"I see the following: {json.dumps(data)}. Let me know if you need anything else."
    assert _extract_json(text) == data

def test_extract_json_invalid():
    assert _extract_json("not json") is None
    assert _extract_json("{invalid: json}") is None

def test_save_temp_image():
    image_bytes = b"fake-image-data"
    path = _save_temp_image(image_bytes)

    try:
        assert os.path.exists(path)
        assert path.endswith(".jpg")
        assert "gamepilot_" in path

        with open(path, "rb") as f:
            assert f.read() == image_bytes
    finally:
        if os.path.exists(path):
            os.unlink(path)

@patch("subprocess.run")
def test_call_claude_success(mock_run):
    mock_run.return_value = MagicMock(
        returncode=0,
        stdout=json.dumps({"result": "Claude's response"}),
        stderr=""
    )

    response = _call_claude("test prompt")
    assert response == "Claude's response"
    mock_run.assert_called_once()

@patch("subprocess.run")
@patch("time.sleep", return_value=None)
def test_call_claude_retry_success(mock_sleep, mock_run):
    # First call fails, second succeeds
    mock_run.side_effect = [
        MagicMock(returncode=1, stderr="Error"),
        MagicMock(returncode=0, stdout=json.dumps({"result": "Success after retry"}))
    ]

    response = _call_claude("test prompt")
    assert response == "Success after retry"
    assert mock_run.call_count == 2

@patch("subprocess.run")
@patch("time.sleep", return_value=None)
def test_call_claude_exhausted_retries(mock_sleep, mock_run):
    mock_run.return_value = MagicMock(returncode=1, stderr="Constant error")

    with pytest.raises(RuntimeError, match="Claude CLI failed"):
        _call_claude("test prompt")

    assert mock_run.call_count == 3  # Initial + 2 retries

@patch("gamepilot.vision._call_claude")
def test_classify_state(mock_call):
    mock_call.return_value = json.dumps({"state": "main_menu", "details": "at main menu"})

    state, details = classify_state(b"fake-image")
    assert state == GameState.MAIN_MENU
    assert details == "at main menu"

@patch("gamepilot.vision._call_claude")
def test_decide_action(mock_call):
    mock_call.return_value = json.dumps({
        "action": "key",
        "args": {"name": "RETURN"},
        "reasoning": "select new game"
    })

    action = decide_action(b"fake-image", GameState.MAIN_MENU, "Start the game")
    assert action["action"] == "key"
    assert action["args"]["name"] == "RETURN"
    assert action["reasoning"] == "select new game"
