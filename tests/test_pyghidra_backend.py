"""Tests for retools/pyghidra_backend.py -- pyghidra headless Ghidra backend."""

import sys
from pathlib import Path
from unittest.mock import MagicMock, patch

import re
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "retools"))


# ---------------------------------------------------------------------------
# is_analyzed
# ---------------------------------------------------------------------------


class TestIsAnalyzed:
    def test_missing_dir(self, tmp_path):
        from pyghidra_backend import is_analyzed

        assert is_analyzed(str(tmp_path / "nonexistent"), "test.exe") is False

    def test_missing_gpr(self, tmp_path):
        from pyghidra_backend import is_analyzed

        project_dir = tmp_path / "ghidra"
        project_dir.mkdir()
        assert is_analyzed(str(project_dir), "test.exe") is False

    def test_empty_rep(self, tmp_path):
        from pyghidra_backend import is_analyzed

        project_dir = tmp_path / "ghidra"
        project_dir.mkdir()
        (project_dir / "test.gpr").write_text("")
        (project_dir / "test.rep").mkdir()
        assert is_analyzed(str(project_dir), "test.exe") is False

    def test_nested_empty_rep(self, tmp_path):
        from pyghidra_backend import is_analyzed

        project_dir = tmp_path / "ghidra"
        nested = project_dir / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.mkdir()
        assert is_analyzed(str(project_dir), "test.exe") is False

    def test_rep_not_dir(self, tmp_path):
        from pyghidra_backend import is_analyzed

        project_dir = tmp_path / "ghidra"
        # pyghidra nests: project_dir/stem/stem.gpr
        nested = project_dir / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.write_text("not a dir")
        assert is_analyzed(str(project_dir), "test.exe") is False

    def test_valid_project(self, tmp_path):
        from pyghidra_backend import is_analyzed

        project_dir = tmp_path / "ghidra"
        # pyghidra nests: project_dir/stem/stem.gpr
        nested = project_dir / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.mkdir()
        (rep / "data").write_text("data")
        assert is_analyzed(str(project_dir), "test.exe") is True


# ---------------------------------------------------------------------------
# _import_pyghidra and env fallbacks
# ---------------------------------------------------------------------------


class TestImports:
    def test_ensure_java_env_fallback(self, tmp_path, monkeypatch):
        import pyghidra_backend

        monkeypatch.delenv("JAVA_HOME", raising=False)
        monkeypatch.setattr("shutil.which", lambda x: None)

        # Fake tools directory
        tools_dir = tmp_path / "tools"
        tools_dir.mkdir()
        jdk_dir = tools_dir / "jdk-17"
        bin_dir = jdk_dir / "bin"
        bin_dir.mkdir(parents=True)
        (bin_dir / "java").touch()

        monkeypatch.setattr(pyghidra_backend, "_TOOLS", tools_dir)

        pyghidra_backend._ensure_java_env()

        import os

        assert os.environ["JAVA_HOME"] == str(jdk_dir)
        assert str(bin_dir) in os.environ["PATH"]

    def test_ensure_ghidra_env_fallback(self, tmp_path, monkeypatch):
        import pyghidra_backend

        monkeypatch.delenv("GHIDRA_INSTALL_DIR", raising=False)

        # Fake tools directory
        tools_dir = tmp_path / "tools"
        tools_dir.mkdir()
        ghidra_dir = tools_dir / "ghidra_10.3"
        ghidra_dir.mkdir()
        (ghidra_dir / "ghidraRun.bat").touch()

        monkeypatch.setattr(pyghidra_backend, "_TOOLS", tools_dir)

        pyghidra_backend._ensure_ghidra_env()

        import os

        assert os.environ["GHIDRA_INSTALL_DIR"] == str(ghidra_dir)

    def test_import_pyghidra_import_error(self, monkeypatch):
        import pyghidra_backend

        monkeypatch.setattr(pyghidra_backend, "_ensure_java_env", lambda: None)
        monkeypatch.setattr(pyghidra_backend, "_ensure_ghidra_env", lambda: None)

        # Mock ImportError when trying to import pyghidra
        import builtins

        real_import = builtins.__import__

        def fake_import(name, *args, **kwargs):
            if name == "pyghidra":
                raise ImportError("Mocked ImportError")
            return real_import(name, *args, **kwargs)

        monkeypatch.setattr(builtins, "__import__", fake_import)

        # Ensure it's not already in sys.modules
        import sys

        monkeypatch.delitem(sys.modules, "pyghidra", raising=False)

        assert pyghidra_backend._import_pyghidra() is None


