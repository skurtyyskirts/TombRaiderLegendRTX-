with open("retools/pyghidra_backend.py", "r") as f:
    content = f.read()

# Add pragma no cover
if "if __name__ == \"__main__\":" in content and "# pragma: no cover" not in content:
    content = content.replace("if __name__ == \"__main__\":", "if __name__ == \"__main__\":  # pragma: no cover")

with open("retools/pyghidra_backend.py", "w") as f:
    f.write(content)
