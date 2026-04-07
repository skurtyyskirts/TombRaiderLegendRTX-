"""Hypothesis engine — generate patch candidates from diagnostic data."""
from __future__ import annotations

import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))


@dataclass
class Hypothesis:
    id: str
    description: str
    target_addr: int
    original_bytes: bytes
    patch_bytes: bytes
    patch_type: str  # "nop_jump_6", "nop_jump_2", "ret_true", "ret_true_stdcall"
    rationale: str
    confidence: float
    source: str  # "diagnostic_diff", "static_analysis", "manual"


# x86 conditional jump opcodes (2-byte near: 0F 8x, 2-byte short: 7x)
_COND_JUMP_NEAR = re.compile(r"\b(je|jne|jz|jnz|jl|jg|jle|jge|ja|jb|jbe|jae|jnp|jp)\b", re.I)


def _read_bytes_from_binary(addr: int, size: int) -> bytes | None:
    """Read bytes from the on-disk PE at a virtual address.

    Returns None if the read fails — callers must skip the hypothesis.
    """
    binary = REPO_ROOT / "Tomb Raider Legend" / "trl.exe"
    try:
        result = subprocess.run(
            [sys.executable, "-m", "retools.readmem", str(binary),
             hex(addr), "bytes", "-n", str(size)],
            capture_output=True, text=True, cwd=str(REPO_ROOT), timeout=10,
        )
        for line in result.stdout.splitlines():
            if ":" in line:
                hex_part = line.split(":", 1)[1].strip()
                byte_strs = hex_part.split()
                return bytes(int(b, 16) for b in byte_strs[:size])
    except Exception:
        pass
    return None


def _decompile_function(addr: int) -> str:
    """Decompile a function and return the output. Uses retools.decompiler."""
    binary = REPO_ROOT / "Tomb Raider Legend" / "trl.exe"
    kb = REPO_ROOT / "patches" / "TombRaiderLegend" / "kb.h"
    types_arg = str(kb) if kb.exists() else ""
    cmd = [sys.executable, "-m", "retools.decompiler", str(binary), hex(addr)]
    if types_arg:
        cmd += ["--types", types_arg]
    try:
        result = subprocess.run(
            cmd, capture_output=True, text=True, cwd=str(REPO_ROOT), timeout=30,
        )
        return result.stdout
    except Exception as e:
        return f"Decompilation failed: {e}"


def _disassemble_range(addr: int, count: int = 50) -> str:
    """Disassemble instructions at address."""
    binary = REPO_ROOT / "Tomb Raider Legend" / "trl.exe"
    try:
        result = subprocess.run(
            [sys.executable, "-m", "retools.disasm", str(binary),
             hex(addr), "-n", str(count)],
            capture_output=True, text=True, cwd=str(REPO_ROOT), timeout=10,
        )
        return result.stdout
    except Exception as e:
        return f"Disassembly failed: {e}"


def _extract_conditional_jumps(disasm_output: str) -> list[dict]:
    """Parse disassembly to find conditional jump instructions.

    Returns list of dicts with keys: addr, mnemonic, target, size.
    Determines instruction size by reading the first opcode byte from the
    binary: 0x0F prefix = 6-byte near conditional jump, 0x70-0x7F = 2-byte
    short conditional jump.
    """
    jumps = []
    for line in disasm_output.splitlines():
        line = line.strip()
        if not line:
            continue

        parts = line.split()
        if len(parts) < 3:
            continue

        addr_str = parts[0].rstrip(":")
        if not addr_str.startswith("0x"):
            continue

        for i, p in enumerate(parts[1:], 1):
            if _COND_JUMP_NEAR.match(p):
                addr = int(addr_str, 16)
                mnemonic = p
                target = parts[i + 1] if i + 1 < len(parts) else ""

                # Determine size from the actual opcode byte in the binary
                first_byte = _read_bytes_from_binary(addr, 1)
                if first_byte is None:
                    break
                if first_byte[0] == 0x0F:
                    byte_count = 6  # 0F 8x XX XX XX XX (near conditional)
                elif 0x70 <= first_byte[0] <= 0x7F:
                    byte_count = 2  # 7x XX (short conditional)
                else:
                    break  # not a recognized conditional jump opcode

                jumps.append({
                    "addr": addr,
                    "mnemonic": mnemonic,
                    "target": target,
                    "size": byte_count,
                })
                break

    return jumps


