import re
with open("tests/test_pyghidra_backend.py", "r") as f:
    content = f.read()

old_code = """        mock_pyghidra.start.assert_called_once()
        mock_pyghidra.open_program.assert_called_once()
        assert "[error]" not in result"""

new_code = """        mock_pyghidra.start.assert_called_once()
        mock_pyghidra.open_program.assert_called_once()
        assert "[error]" not in result

        import re
        assert re.match(r"Analysis complete: test\.exe \([0-9.]+s\), project saved to .*", result)"""

if old_code in content:
    content = content.replace(old_code, new_code)
    with open("tests/test_pyghidra_backend.py", "w") as f:
        f.write(content)
    print("Replaced successfully.")
else:
    print("Could not find the target code.")
