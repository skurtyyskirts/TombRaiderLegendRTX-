with open("tests/test_pyghidra_backend.py", "r") as f:
    content = f.read()

# Missing 149: decompile -> is_analyzed is False -> return [error]...
test_decompile_not_analyzed = """    def test_decompile_not_analyzed(self, tmp_path):
        from pyghidra_backend import decompile
        project_dir = tmp_path / "ghidra"
        project_dir.mkdir()
        binary = tmp_path / "test.exe"
        binary.write_bytes(b"MZ" + b"\\x00" * 100)
        result = decompile(str(project_dir), str(binary), 0x401000)
        assert "[error] no analyzed project" in result

"""

if "test_decompile_not_analyzed" not in content:
    content = content.replace("    def test_decompile_pyghidra_missing(self, tmp_path, monkeypatch):", test_decompile_not_analyzed + "    def test_decompile_pyghidra_missing(self, tmp_path, monkeypatch):")

with open("tests/test_pyghidra_backend.py", "w") as f:
    f.write(content)