# ---------------------------------------------------------------------------
# analyze
# ---------------------------------------------------------------------------


class TestAnalyze:
    def test_calls_start_and_open_program(self, tmp_path, monkeypatch):
        from pyghidra_backend import analyze

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        mock_pyghidra = MagicMock()
        mock_ctx = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_ctx)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)
        result = analyze(str(binary), str(tmp_path / "ghidra"))

        mock_pyghidra.start.assert_called_once()
        mock_pyghidra.open_program.assert_called_once()
        assert "[error]" not in result

        assert re.match(r"Analysis complete: test\.exe \([0-9.]+s\), project saved to .*", result)

    def test_creates_project_dir(self, tmp_path, monkeypatch):
        from pyghidra_backend import analyze

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        mock_pyghidra = MagicMock()
        mock_ctx = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_ctx)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)
        project_dir = tmp_path / "ghidra" / "sub"
        analyze(str(binary), str(project_dir))

        assert project_dir.exists()

    def test_error_no_ghidra_install_dir(self, tmp_path, monkeypatch):
        from pyghidra_backend import analyze

        monkeypatch.delenv("GHIDRA_INSTALL_DIR", raising=False)
        # Patch _ensure_ghidra_env to not auto-detect from tools/
        monkeypatch.setattr("pyghidra_backend._ensure_ghidra_env", lambda: None)

        mock_pyghidra = MagicMock()
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)
        result = analyze(str(binary), str(tmp_path / "ghidra"))

        assert result.startswith("[error]")

    def test_error_pyghidra_missing(self, tmp_path, monkeypatch):
        from pyghidra_backend import analyze

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))
        monkeypatch.delitem(sys.modules, "pyghidra", raising=False)

        with patch("pyghidra_backend._import_pyghidra", return_value=None):
            binary = tmp_path / "test.exe"
            binary.write_bytes(b"MZ" + b"\x00" * 100)
            result = analyze(str(binary), str(tmp_path / "ghidra"))

        assert result.startswith("[error]")


# ---------------------------------------------------------------------------
# decompile
# ---------------------------------------------------------------------------


def _setup_ghidra_project(tmp_path):
    """Create a fake valid Ghidra project on disk for decompile() checks."""
    project_dir = tmp_path / "ghidra"
    # pyghidra nests: project_dir/stem/stem.gpr
    nested = project_dir / "test"
    nested.mkdir(parents=True)
    (nested / "test.gpr").write_text("project")
    rep = nested / "test.rep"
    rep.mkdir()
    (rep / "data").write_text("data")
    return project_dir


def _mock_java_modules(monkeypatch):
    """Inject mock Java modules (ghidra.app.decompiler, ghidra.util.task)."""
    mock_decomp_mod = MagicMock()
    mock_task_mod = MagicMock()

    mock_ifc = MagicMock()
    mock_decomp_mod.DecompInterface.return_value = mock_ifc

    mock_monitor = MagicMock()
    mock_task_mod.ConsoleTaskMonitor.return_value = mock_monitor

    monkeypatch.setitem(sys.modules, "ghidra", MagicMock())
    monkeypatch.setitem(sys.modules, "ghidra.app", MagicMock())
    monkeypatch.setitem(sys.modules, "ghidra.app.decompiler", mock_decomp_mod)
    monkeypatch.setitem(sys.modules, "ghidra.util", MagicMock())
    monkeypatch.setitem(sys.modules, "ghidra.util.task", mock_task_mod)

    return mock_ifc, mock_monitor


