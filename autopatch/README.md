# autopatch — Autonomous Patch-and-Test Loop

Fully autonomous system that diagnoses why geometry disappears at distance, generates patch hypotheses, applies them at runtime, runs a 3-position movement test with screenshots, and evaluates the result — with zero human input.

```bash
python -m autopatch                  # full run: diagnose + patch loop
python -m autopatch --skip-diagnosis # skip capture, use existing diagnostic data
python -m autopatch --dry-run        # calibrate evaluator only, no game launch
```

---

## How It Works

### Phase 1 — Diagnostic Capture

Swaps in the dx9 tracer DLL, launches the game twice (Lara near stage, Lara far from stage), captures full-frame JSONL at each position, diffs the two captures to identify which draw calls disappear at distance, then restores the proxy DLL.

### Phase 2 — Hypothesis Generation

Decompiles the calling functions of missing draw calls, extracts conditional jumps, ranks candidates by proximity and type, and filters out addresses already tried in previous builds.

### Phase 3 — Patch and Test Loop

For each hypothesis:
1. Launches the game with the current proxy
2. Attaches livetools and applies a runtime NOP patch via `mem write`
3. Runs a 3-position movement macro with screenshot capture
4. Evaluates screenshots for red + green light visibility using pixel heuristics
5. Records result and advances to next hypothesis on failure

### Phase 4 — Promotion

If a runtime patch passes all 3 positions, promotes it to proxy C source (`TRL_ApplyMemoryPatches` in `d3d9_device.c`), rebuilds the proxy, and deploys.

---

## Output

| File | Contents |
|------|----------|
| `knowledge.json` | Full iteration history — every address tried, result, draw count delta |
| `diagnostic_captures/` | Near/far frame JSONL and differential report |

Progress is printed per iteration:

```
Iteration iter_045: NOP 6-byte je at 0x40ACF0 (near caller 0x40AD12)
  Applying patch: 0x40ACF0 <- 909090909090
  Running 3-position evaluation macro...
  Verdict: passed=False, red=[True, True, False], green=[True, True, False]
```

**Stopping conditions:** PASS (promotes and rebuilds), 10 consecutive failures (pauses for review), or no remaining hypotheses.

---

## Modules

| Module | Role |
|--------|------|
| `orchestrator.py` | Top-level loop — coordinates all phases |
| `diagnose.py` | Near/far frame capture and draw call diff |
| `hypothesis.py` | Candidate generation and filtering |
| `patcher.py` | Runtime memory patch via livetools |
| `evaluator.py` | Screenshot pixel heuristics for pass/fail |
| `safety.py` | Sanity checks before patching |
| `knowledge.py` | Persistent iteration history |
