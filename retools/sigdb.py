#!/usr/bin/env python3
"""Signature database for function identification in PE binaries.

Provides a SQLite-backed signature database with:
- Byte-pattern signatures (FLIRT-style, with relocation wildcards)
- Structural signatures (CFG shape + mnemonic hash)
- Compiler fingerprinting (Rich header, CRT imports, marker patterns)
- Multi-tier matching: byte-exact -> structural fallback
- Build pipeline from JSON manifest + CSV address maps
- CLI with build, scan, identify, fingerprint subcommands

Usage:
    python retools/sigdb.py build manifest.json -o sigs.db
    python retools/sigdb.py scan binary.exe -d sigs.db
    python retools/sigdb.py identify binary.exe 0x401000 -d sigs.db
    python retools/sigdb.py fingerprint binary.exe
"""

from __future__ import annotations

import argparse
import csv
import json
import sqlite3
import struct
import sys
import urllib.error
import urllib.request
import zlib
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from common import Binary

DEFAULT_DB_PATH = Path(__file__).resolve().parent / "data" / "signatures.db"

_HF_REPO_DEFAULT = "RTX-Remix/Vibe-Reverse-Engineering-Signature-DB"
_HF_URL_TEMPLATE = "https://huggingface.co/{repo}/resolve/main/{path}"

SCHEMA_VERSION = 2

_SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS schema_version (
    version INTEGER NOT NULL
);