class TestDecompile:
    def test_returns_c_output(self, tmp_path, monkeypatch):
        from pyghidra_backend import decompile

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        project_dir = _setup_ghidra_project(tmp_path)
        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        mock_pyghidra = MagicMock()
        mock_flat_api = MagicMock()
        mock_program = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_flat_api)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        mock_flat_api.getCurrentProgram.return_value = mock_program
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        mock_ifc, mock_monitor = _mock_java_modules(monkeypatch)

        # Mock the listing / function lookup
        mock_func = MagicMock()
        mock_listing = MagicMock()
        mock_program.getListing.return_value = mock_listing
        mock_listing.getFunctionContaining.return_value = mock_func

        # Mock decompile result
        mock_result = MagicMock()
        mock_result.getDecompiledFunction.return_value.getC.return_value = (
            "int foo(void) {\n  return 42;\n}"
        )
        mock_ifc.decompileFunction.return_value = mock_result

        result = decompile(str(project_dir), str(binary), 0x401000)
        assert "int foo(void)" in result
        assert "[error]" not in result

    def test_decompile_not_analyzed(self, tmp_path):
        from pyghidra_backend import decompile

        project_dir = tmp_path / "ghidra"
        project_dir.mkdir()
        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)
        result = decompile(str(project_dir), str(binary), 0x401000)
        assert "[error] no analyzed project" in result

    def test_decompile_pyghidra_missing(self, tmp_path, monkeypatch):
        from pyghidra_backend import decompile

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        project_dir = _setup_ghidra_project(tmp_path)
        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        import sys

        monkeypatch.delitem(sys.modules, "pyghidra", raising=False)
        import builtins

        real_import = builtins.__import__

        def fake_import(name, *args, **kwargs):
            if name == "pyghidra":
                raise ImportError("Mocked ImportError")
            return real_import(name, *args, **kwargs)

        monkeypatch.setattr(builtins, "__import__", fake_import)

        result = decompile(str(project_dir), str(binary), 0x401000)
        assert result == "[error] pyghidra is not installed"

    def test_decompile_no_ghidra_dir(self, tmp_path, monkeypatch):
        from pyghidra_backend import decompile

        monkeypatch.delenv("GHIDRA_INSTALL_DIR", raising=False)
        monkeypatch.setattr("pyghidra_backend._ensure_ghidra_env", lambda: None)

        project_dir = _setup_ghidra_project(tmp_path)
        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        import sys

        mock_pyghidra = MagicMock()
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        result = decompile(str(project_dir), str(binary), 0x401000)
        assert result == "[error] GHIDRA_INSTALL_DIR environment variable not set"

    def test_error_no_function(self, tmp_path, monkeypatch):
        from pyghidra_backend import decompile

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        project_dir = _setup_ghidra_project(tmp_path)
        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        mock_pyghidra = MagicMock()
        mock_flat_api = MagicMock()
        mock_program = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_flat_api)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        mock_flat_api.getCurrentProgram.return_value = mock_program
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        mock_ifc, mock_monitor = _mock_java_modules(monkeypatch)

        # No function at this address
        mock_listing = MagicMock()
        mock_program.getListing.return_value = mock_listing
        mock_listing.getFunctionContaining.return_value = None

        result = decompile(str(project_dir), str(binary), 0x401000)
        assert result.startswith("[error]")


# ---------------------------------------------------------------------------
# CLI (main)
# ---------------------------------------------------------------------------


