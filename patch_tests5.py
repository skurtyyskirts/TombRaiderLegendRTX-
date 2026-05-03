with open("tests/test_pyghidra_backend.py", "r") as f:
    content = f.read()

test_decompile_dec_va = """    def test_decompile_dec_va(self, tmp_path, capsys, monkeypatch):
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
        binary.write_bytes(b"MZ" + b"\\x00" * 100)

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
                "--project", str(tmp_path / "patches" / "TestProj"),
            ]
            main()
        captured = capsys.readouterr()
        assert "void bar_dec(void)" in captured.out
"""

if "test_decompile_dec_va" not in content:
    content = content + "\n" + test_decompile_dec_va

with open("tests/test_pyghidra_backend.py", "w") as f:
    f.write(content)