CREATE TABLE IF NOT EXISTS byte_sigs (
    id        INTEGER PRIMARY KEY AUTOINCREMENT,
    name      TEXT    NOT NULL,
    pattern   BLOB    NOT NULL,
    mask      BLOB    NOT NULL,
    func_size INTEGER NOT NULL DEFAULT 0,
    tail_crc  INTEGER NOT NULL DEFAULT 0,
    compiler  TEXT    NOT NULL DEFAULT '',
    source    TEXT    NOT NULL DEFAULT '',
    category  TEXT    NOT NULL DEFAULT '',
    prefix    BLOB    NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_byte_prefix ON byte_sigs (prefix);

CREATE TABLE IF NOT EXISTS structural_sigs (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    name          TEXT    NOT NULL,
    block_count   INTEGER NOT NULL,
    edge_count    INTEGER NOT NULL,
    call_count    INTEGER NOT NULL,
    mnemonic_hash INTEGER NOT NULL,
    constants     TEXT    NOT NULL DEFAULT '',
    compiler      TEXT    NOT NULL DEFAULT '',
    source        TEXT    NOT NULL DEFAULT '',
    category      TEXT    NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_struct_mhash
    ON structural_sigs (mnemonic_hash);
"""


# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------

@dataclass
class Match:
    name: str
    confidence: float
    tier: str          # "byte" | "structural"
    compiler: str
    source: str
    category: str


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _masked_eq(code: bytes, pattern: bytes, mask: bytes) -> bool:
    if len(code) < len(pattern):
        return False
    for c, p, m in zip(code, pattern, mask):
        if m and c != p:
            return False
    return True


def _mnemonic_hash(insns) -> int:
    h = 0
    for insn in insns:
        h = (h * 31 + hash(insn.mnemonic)) & 0xFFFFFFFF
    return h


def _compute_tail_crc(code: bytes) -> int:
    if len(code) < 8:
        return 0
    return zlib.crc32(code[-8:]) & 0xFFFFFFFF


def _extract_prefix(pattern: bytes, mask: bytes, max_len: int = 8) -> bytes:
    """Extract the longest leading run of non-wildcard bytes."""
    prefix = bytearray()
    for b, m in zip(pattern[:max_len], mask[:max_len]):
        if not m:   # wildcard byte
            break
        prefix.append(b)
    return bytes(prefix)


def _download_file(url: str, dest: str) -> None:
    with urllib.request.urlopen(url) as resp, open(dest, 'wb') as fout:
        while True:
            chunk = resp.read(65536)
            if not chunk:
                break
            fout.write(chunk)


def _pull_sources(repo: str, dest_dir: str) -> None:
    """Download all source JSON files listed in the HuggingFace repo index."""
    index_url = _HF_URL_TEMPLATE.format(repo=repo, path="sources/index.json")
    dest = Path(dest_dir)
    dest.mkdir(parents=True, exist_ok=True)
    try:
        with urllib.request.urlopen(index_url) as resp:
            index = json.loads(resp.read())
        for fname in index.get("files", []):
            url = _HF_URL_TEMPLATE.format(repo=repo, path=f"sources/{fname}")
            _download_file(url, str(dest / fname))
            print(f"  Downloaded {fname}")
    except Exception as e:
        print(f"  Warning: could not pull sources: {e}")


# ---------------------------------------------------------------------------
# Rich-header parser
# ---------------------------------------------------------------------------

def _parse_rich_header(pe_bytes: bytes) -> dict | None:
    """Parse the Rich header from a PE file and return product counts."""
    # The Rich header sits between the DOS stub and the PE header.
    # It is XOR-obfuscated with a 4-byte key stored just before "Rich".
    # Marker: b"Rich" followed by the 4-byte XOR key.
    RICH = b"Rich"
    DANS = b"DanS"
    rich_pos = pe_bytes.find(RICH)
    if rich_pos < 0:
        return None
    key = struct.unpack_from("<I", pe_bytes, rich_pos + 4)[0]
    # Locate "DanS" (XOR-encoded start of the rich header)
    dans_pos = -1
    for i in range(rich_pos - 4, -1, -4):
        if len(pe_bytes) < i + 4:
            continue
        val = struct.unpack_from("<I", pe_bytes, i)[0] ^ key
        if val == struct.unpack(">I", DANS)[0]:
            dans_pos = i
            break
        val2 = struct.unpack_from("<I", pe_bytes, i)[0]
        if val2 ^ key == int.from_bytes(DANS, 'big'):
            dans_pos = i
            break
    if dans_pos < 0:
        return None

    entries: dict[int, int] = {}
    pos = dans_pos + 16  # skip 4 DWORDs (DanS + 3 padding)
    while pos + 8 <= rich_pos:
        prod_id = struct.unpack_from("<I", pe_bytes, pos)[0] ^ key
        count = struct.unpack_from("<I", pe_bytes, pos + 4)[0] ^ key
        comp_id = prod_id >> 16
        build_id = prod_id & 0xFFFF
        entries[(comp_id, build_id)] = entries.get((comp_id, build_id), 0) + count
        pos += 8
    return entries


# ---------------------------------------------------------------------------
# Compiler fingerprinting helpers
# ---------------------------------------------------------------------------

_MSVC_MARKER_PATTERNS = [
    b"\xCC\xCC\xCC\xCC",          # int3 padding
    b"\x56\x8B\xF1",              # push esi; mov esi, ecx  (thiscall prologue)
    b"\x8B\xFF\x55\x8B\xEC",      # mov edi,edi; push ebp; mov ebp,esp
]
_GCC_MARKER_PATTERNS = [
    b"\x55\x89\xE5",              # push ebp; mov ebp, esp (GCC x86 prologue)
    b"\x66\x90",                  # xchg ax,ax (GCC nop)
]
_MINGW_IMPORT_HINTS = [b"__mingw", b"libgcc", b"__imp_"]


def _scan_import_names(b: Binary) -> list[str]:
    """Return a flat list of imported DLL / function names."""
    names: list[str] = []
    try:
        for imp in b.pe.DIRECTORY_ENTRY_IMPORT:
            names.append(imp.dll.decode(errors="replace").lower())
            for sym in imp.imports:
                if sym.name:
                    names.append(sym.name.decode(errors="replace").lower())
    except AttributeError:
        pass
    return names


# ---------------------------------------------------------------------------
# SignatureDB
# ---------------------------------------------------------------------------

class SignatureDB:
    def __init__(self, path: str | Path = DEFAULT_DB_PATH) -> None:
        self._path = str(path)
        self._conn: sqlite3.Connection | None = None
        self._open()

    def _open(self) -> None:
        self._conn = sqlite3.connect(self._path)
        self._conn.row_factory = sqlite3.Row
        cur = self._conn.execute(
            "SELECT name FROM sqlite_master WHERE type='table' AND name='schema_version'"
        )
        if cur.fetchone() is None:
            self._init_schema()
        else:
            row = self._conn.execute(
                "SELECT version FROM schema_version"
            ).fetchone()
            if row is None or row[0] != SCHEMA_VERSION:
                self._migrate(row[0] if row else 0)

    def _migrate(self, from_version: int) -> None:
        """Migrate schema from from_version to SCHEMA_VERSION."""
        if from_version == 1:
            # v1 -> v2: add prefix column and index, populate from existing rows
            try:
                self._conn.execute(
                    "ALTER TABLE byte_sigs ADD COLUMN prefix BLOB NOT NULL DEFAULT ''"
                )
            except sqlite3.OperationalError:
                pass  # column already exists
            self._conn.execute(
                "CREATE INDEX IF NOT EXISTS idx_byte_prefix ON byte_sigs (prefix)"
            )
            # Populate prefix for all existing rows
            rows = self._conn.execute(
                "SELECT id, pattern, mask FROM byte_sigs"
            ).fetchall()
            for row in rows:
                prefix = _extract_prefix(bytes(row[1]), bytes(row[2]))
                self._conn.execute(
                    "UPDATE byte_sigs SET prefix = ? WHERE id = ?",
                    (prefix, row[0]),
                )
            self._conn.execute(
                "UPDATE schema_version SET version = ?", (SCHEMA_VERSION,)
            )
            self._conn.commit()
        else:
            # Unknown version: drop and reinit
            self._conn.executescript(
                "DROP TABLE IF EXISTS schema_version;"
                "DROP TABLE IF EXISTS byte_sigs;"
                "DROP TABLE IF EXISTS structural_sigs;"
            )
            self._init_schema()

    def _init_schema(self) -> None:
        self._conn.executescript(_SCHEMA_SQL)
        self._conn.execute(
            "INSERT INTO schema_version (version) VALUES (?)",
            (SCHEMA_VERSION,),
        )
        self._conn.commit()

    def close(self) -> None:
        if self._conn is not None:
            try:
                self._conn.close()
            except Exception:
                pass
            self._conn = None

    # -- Insertion ---------------------------------------------------------

    def add_byte_sig(
        self, *, name: str, pattern: bytes, mask: bytes,
        func_size: int, tail_crc: int,
        compiler: str = "", source: str = "", category: str = "",
    ) -> None:
        prefix = _extract_prefix(pattern, mask)
        self._conn.execute(
            "INSERT INTO byte_sigs "
            "(name, pattern, mask, func_size, tail_crc, compiler, source, category, prefix) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (name, pattern, mask, func_size, tail_crc, compiler, source, category, prefix),
        )
        self._conn.commit()

    def add_structural_sig(
        self, *, name: str, block_count: int, edge_count: int,
        call_count: int, mnemonic_hash: int, constants: str = "",
        compiler: str = "", source: str = "", category: str = "",
    ) -> None:
        self._conn.execute(
            "INSERT INTO structural_sigs "
            "(name, block_count, edge_count, call_count, mnemonic_hash, "
            "constants, compiler, source, category) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (name, block_count, edge_count, call_count, mnemonic_hash,
             constants, compiler, source, category),
        )
        self._conn.commit()

    # -- Byte matching -----------------------------------------------------

    def match_bytes(
        self, code: bytes, func_size: int,
        preferred_compiler: str, func_tail_crc: int,
    ) -> list[Match]:
        """Find byte-pattern matches for *code* (first 32 bytes of function).

        Matching tiers:
          1. Masked byte comparison against all stored patterns.
          2. Size disambiguation: exact size match scores higher.
          3. Tail CRC disambiguation: matching tail_crc scores higher.
          4. Preferred compiler boost.
        """
        if len(code) < 32:
            return []
        code32 = code[:32]

        # Generate all possible prefixes (lengths 0 to 8)
        prefixes = [code32[:i] for i in range(min(len(code32), 8) + 1)]

        query = (
            "SELECT name, pattern, mask, func_size, tail_crc, "
            "compiler, source, category FROM byte_sigs "
            f"WHERE prefix IN ({','.join(['?'] * len(prefixes))})"
        )
        cur = self._conn.execute(query, prefixes)
        candidates: list[tuple[Match, float]] = []

        for name, pattern, mask, db_size, db_tail_crc, compiler, source, category in cur:
            if not _masked_eq(code32, pattern, mask):
                continue
            score = 0.7  # base confidence for byte match
            if db_size == func_size:
                score += 0.15
            elif db_size > 0 and abs(db_size - func_size) / max(db_size, func_size) < 0.1:
                score += 0.05
            if db_tail_crc != 0 and func_tail_crc != 0 and db_tail_crc == func_tail_crc:
                score += 0.10
            if preferred_compiler and compiler == preferred_compiler:
                score += 0.05
            candidates.append((
                Match(
                    name=name, confidence=min(score, 1.0), tier="byte",
                    compiler=compiler, source=source, category=category,
                ),
                score,
            ))

        candidates.sort(key=lambda c: c[1], reverse=True)
        return [m for m, _ in candidates]

    # -- Structural matching -----------------------------------------------

    def match_structural(
        self, b: Binary, va: int,
        preferred_compiler: str,
    ) -> list[Match]:
        """Find structural matches for the function at *va*."""
        try:
            from analyzer import build_cfg
            cfg = build_cfg(b, va)
        except Exception:
            return []

        block_count = len(cfg)
        edge_count = sum(len(bb.successors) for bb in cfg.values())
        call_count = sum(
            1 for bb in cfg.values()
            for insn in bb.insns if insn.mnemonic == "call"
        )
        all_insns = [
            insn for bb in cfg.values() for insn in bb.insns
        ]
        mh = _mnemonic_hash(all_insns)

        cur = self._conn.execute(
            "SELECT name, block_count, edge_count, call_count, mnemonic_hash, "
            "compiler, source, category FROM structural_sigs "
            "WHERE mnemonic_hash = ?",
            (mh,),
        )
        candidates: list[tuple[Match, float]] = []
        for name, bc, ec, cc, mh2, compiler, source, category in cur:
            score = 0.5
            if bc == block_count:
                score += 0.15
            if ec == edge_count:
                score += 0.10
            if cc == call_count:
                score += 0.10
            if preferred_compiler and compiler == preferred_compiler:
                score += 0.05
            candidates.append((
                Match(
                    name=name, confidence=min(score, 1.0), tier="structural",
                    compiler=compiler, source=source, category=category,
                ),
                score,
            ))
        candidates.sort(key=lambda c: c[1], reverse=True)
        return [m for m, _ in candidates]

    # -- Identification ----------------------------------------------------

    def identify(
        self, b: Binary, va: int,
        preferred_compiler: str = "",
    ) -> Match | None:
        """Return the best match for the function at *va*, or None."""
        code = b.read_va(va, 256)
        func_size = 0  # unknown without disassembly
        tail_crc = _compute_tail_crc(code)
        fp = self.fingerprint(b)
        compiler = preferred_compiler or fp.get("compiler", "")

        byte_matches = self.match_bytes(code, func_size, compiler, tail_crc)
        if byte_matches:
            return byte_matches[0]

        struct_matches = self.match_structural(b, va, compiler)
        if struct_matches:
            return struct_matches[0]

        return None

    # -- Scanning ----------------------------------------------------------

    def scan(
        self, b: Binary,
        preferred_compiler: str = "",
    ) -> list[tuple[int, Match]]:
        """Scan all executable functions in *b* and return (va, match) pairs."""
        fp = self.fingerprint(b)
        compiler = preferred_compiler or fp.get("compiler", "")
        results: list[tuple[int, Match]] = []

        for va, offset, size in b.exec_ranges():
            code = b.read_va(va, min(size, 256))
            tail_crc = _compute_tail_crc(code)
            ms = self.match_bytes(code, size, compiler, tail_crc)
            if ms:
                results.append((va, ms[0]))
                continue
            ms = self.match_structural(b, va, compiler)
            if ms:
                results.append((va, ms[0]))

        return results

    # -- Compiler fingerprinting ------------------------------------------

    def fingerprint(self, b: Binary) -> dict:
        """Heuristically determine the compiler used to build *b*."""
        evidence: list[str] = []
        scores: dict[str, float] = {}

        import_names = _scan_import_names(b)

        # MSVC CRT imports
        msvc_crt = sum(1 for n in import_names if "msvcr" in n or "vcruntime" in n or "msvcp" in n)
        if msvc_crt:
            scores["msvc"] = scores.get("msvc", 0.0) + 0.4
            evidence.append(f"MSVC CRT imports: {msvc_crt}")

        # GCC/MinGW hints
        mingw_hints = sum(1 for n in import_names
                          for hint in _MINGW_IMPORT_HINTS if hint.decode() in n)
        if mingw_hints:
            scores["gcc"] = scores.get("gcc", 0.0) + 0.4
            evidence.append(f"MinGW import hints: {mingw_hints}")

        # Rich header product IDs
        pe_bytes = b.read_va(b.base, min(0x1000, b.pe.OPTIONAL_HEADER.SizeOfHeaders))
        rich = _parse_rich_header(pe_bytes)
        if rich is not None:
            for (comp_id, _), count in rich.items():
                if comp_id in (0x0001, 0x00C7, 0x00FF, 0x0102):  # MSVC linkers
                    scores["msvc"] = scores.get("msvc", 0.0) + 0.3
                    evidence.append(f"Rich header MSVC comp_id=0x{comp_id:04X} x{count}")
                    break
        elif pe_bytes.find(b"Rich") < 0:
            # No Rich header at all: strongly suggests GCC/Clang
            scores["gcc"] = scores.get("gcc", 0.0) + 0.2
            evidence.append("No Rich header (GCC/Clang indicator)")

        # Code byte pattern scanning on the first 0x10000 bytes
        scan_bytes = b.read_va(b.base, min(0x10000, b.pe.OPTIONAL_HEADER.SizeOfImage))
        msvc_hits = sum(1 for pat in _MSVC_MARKER_PATTERNS if pat in scan_bytes)
        gcc_hits = sum(1 for pat in _GCC_MARKER_PATTERNS if pat in scan_bytes)
        if msvc_hits:
            scores["msvc"] = scores.get("msvc", 0.0) + 0.1 * msvc_hits
            evidence.append(f"MSVC code patterns: {msvc_hits}")
        if gcc_hits:
            scores["gcc"] = scores.get("gcc", 0.0) + 0.1 * gcc_hits
            evidence.append(f"GCC code patterns: {gcc_hits}")

        if not scores:
            return {"compiler": "unknown", "confidence": 0.0, "evidence": evidence}

        best = max(scores, key=scores.get)
        return {
            "compiler": best,
            "confidence": min(scores[best], 1.0),
            "evidence": evidence,
        }

    # -- Build from manifest -----------------------------------------------

    def build_from_manifest(
        self, manifest_path: str | Path,
        csv_paths: list[str | Path] | None = None,
    ) -> int:
        """Build the database from a JSON manifest.

        manifest.json format:
        {
          "compiler": "msvc",
          "source": "TombRaiderLegend",
          "functions": [
            {
              "name": "func_name",
              "va": "0x401000",
              "binary": "trl.exe"
            },
            ...
          ]
        }
        """
        manifest_path = Path(manifest_path)
        with open(manifest_path) as f:
            manifest = json.load(f)

        compiler = manifest.get("compiler", "")
        source = manifest.get("source", str(manifest_path))
        category = manifest.get("category", "")
        added = 0

        # Optional: load address overrides from CSV
        addr_overrides: dict[str, int] = {}
        for csv_path in (csv_paths or []):
            with open(csv_path, newline="") as cf:
                for row in csv.DictReader(cf):
                    addr_overrides[row["name"]] = int(row["va"], 16)

        for func in manifest.get("functions", []):
            name = func["name"]
            binary_path = Path(manifest_path).parent / func["binary"]
            if not binary_path.exists():
                print(f"  WARNING: binary not found: {binary_path}")
                continue

            va = addr_overrides.get(name, int(func.get("va", "0"), 16))
            if not va:
                print(f"  WARNING: no VA for {name}")
                continue

            try:
                b = Binary(str(binary_path))
                code = b.read_va(va, 256)
                if len(code) < 32:
                    print(f"  WARNING: too little code for {name} @ 0x{va:X}")
                    continue

                # Compute mask: non-relocation bytes are 0xFF, relocation bytes are 0x00
                # Simple heuristic: mask out 4-byte aligned sequences that look like pointers
                mask = bytearray(b"\xFF" * len(code))
                for i in range(0, len(code) - 3, 1):
                    candidate = int.from_bytes(code[i:i+4], "little")
                    if b.base <= candidate < b.base + b.pe.OPTIONAL_HEADER.SizeOfImage:
                        mask[i:i+4] = b"\x00\x00\x00\x00"

                func_size = func.get("size", 0)
                tail_crc = _compute_tail_crc(code)

                self.add_byte_sig(
                    name=name,
                    pattern=bytes(code[:32]),
                    mask=bytes(mask[:32]),
                    func_size=func_size,
                    tail_crc=tail_crc,
                    compiler=compiler,
                    source=source,
                    category=category,
                )
                added += 1
            except Exception as exc:
                print(f"  ERROR: {name}: {exc}")

        return added


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main() -> None:
    ap = argparse.ArgumentParser(
        description="Signature database for TRL reverse engineering"
    )
    sub = ap.add_subparsers(dest="command", required=True)

    # build
    bp = sub.add_parser("build", help="Build signature DB from a manifest")
    bp.add_argument("manifest", help="JSON manifest path")
    bp.add_argument("-o", "--output", default=str(DEFAULT_DB_PATH),
                    help="Output DB path")
    bp.add_argument("--csv", nargs="*", default=[],
                    help="Optional CSV address override files")

    # scan
    sp = sub.add_parser("scan", help="Scan a binary for known functions")
    sp.add_argument("binary", help="PE binary path")
    sp.add_argument("-d", "--db", default=str(DEFAULT_DB_PATH),
                    help="Signature DB path")
    sp.add_argument("--compiler", default="",
                    help="Preferred compiler hint")

    # identify
    ip = sub.add_parser("identify", help="Identify a single function")
    ip.add_argument("binary", help="PE binary path")
    ip.add_argument("va", help="Virtual address (hex)")
    ip.add_argument("-d", "--db", default=str(DEFAULT_DB_PATH),
                    help="Signature DB path")
    ip.add_argument("--compiler", default="",
                    help="Preferred compiler hint")

    # fingerprint
    fp = sub.add_parser("fingerprint", help="Fingerprint compiler of a binary")
    fp.add_argument("binary", help="PE binary path")

    # pull
    pp = sub.add_parser("pull", help="Pull signature DB from HuggingFace")
    pp.add_argument("--repo", default=_HF_REPO_DEFAULT,
                    help="HuggingFace repo (owner/name)")
    pp.add_argument("--sources", action="store_true",
                    help="Also pull source JSON files")

    args = ap.parse_args()

    if args.command == "build":
        db = SignatureDB(args.output)
        n = db.build_from_manifest(args.manifest, args.csv)
        print(f"Added {n} signatures to {args.output}")
        db.close()

    elif args.command == "scan":
        db = SignatureDB(args.db)
        b = Binary(args.binary)
        results = db.scan(b, args.compiler)
        for va, m in results:
            print(f"  0x{va:X}  {m.name}  ({m.tier}, {m.confidence:.0%})")
        db.close()

    elif args.command == "identify":
        db = SignatureDB(args.db)
        b = Binary(args.binary)
        va = int(args.va, 16)
        m = db.identify(b, va, args.compiler)
        if m:
            print(f"  {m.name}")
            print(f"  confidence: {m.confidence:.0%}")
            print(f"  tier:       {m.tier}")
            print(f"  compiler:   {m.compiler}")
            print(f"  source:     {m.source}")
            print(f"  category:   {m.category}")
        else:
            print("  No match")
        db.close()

    elif args.command == "fingerprint":
        b = Binary(args.binary)
        db = SignatureDB(":memory:")
        result = db.fingerprint(b)
        print(f"  Compiler:   {result['compiler']}")
        print(f"  Confidence: {result['confidence']:.0%}")
        for ev in result["evidence"]:
            print(f"    - {ev}")
        db.close()

    elif args.command == "pull":
        data_dir = DEFAULT_DB_PATH.parent
        db_url = _HF_URL_TEMPLATE.format(repo=args.repo, path="signatures.db")
        print(f"Downloading from {args.repo}...")
        _download_file(db_url, str(data_dir / "signatures.db"))
        if args.sources:
            _pull_sources(args.repo, str(data_dir / "sources"))
        print("Done.")


if __name__ == "__main__":
    main()
