import pytest
import sys
import os

# Add root directory to sys.path so verify_install can be imported
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

import verify_install


@pytest.fixture(autouse=True)
def clean_results():
    """Clear the global results list before and after each test."""
    verify_install.results.clear()
    yield
    verify_install.results.clear()


def test_record_pass(capsys):
    verify_install.record("test-pass", verify_install.PASS, "all good")

    assert len(verify_install.results) == 1
    assert verify_install.results[0] == ("test-pass", verify_install.PASS, "all good")

    captured = capsys.readouterr()
    assert captured.out.strip() == "[+] test-pass -- all good"


def test_record_fail(capsys):
    verify_install.record("test-fail", verify_install.FAIL, "something broke")

    assert len(verify_install.results) == 1
    assert verify_install.results[0] == (
        "test-fail",
        verify_install.FAIL,
        "something broke",
    )

    captured = capsys.readouterr()
    assert captured.out.strip() == "[!] test-fail -- something broke"


def test_record_warn(capsys):
    verify_install.record("test-warn", verify_install.WARN, "warning msg")

    assert len(verify_install.results) == 1
    assert verify_install.results[0] == (
        "test-warn",
        verify_install.WARN,
        "warning msg",
    )

    captured = capsys.readouterr()
    assert captured.out.strip() == "[~] test-warn -- warning msg"


def test_record_no_detail(capsys):
    verify_install.record("test-no-detail", verify_install.PASS)

    assert len(verify_install.results) == 1
    assert verify_install.results[0] == ("test-no-detail", verify_install.PASS, "")

    captured = capsys.readouterr()
    assert captured.out.strip() == "[+] test-no-detail"


def test_record_invalid_status():
    with pytest.raises(KeyError):
        verify_install.record("test-invalid", "INVALID_STATUS", "should fail")