def generate_from_diagnostic(
    diagnostic_result: dict,
    tried_addrs: list[int],
    blacklisted_addrs: list[int],
    max_hypotheses: int = 10,
) -> list[Hypothesis]:
    """Generate patch hypotheses from diagnostic diff results.

    For each unique caller address of missing draws, disassemble the surrounding
    function and extract conditional jumps. Each jump becomes a NOP hypothesis.

    Args:
        diagnostic_result: Output from diagnose.run_diagnostic().
        tried_addrs: Addresses already attempted in previous iterations.
        blacklisted_addrs: Addresses that caused crashes.
        max_hypotheses: Maximum number of hypotheses to generate.

    Returns:
        List of Hypothesis objects, sorted by confidence (highest first).
    """
    caller_addrs = diagnostic_result.get("unique_caller_addrs", [])
    if not caller_addrs:
        return []

    tried_set = set(tried_addrs)
    blacklist_set = set(blacklisted_addrs)

    hypothesis_counter = 45 + len(tried_addrs)

    # Phase 1: collect the best untried jump from each caller
    per_caller_best: list[Hypothesis] = []
    per_caller_rest: list[Hypothesis] = []

    for caller_hex in caller_addrs:
        try:
            caller_addr = int(caller_hex, 16)
        except ValueError:
            continue

        if caller_addr < 0x401000 or caller_addr > 0x01000000:
            print(f"  [hyp] Skipping caller {caller_hex} — outside .text")
            continue

        start = max(0x401000, caller_addr - 0x100)
        disasm = _disassemble_range(start, count=100)
        jumps = _extract_conditional_jumps(disasm)
        print(f"  [hyp] Caller {caller_hex}: {len(jumps)} conditional jumps found")

        caller_hypotheses: list[Hypothesis] = []
        for jump in jumps:
            addr = jump["addr"]
            size = jump["size"]

            if addr in tried_set or addr in blacklist_set:
                continue

            orig = _read_bytes_from_binary(addr, size)
            if orig is None:
                continue
            nop_bytes = b"\x90" * size

            distance = abs(addr - caller_addr)
            confidence = max(0.1, 1.0 - (distance / 0x200))

            mnemonic = jump["mnemonic"].lower()
            if mnemonic in ("jle", "jge", "jl", "jg", "ja", "jb", "jbe", "jae"):
                confidence = min(1.0, confidence + 0.2)

            hypothesis_counter += 1
            caller_hypotheses.append(Hypothesis(
                id=f"H{hypothesis_counter:03d}",
                description=f"NOP {size}-byte {mnemonic} at {hex(addr)} "
                           f"(near caller {caller_hex})",
                target_addr=addr,
                original_bytes=orig,
                patch_bytes=nop_bytes,
                patch_type=f"nop_jump_{size}",
                rationale=f"Conditional jump {mnemonic} at {hex(addr)} is within "
                         f"{distance} bytes of a draw call caller at {caller_hex}. "
                         f"NOPing it removes this culling gate.",
                confidence=confidence,
                source="diagnostic_diff",
            ))

        # Sort this caller's hypotheses by confidence
        caller_hypotheses.sort(key=lambda h: h.confidence, reverse=True)
        if caller_hypotheses:
            per_caller_best.append(caller_hypotheses[0])
            per_caller_rest.extend(caller_hypotheses[1:])

    # Phase 2: take best from each caller first, then fill with remaining
    per_caller_best.sort(key=lambda h: h.confidence, reverse=True)
    per_caller_rest.sort(key=lambda h: h.confidence, reverse=True)

    hypotheses = per_caller_best[:max_hypotheses]
    remaining_slots = max_hypotheses - len(hypotheses)
    if remaining_slots > 0:
        hypotheses.extend(per_caller_rest[:remaining_slots])

    hypotheses.sort(key=lambda h: h.confidence, reverse=True)
    return hypotheses


def generate_from_function(
    func_addr: int,
    tried_addrs: list[int],
    blacklisted_addrs: list[int],
    description_prefix: str = "Static analysis",
) -> list[Hypothesis]:
    """Generate NOP hypotheses from all conditional jumps in a function.

    Useful for systematically scanning a known culling function.
    """
    disasm = _disassemble_range(func_addr, count=200)
    jumps = _extract_conditional_jumps(disasm)

    tried_set = set(tried_addrs)
    blacklist_set = set(blacklisted_addrs)

    hypotheses = []
    counter = 100

    for jump in jumps:
        addr = jump["addr"]
        size = jump["size"]

        if addr in tried_set or addr in blacklist_set:
            continue

        orig = _read_bytes_from_binary(addr, size)
        if orig is None:
            continue
        counter += 1

        hypotheses.append(Hypothesis(
            id=f"S{counter:03d}",
            description=f"{description_prefix}: NOP {jump['mnemonic']} at {hex(addr)}",
            target_addr=addr,
            original_bytes=orig,
            patch_bytes=b"\x90" * size,
            patch_type=f"nop_jump_{size}",
            rationale=f"Conditional jump in function at {hex(func_addr)}",
            confidence=0.5,
            source="static_analysis",
        ))

    return hypotheses
