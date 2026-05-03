with open("tests/test_pyghidra_backend.py", "r") as f:
    content = f.read()

# Add test_empty_rep wait it already exists?
# Wait, missing 40 is `return False` from `if not any(rep.iterdir()):`
# Wait, TestIsAnalyzed.test_empty_rep was
# `(project_dir / "test.rep").mkdir()` but NOT nested.

test_nested_empty_rep = """    def test_nested_empty_rep(self, tmp_path):
        from pyghidra_backend import is_analyzed
        project_dir = tmp_path / "ghidra"
        nested = project_dir / "test"
        nested.mkdir(parents=True)
        (nested / "test.gpr").write_text("project")
        rep = nested / "test.rep"
        rep.mkdir()
        assert is_analyzed(str(project_dir), "test.exe") is False
"""
if "test_nested_empty_rep" not in content:
    content = content.replace("    def test_rep_not_dir(self, tmp_path):", test_nested_empty_rep + "\n    def test_rep_not_dir(self, tmp_path):")

with open("tests/test_pyghidra_backend.py", "w") as f:
    f.write(content)
