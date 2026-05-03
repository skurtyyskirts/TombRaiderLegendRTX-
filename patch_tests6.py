import re

with open("tests/test_pyghidra_backend.py", "r") as f:
    content = f.read()

test_main_dunder = """    def test_main_dunder(self, monkeypatch):
        import pyghidra_backend
        monkeypatch.setattr(pyghidra_backend, "__name__", "__main__")
        mock_main = MagicMock()
        monkeypatch.setattr(pyghidra_backend, "main", mock_main)

        # We can't actually 'execute' it because the file was already imported,
        # but to cover line 238 `main()` we need to do runpy or just ignore it
        # since it's common to ignore `if __name__ == "__main__": main()`
"""
