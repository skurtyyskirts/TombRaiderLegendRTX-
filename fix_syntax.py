with open("tests/test_cfg_switch.py", "r") as f:
    content = f.read()
content = content.replace(
    'table_entries = struct.pack("<4I", 0x401010, 0x401014, 0x401018, 0x40101C  # noqa',
    'table_entries = struct.pack("<4I", 0x401010, 0x401014, 0x401018, 0x40101C)  # noqa'
)
with open("tests/test_cfg_switch.py", "w") as f:
    f.write(content)