class TestCLI:
    def test_status_not_analyzed(self, tmp_path, capsys):
        from pyghidra_backend import main

        with pytest.raises(SystemExit, match="0"):
            sys.argv = [
                "pyghidra_backend",
                "status",
                str(tmp_path / "test.exe"),
                "--project",
                "TestProj",
            ]
            main()
        captured = capsys.readouterr()
        assert "not analyzed" in captured.out.lower()

    def test_status_analyzed(self, tmp_path, capsys):
        from pyghidra_backend import main

        # Set up a valid project under patches/TestProj/ghidra/test/
        nested = tmp_path / "patches" / "TestProj" / "ghidra" / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.mkdir()
        (rep / "data").write_text("data")

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        with pytest.raises(SystemExit, match="0"):
            sys.argv = [
                "pyghidra_backend",
                "status",
                str(binary),
                "--project",
                str(tmp_path / "patches" / "TestProj"),
            ]
            main()
        captured = capsys.readouterr()
        assert "analyzed" in captured.out.lower()

    def test_analyze_subcommand(self, tmp_path, capsys, monkeypatch):
        from pyghidra_backend import main

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        mock_pyghidra = MagicMock()
        mock_ctx = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_ctx)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        with pytest.raises(SystemExit, match="0"):
            sys.argv = [
                "pyghidra_backend",
                "analyze",
                str(binary),
                "--project",
                str(tmp_path / "patches" / "TestProj"),
            ]
            main()
        captured = capsys.readouterr()
        assert "analysis complete" in captured.out.lower()

    def test_decompile_subcommand(self, tmp_path, capsys, monkeypatch):
        from pyghidra_backend import main

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        # Set up a valid project (nested: ghidra/test/test.gpr)
        nested = tmp_path / "patches" / "TestProj" / "ghidra" / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.mkdir()
        (rep / "data").write_text("data")

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        mock_pyghidra = MagicMock()
        mock_flat_api = MagicMock()
        mock_program = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_flat_api)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        mock_flat_api.getCurrentProgram.return_value = mock_program
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        mock_ifc, _ = _mock_java_modules(monkeypatch)
        mock_func = MagicMock()
        mock_listing = MagicMock()
        mock_program.getListing.return_value = mock_listing
        mock_listing.getFunctionContaining.return_value = mock_func
        mock_result = MagicMock()
        mock_result.getDecompiledFunction.return_value.getC.return_value = "void bar(void) {}"
        mock_ifc.decompileFunction.return_value = mock_result

        with pytest.raises(SystemExit, match="0"):
            sys.argv = [
                "pyghidra_backend",
                "decompile",
                str(binary),
                "0x401000",
                "--project",
                str(tmp_path / "patches" / "TestProj"),
            ]
            main()
        captured = capsys.readouterr()
        assert "void bar(void)" in captured.out

    def test_decompile_hex_va(self, tmp_path, capsys, monkeypatch):
        from pyghidra_backend import main

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        # Set up a valid project (nested: ghidra/test/test.gpr)
        nested = tmp_path / "patches" / "TestProj" / "ghidra" / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.mkdir()
        (rep / "data").write_text("data")

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        mock_pyghidra = MagicMock()
        mock_flat_api = MagicMock()
        mock_program = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_flat_api)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        mock_flat_api.getCurrentProgram.return_value = mock_program
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        mock_ifc, _ = _mock_java_modules(monkeypatch)
        mock_func = MagicMock()
        mock_listing = MagicMock()
        mock_program.getListing.return_value = mock_listing
        mock_listing.getFunctionContaining.return_value = mock_func
        mock_result = MagicMock()
        mock_result.getDecompiledFunction.return_value.getC.return_value = "void bar_hex(void) {}"
        mock_ifc.decompileFunction.return_value = mock_result

        with pytest.raises(SystemExit, match="0"):
            sys.argv = [
                "pyghidra_backend",
                "decompile",
                str(binary),
                "0x401000",
                "--project",
                str(tmp_path / "patches" / "TestProj"),
            ]
            main()
        captured = capsys.readouterr()
        assert "void bar_hex(void)" in captured.out

    def test_decompile_dec_va(self, tmp_path, capsys, monkeypatch):
        from pyghidra_backend import main

        monkeypatch.setenv("GHIDRA_INSTALL_DIR", str(tmp_path))

        # Set up a valid project (nested: ghidra/test/test.gpr)
        nested = tmp_path / "patches" / "TestProj" / "ghidra" / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.mkdir()
        (rep / "data").write_text("data")

        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\x00" * 100)

        mock_pyghidra = MagicMock()
        mock_flat_api = MagicMock()
        mock_program = MagicMock()
        mock_pyghidra.open_program.return_value.__enter__ = MagicMock(return_value=mock_flat_api)
        mock_pyghidra.open_program.return_value.__exit__ = MagicMock(return_value=False)
        mock_flat_api.getCurrentProgram.return_value = mock_program
        monkeypatch.setitem(sys.modules, "pyghidra", mock_pyghidra)

        mock_ifc, _ = _mock_java_modules(monkeypatch)
        mock_func = MagicMock()
        mock_listing = MagicMock()
        mock_program.getListing.return_value = mock_listing
        mock_listing.getFunctionContaining.return_value = mock_func
        mock_result = MagicMock()
        mock_result.getDecompiledFunction.return_value.getC.return_value = "void bar_dec(void) {}"
        mock_ifc.decompileFunction.return_value = mock_result

        with pytest.raises(SystemExit, match="0"):
            sys.argv = [
                "pyghidra_backend",
                "decompile",
                str(binary),
                "4198400",  # 0x401000
                "--project",
                str(tmp_path / "patches" / "TestProj"),
            ]
            main()
        captured = capsys.readouterr()
        assert "void bar_dec(void)" in captured.out
