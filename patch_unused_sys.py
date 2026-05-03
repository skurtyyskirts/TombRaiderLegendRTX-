with open("retools/pyghidra_backend.py", "r") as f:
    content = f.read()

content = content.replace("import shutil\nimport sys\nimport time", "import shutil\nimport time")

with open("retools/pyghidra_backend.py", "w") as f:
    f.write(content)

with open("tests/test_pyghidra_backend.py", "r") as f:
    content = f.read()

if "result =" in content and "result = analyze(" in content:
    content = content.replace("        result = analyze(str(binary), str(project_dir))", "        analyze(str(binary), str(project_dir))")

with open("tests/test_pyghidra_backend.py", "w") as f:
    f.write(content)
