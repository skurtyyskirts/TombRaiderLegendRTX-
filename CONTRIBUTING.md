# Contributing

This project is an active reverse engineering effort to make Tomb Raider Legend render correctly under NVIDIA RTX Remix. Contributions are welcome in the following areas:

- **Static analysis**: Decompiling and annotating TRL engine functions in `patches/TombRaiderLegend/kb.h`
- **Proxy DLL**: Improvements to the D3D9 proxy in `proxy/`
- **Tooling**: Enhancements to `retools/`, `livetools/`, or `graphics/directx/dx9/tracer/`
- **Documentation**: Guides, reference docs, and research in `docs/`

---

## Getting Started

1. Clone the repo and run `python verify_install.py` — every check must pass before using any tool
2. Run `pip install -r requirements.txt` for Python dependencies
3. Read [`docs/status/WHITEBOARD.md`](docs/status/WHITEBOARD.md) for full project context
4. Read [`patches/TombRaiderLegend/kb.h`](patches/TombRaiderLegend/kb.h) for accumulated address discoveries

---

## Project Structure

| Directory | Purpose |
|-----------|---------|
| `proxy/` | Working D3D9 proxy DLL source (MSVC x86, no-CRT) |
| `retools/` | Offline static analysis toolkit (PE analysis, decompilation, signatures) |
| `livetools/` | Live dynamic analysis toolkit (Frida-based, attaches to running process) |
| `graphics/directx/dx9/tracer/` | Full-frame D3D9 API capture and analysis |
| `autopatch/` | Autonomous patch-and-test loop system |
| `docs/` | Technical guides, reference docs, research |
| `TRL tests/` | Test build archive — every build committed with SUMMARY.md + screenshots |
| `patches/TombRaiderLegend/` | Project workspace: KB, findings, proxy source, test scripts (git-ignored) |

---

## Code Review Checklist

Before merging, verify the following:

- **General purpose x86/x64 RE tools**: Tools in `retools/`, `livetools/`, and `graphics/` are meant to be general-purpose, not game-specific. LLM-friendly outputs, Unix-like composability. Windows-first; Linux/macOS support is optional but encouraged.

- **Legal & scope**: No game-specific data (function maps, address databases, struct definitions) in tracked core code. General-purpose signatures only (CRT, compiler, STL). Project-specific data belongs in gitignored workspace directories (`patches/<project>/`) or a pullable package — not MIT-licensed under core tooling.

- **No duplication**: Before adding a new tool, check whether an existing one can be extended. Follow the "one obvious way of doing things" rule and keep tools general-purpose.

- **IDE instructions in sync**: All IDE rule/instruction/hook files (`.claude/`, `.cursor/`, `.github/`, `.kiro/`) must describe all tools consistently so every user gets the same experience regardless of IDE.

- **Apply repo rules**: Contributions to IDE-specific files (rules, skills, hooks) should be diffed against the base branch and reviewed against all project rules. LLMs often produce quick-fix code that violates conventions — always do a pass.

---

## Build & Test

```bash
# Build proxy DLL (requires MSVC x86 toolchain)
cd proxy && build.bat

# Run full test pipeline (build + deploy + launch + macro + collect results)
python patches/TombRaiderLegend/run.py test --build --randomize

# Autonomous patch-and-test loop
python -m autopatch
```

See [`TRL tests/README.md`](TRL%20tests/README.md) for pass/fail criteria and the full build archive.

---

## Conventions

- Every test run gets its own `TRL tests/build-NNN-<description>/` folder with a `SUMMARY.md`, screenshots, proxy log, and proxy source snapshot
- PASS builds include `miracle` in the folder name; FAIL builds describe the failure
- All builds — pass or fail — are committed and pushed immediately
- Never batch multiple test results without a code change between runs
- Before any proxy edit, create a timestamped backup in `patches/TombRaiderLegend/backups/`
