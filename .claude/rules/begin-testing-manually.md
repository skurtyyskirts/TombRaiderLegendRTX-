# "Begin Testing Manually" Trigger

When the user says **"begin testing manually"**, **"manual test"**, **"test manually"**, or **"manual testing"**, immediately execute this workflow without asking questions:

## Workflow

1. **Build + deploy**: Run `python patches/TombRaiderLegend/run.py test --build` logic — but only the **build and deploy** steps (build proxy, copy DLL + INI to game dir). Do NOT replay any macro or send any keypresses.

2. **Launch the game**: Use the same `launch_game()` path (NvRemixLauncher32 → trl.exe, dismiss setup dialog, wait for window). Let the game fully load.

3. **Attach livetools after 40 seconds**: Once the game window is found and the initial 20s load wait completes, wait an additional 20s (40s total from window detection), then run:
   ```
   python -m livetools attach trl.exe
   ```
   Confirm attachment succeeded. If it fails, retry once after 5s.

4. **Spawn static-analyzer subagent in background** immediately after attachment — same tasks as the automated test:
   - `disasm.py trl.exe 0x407150 -n 10` — verify RET byte at cull function patch site
   - `disasm.py trl.exe 0x4070F0 -n 30` — verify NOP bytes at scene traversal cull jumps
   - Write findings to `patches/TombRaiderLegend/findings.md`

5. **Tell the user the game is ready**: Print a clear message that the game is launched, livetools are attached, and they should play/test. Remind them to say when they're done.

6. **Wait for user signal**: The user will either:
   - Say the patches worked / didn't work
   - Say they're done testing
   - Close the game (gracefully or force-kill)

7. **After user signals done**:
   - Detach livetools: `python -m livetools detach`
   - Read the proxy log (`ffp_proxy.log`) from the game directory
   - Read `patches/TombRaiderLegend/findings.md` for static-analyzer output
   - Run Ghidra MCP checks (same as automated: `mcp__ghidra__get_code` on `RenderLights_FrustumCull` at 0x0060C7D0, plus any function flagged in the proxy log)

8. **Determine result**: Based on what the user reported:
   - User says it worked → PASS
   - User says it didn't work → FAIL (ask what they observed if not already stated)

9. **Package build**: Create `TombRaiderLegendRTX-/TRL tests/build-NNN-<description>/` with:
   - `ffp_proxy.log`
   - Proxy source files (`proxy/` subfolder)
   - `SUMMARY.md` — must include ALL of the following sections:
     ```
     ## Result
     ## What Changed This Build
     ## Proxy Log Summary (draw counts, vpValid, patch addresses)
     ## Retools Findings (from static-analyzer subagent)
     ## Ghidra MCP Findings
     ## User Observations (what the user reported seeing)
     ## Open Hypotheses (what we think is still wrong and why)
     ## Next Build Plan (what to change next and what result to expect)
     ```
   - **No screenshots** — the user tests visually and reports results verbally

10. **Commit + push**: Push to `skurtyyskirts/TombRaiderLegendRTX-` immediately — every build gets uploaded, pass or fail

## Build Numbering

- Check existing builds in `TombRaiderLegendRTX-/TRL tests/` and increment
- Append `-manual` to the folder name to distinguish from automated tests
- PASS builds: include "miracle" in the folder name
- FAIL builds: include the failure reason (e.g., "lights-missing-manual-fail")

## Key Differences from Automated Testing

| Aspect | Automated (`begin testing`) | Manual (`begin testing manually`) |
|--------|----------------------------|-----------------------------------|
| Keypresses/macro | Yes — full macro replay | **None** — user controls the game |
| Screenshots | Captured automatically | **None** — user reports visually |
| Livetools | Not attached during test | **Attached after 40s** for live inspection |
| Result determination | Screenshot analysis | **User's verbal report** |
| Phases | Two phases (hash debug + clean) | **Single session** — user switches views |

## No Questions

Do not ask the user to copy files, confirm the build, or confirm launch. Build, deploy, launch, attach — then tell them it's ready. The only interaction is waiting for their test results.
