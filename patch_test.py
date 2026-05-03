import re

with open("tests/test_pyghidra_backend.py", "r") as f:
    content = f.read()

# Make the import re at top of file
if "import pytest" in content and "import re" not in content:
    content = content.replace("import pytest", "import re\nimport pytest")

with open("tests/test_pyghidra_backend.py", "w") as f:
    f.write(content)
