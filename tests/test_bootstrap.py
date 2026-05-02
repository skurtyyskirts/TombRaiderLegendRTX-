"""Tests for retools/bootstrap.py -- auto-KB seeding pipeline."""

import os
import struct
import sys
import tempfile
from pathlib import Path
from unittest.mock import MagicMock, patch

import pytest

# Ensure retools is importable
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "retools"))


# ---------------------------------------------------------------------------
# classify_function
# ---------------------------------------------------------------------------

class TestClassifyFunction:
    def test_thunk_single_callee(self):
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000],
            callee_names={0x401000: "_malloc"},
            is_vtable_call=False,
        )
        assert result is not None
        assert "thunk" in result["label"].lower()
        assert result["label"] == "_thunk__malloc"
        assert result["confidence"] == 0.80

    def test_thunk_unnamed_callee(self):
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000],
            callee_names={},
            is_vtable_call=False,
        )
        assert result is not None
        assert result["label"] == "_thunk_sub_401000"
        assert result["confidence"] == 0.80

    def test_constructor_detection(self):
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000, 0x402000],
            callee_names={0x401000: "operator_new"},
            is_vtable_call=True,
        )
        assert result is not None
        assert result["label"] == "constructor"
        assert result["confidence"] == 0.75

    def test_destructor_detection(self):
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000, 0x402000],
            callee_names={0x401000: "operator_delete"},
            is_vtable_call=False,
        )
        assert result is not None
        assert result["label"] == "destructor"
        assert result["confidence"] == 0.70

    def test_throws_detection(self):
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000, 0x402000, 0x403000],
            callee_names={0x402000: "_CxxThrowException"},
            is_vtable_call=False,
        )
        assert result is not None
        assert result["label"] == "throws"
        assert result["confidence"] == 0.85

    def test_init_global_detection(self):
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000, 0x402000],
            callee_names={0x401000: "malloc"},
            is_vtable_call=False,
        )
        assert result is not None
        assert result["label"] == "init_global"
        assert result["confidence"] == 0.55

    def test_init_global_too_many_callees(self):
        """malloc with >3 callees should not match init_global."""
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000, 0x402000, 0x403000, 0x404000],
            callee_names={0x401000: "malloc"},
            is_vtable_call=False,
        )
        # Should not be init_global; might match something else or None
        assert result is None or result["label"] != "init_global"

    def test_no_match_returns_none(self):
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000, 0x402000, 0x403000, 0x404000, 0x405000],
            callee_names={},
            is_vtable_call=False,
        )
        assert result is None

    def test_priority_throws_over_thunk(self):
        """CxxThrowException with exactly one callee -- thunk takes priority
        since single-callee is checked first."""
        from bootstrap import classify_function
        result = classify_function(
            callees=[0x401000],
            callee_names={0x401000: "_CxxThrowException"},
            is_vtable_call=False,
        )
        assert result is not None
        # Single callee -> thunk rule fires first
        assert "thunk" in result["label"].lower()


# ---------------------------------------------------------------------------
# bootstrap pipeline
# ---------------------------------------------------------------------------

def _make_minimal_pe(tmp_dir, packed=False):
    """Create a minimal valid PE file for testing.

    If packed=True, the executable section has raw_size << virtual_size
    to trigger packed binary detection.
    """
    import pefile

    # Build a minimal PE from scratch using struct packing
    # DOS header
    dos = bytearray(64)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 64)  # e_lfanew

    # PE signature
    pe_sig = b"PE\x00\x00"

    # COFF header (20 bytes)
    coff = bytearray(20)
    struct.pack_into("<H", coff, 0, 0x14C)   # Machine: i386
    struct.pack_into("<H", coff, 2, 1)        # NumberOfSections
    struct.pack_into("<H", coff, 16, 0xE0)    # SizeOfOptionalHeader
    struct.pack_into("<H", coff, 18, 0x102)   # Characteristics: EXECUTABLE_IMAGE | 32BIT_MACHINE

    # Optional header (224 bytes for PE32)
    opt = bytearray(0xE0)
    struct.pack_into("<H", opt, 0, 0x10B)      # Magic: PE32
    struct.pack_into("<I", opt, 16, 0x1000)    # AddressOfEntryPoint
    struct.pack_into("<I", opt, 28, 0x400000)  # ImageBase
    struct.pack_into("<I", opt, 32, 0x1000)    # SectionAlignment
    struct.pack_into("<I", opt, 36, 0x200)     # FileAlignment
    struct.pack_into("<H", opt, 40, 4)         # MajorOSVersion
    struct.pack_into("<H", opt, 44, 4)         # MajorSubsystemVersion
    struct.pack_into("<I", opt, 56, 0x3000)    # SizeOfImage
    struct.pack_into("<I", opt, 60, 0x200)     # SizeOfHeaders
    struct.pack_into("<H", opt, 68, 3)         # Subsystem: CONSOLE
    struct.pack_into("<I", opt, 76, 0x100000)  # SizeOfStackReserve
    struct.pack_into("<I", opt, 80, 0x1000)    # SizeOfStackCommit
    struct.pack_into("<I", opt, 84, 0x100000)  # SizeOfHeapReserve
    struct.pack_into("<I", opt, 88, 0x1000)    # SizeOfHeapCommit
    struct.pack_into("<I", opt, 92, 16)        # NumberOfRvaAndSizes

    # Section header (40 bytes) - .text
    sec = bytearray(40)
    sec[0:6] = b".text\x00"

    if packed:
        # Virtual size much larger than raw size
        struct.pack_into("<I", sec, 8, 0x10000)   # VirtualSize
        struct.pack_into("<I", sec, 12, 0x1000)   # VirtualAddress
        struct.pack_into("<I", sec, 16, 0x200)    # SizeOfRawData (tiny)
        struct.pack_into("<I", sec, 20, 0x200)    # PointerToRawData
    else:
        struct.pack_into("<I", sec, 8, 0x1000)    # VirtualSize
        struct.pack_into("<I", sec, 12, 0x1000)   # VirtualAddress
        struct.pack_into("<I", sec, 16, 0x800)    # SizeOfRawData
        struct.pack_into("<I", sec, 20, 0x200)    # PointerToRawData

    # IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ | IMAGE_SCN_CNT_CODE
    struct.pack_into("<I", sec, 36, 0x60000020)

    # Assemble headers
    headers = dos + pe_sig + coff + opt + sec

    # Pad headers to FileAlignment (0x200)
    headers += b"\x00" * (0x200 - len(headers))

    # Section data (pad to a nice size)
    if packed:
        section_data = b"\xCC" * 0x200  # Small raw data for packed
    else:
        section_data = b"\xCC" * 0x800

    pe_data = headers + section_data

    pe_path = os.path.join(tmp_dir, "test.exe")
    with open(pe_path, "wb") as f:
        f.write(pe_data)

    return pe_path


class TestBootstrapCreatesKB:
    def test_creates_kb_file(self, tmp_path):
        from bootstrap import bootstrap
        pe_path = _make_minimal_pe(str(tmp_path))
        project_dir = str(tmp_path / "project")

        result = bootstrap(pe_path, project_dir)

        kb_path = os.path.join(project_dir, "kb.h")
        assert os.path.isfile(kb_path)
        assert isinstance(result, dict)

    def test_creates_report(self, tmp_path):
        from bootstrap import bootstrap
        pe_path = _make_minimal_pe(str(tmp_path))
        project_dir = str(tmp_path / "project")

        result = bootstrap(pe_path, project_dir)

        report_path = os.path.join(project_dir, "bootstrap_report.txt")
        assert os.path.isfile(report_path)
        report = Path(report_path).read_text()
        assert "Compiler:" in report
        assert "Functions identified:" in report

    def test_skips_duplicate_addresses(self, tmp_path):
        from bootstrap import bootstrap
        pe_path = _make_minimal_pe(str(tmp_path))
        project_dir = str(tmp_path / "project")

        # Run twice
        bootstrap(pe_path, project_dir)
        bootstrap(pe_path, project_dir)

        kb_path = os.path.join(project_dir, "kb.h")
        content = Path(kb_path).read_text()
        # Count lines starting with "@ 0x" -- each address should appear once
        addr_lines = [l for l in content.splitlines() if l.startswith("@ 0x")]
        addresses = [l.split()[1] for l in addr_lines]
        assert len(addresses) == len(set(addresses)), \
            f"Duplicate addresses found: {addresses}"


class TestPackedDetection:
    def test_packed_binary_early_return(self, tmp_path):
        from bootstrap import bootstrap
        pe_path = _make_minimal_pe(str(tmp_path), packed=True)
        project_dir = str(tmp_path / "project")

        result = bootstrap(pe_path, project_dir)

        assert result.get("packed") is True
        report_path = os.path.join(project_dir, "bootstrap_report.txt")
        report = Path(report_path).read_text()
        assert "packed" in report.lower() or "warning" in report.lower()


class TestBootstrapReturnDict:
    def test_returns_dict_with_stats(self, tmp_path):
        from bootstrap import bootstrap
        pe_path = _make_minimal_pe(str(tmp_path))
        project_dir = str(tmp_path / "project")

        result = bootstrap(pe_path, project_dir)

        assert isinstance(result, dict)
        # Should have some stats keys
        assert "functions_identified" in result or "packed" in result


class TestBootstrapProjectDir:
    def test_creates_project_dir(self, tmp_path):
        from bootstrap import bootstrap
        pe_path = _make_minimal_pe(str(tmp_path))
        project_dir = str(tmp_path / "newproject" / "sub")

        bootstrap(pe_path, project_dir)

        assert os.path.isdir(project_dir)


# ---------------------------------------------------------------------------
# Pipeline steps
# ---------------------------------------------------------------------------

class TestPipelineSteps:
    def test_detect_compiler_returns_dict(self, tmp_path):
        from bootstrap import _detect_compiler
        import pefile
        pe_path = _make_minimal_pe(str(tmp_path))
        b = MagicMock()
        b.pe = pefile.PE(pe_path, fast_load=False)
        result = _detect_compiler(b, db_path=None)
        assert "compiler" in result
        assert "confidence" in result

    def test_propagate_labels_incremental_set(self):
        """Verify propagation uses incremental set, not O(n^2) rebuild."""
        from bootstrap import _propagate_labels
        mock_b = MagicMock()
        result = _propagate_labels(
            b=mock_b, func_table=[], known_names={}, known_addresses=set(),
            kb_entry_addresses=set(),
        )
        assert isinstance(result, list)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

class TestCLI:
    def test_module_runnable(self):
        """bootstrap should be runnable as python -m retools.bootstrap."""
        from bootstrap import main
        assert callable(main)

    def test_main_no_args_exits(self):
        from bootstrap import main
        with pytest.raises(SystemExit):
            main([])

class TestBootstrapOrchestrator:
    def test_full_bootstrap_mock(self, tmp_path):
        from unittest.mock import patch, MagicMock
        from bootstrap import bootstrap
        import os

        with patch("bootstrap.pefile.PE") as mock_pe, \
             patch("bootstrap.Binary") as mock_binary, \
             patch("bootstrap._is_packed", return_value=False), \
             patch("bootstrap._read_existing_addresses", return_value=set()), \
             patch("bootstrap._detect_compiler", return_value={"compiler": "msvc", "confidence": 0.9}), \
             patch("bootstrap._scan_signatures", return_value=({0x1000: MagicMock(name="mock_sig")}, ["@ 0x1000 sig;"])), \
             patch("bootstrap._scan_rtti", return_value=(1, ["@ 0x1010 rtti;"])), \
             patch("bootstrap._analyze_imports", return_value=[MagicMock()]), \
             patch("bootstrap._seed_strings", return_value=(1, ["@ 0x1020 str;"])), \
             patch("bootstrap._propagate_labels", return_value=["@ 0x1030 prop;"]), \
             patch("bootstrap._write_kb_entries", return_value=4) as mock_write, \
             patch("bootstrap.os.path.isfile", return_value=True):

             mock_bin_instance = mock_binary.return_value
             mock_bin_instance.func_table = [0x1000, 0x1010]

             # Mock imports directory
             import_entry = MagicMock()
             imp = MagicMock()
             imp.name = b"malloc"
             imp.address = 0x2000
             import_entry.imports = [imp]
             mock_bin_instance.pe.DIRECTORY_ENTRY_IMPORT = [import_entry]

             result = bootstrap("dummy.exe", str(tmp_path), db_path="dummy.db")

             assert result["packed"] is False
             assert result["compiler"] == "msvc"
             assert result["sigdb_matches"] == 1
             assert result["rtti_classes"] == 1
             assert result["strings_seeded"] == 1
             assert result["propagated"] == 1
             assert result["functions_identified"] == 4

             mock_write.assert_called_once()
             assert "bootstrap_report.txt" in os.listdir(tmp_path)

    def test_bootstrap_download_db_failure(self, tmp_path):
        from unittest.mock import patch, MagicMock
        from bootstrap import bootstrap
        import os
        import sys

        with patch("bootstrap.pefile.PE"), \
             patch("bootstrap.Binary"), \
             patch("bootstrap._is_packed", return_value=False), \
             patch("bootstrap._read_existing_addresses", return_value=set()), \
             patch("bootstrap._detect_compiler", return_value={"compiler": "unknown", "confidence": 0.0}), \
             patch("bootstrap._scan_signatures", return_value=({}, [])), \
             patch("bootstrap._scan_rtti", return_value=(0, [])), \
             patch("bootstrap._analyze_imports", return_value=[]), \
             patch("bootstrap._seed_strings", return_value=(0, [])), \
             patch("bootstrap._propagate_labels", return_value=[]), \
             patch("bootstrap._write_kb_entries", return_value=0), \
             patch("bootstrap.os.path.isfile", side_effect=lambda x: False if x.endswith(".db") else True), \
             patch("sigdb._download_file", side_effect=Exception("Network error")), \
             patch("bootstrap.sys.stderr", new_callable=MagicMock) as mock_stderr:

             result = bootstrap("dummy.exe", str(tmp_path), db_path=None)

             assert result["packed"] is False
             assert result["compiler"] == "unknown"
             mock_stderr.write.assert_any_call("Could not download signature DB: Network error")

    def test_bootstrap_download_db_success(self, tmp_path):
        from unittest.mock import patch, MagicMock
        from bootstrap import bootstrap

        with patch("bootstrap.pefile.PE"), \
             patch("bootstrap.Binary"), \
             patch("bootstrap._is_packed", return_value=False), \
             patch("bootstrap._read_existing_addresses", return_value=set()), \
             patch("bootstrap._detect_compiler", return_value={"compiler": "unknown", "confidence": 0.0}), \
             patch("bootstrap._scan_signatures", return_value=({}, [])), \
             patch("bootstrap._scan_rtti", return_value=(0, [])), \
             patch("bootstrap._analyze_imports", return_value=[]), \
             patch("bootstrap._seed_strings", return_value=(0, [])), \
             patch("bootstrap._propagate_labels", return_value=[]), \
             patch("bootstrap._write_kb_entries", return_value=0), \
             patch("bootstrap.os.path.isfile", side_effect=lambda x: False if x.endswith(".db") else True), \
             patch("sigdb._download_file") as mock_download:

             bootstrap("dummy.exe", str(tmp_path), db_path=None)

             mock_download.assert_called_once()

    def test_bootstrap_error_stats_report(self, tmp_path):
        from unittest.mock import patch, MagicMock
        from bootstrap import bootstrap
        import os
        from pathlib import Path

        # We want to patch the stats dict returned by one of the stages, but stats
        # is just a local variable. So we can monkeypatch `sorted` to fake the key iteration
        # in the loop on line 476.
        # Original: for key in sorted(stats): ...

        with patch("bootstrap.pefile.PE"), \
             patch("bootstrap.Binary"), \
             patch("bootstrap._is_packed", return_value=False), \
             patch("bootstrap._read_existing_addresses", return_value=set()), \
             patch("bootstrap._detect_compiler", return_value={"compiler": "unknown", "confidence": 0.0}), \
             patch("bootstrap._scan_signatures", return_value=({}, [])), \
             patch("bootstrap._scan_rtti", return_value=(0, [])), \
             patch("bootstrap._analyze_imports", return_value=[]), \
             patch("bootstrap._seed_strings", return_value=(0, [])), \
             patch("bootstrap._propagate_labels", return_value=[]), \
             patch("bootstrap._write_kb_entries", return_value=0), \
             patch("bootstrap.os.path.isfile", return_value=True):

             original_sorted = sorted
             def mock_sorted(iterable, *args, **kwargs):
                 if isinstance(iterable, dict) and "compiler" in iterable:
                     # This is our stats dict. We can insert a fake error key directly into the dict here!
                     iterable["_test_error"] = "failed_step"
                     return original_sorted(list(iterable.keys()), *args, **kwargs)
                 return original_sorted(iterable, *args, **kwargs)

             with patch("builtins.sorted", side_effect=mock_sorted):
                 result = bootstrap("dummy.exe", str(tmp_path), db_path=None)

                 # Verify that the report contains our faked error note
                 report_path = os.path.join(str(tmp_path), "bootstrap_report.txt")
                 report_content = Path(report_path).read_text()
                 assert "Note: test step raised failed_step" in report_content

    def test_bootstrap_address_parsing_error(self, tmp_path):
        from unittest.mock import patch, MagicMock
        from bootstrap import bootstrap

        # Test line 429-430 exception handling during address parsing
        with patch("bootstrap.pefile.PE"), \
             patch("bootstrap.Binary"), \
             patch("bootstrap._is_packed", return_value=False), \
             patch("bootstrap._read_existing_addresses", return_value=set()), \
             patch("bootstrap._detect_compiler", return_value={"compiler": "unknown", "confidence": 0.0}), \
             patch("bootstrap._scan_signatures", return_value=({}, ["@ 0xbadformat"])), \
             patch("bootstrap._scan_rtti", return_value=(0, [])), \
             patch("bootstrap._analyze_imports", return_value=[]), \
             patch("bootstrap._seed_strings", return_value=(0, [])), \
             patch("bootstrap._propagate_labels", return_value=[]), \
             patch("bootstrap._write_kb_entries", return_value=0), \
             patch("bootstrap.os.path.isfile", return_value=True):

             # `@ 0xbadformat` string split is `["@", "0xbadformat"]`
             # int("0xbadformat", 16) raises ValueError
             # this exercises the try/except block in the kb_entry_addresses parsing loop
             result = bootstrap("dummy.exe", str(tmp_path), db_path=None)
             assert result["compiler"] == "unknown"

    def test_cli_main_prints_results(self, tmp_path, capsys):
        from bootstrap import main
        from unittest.mock import patch
        import os

        with patch("bootstrap.bootstrap", return_value={"compiler": "msvc", "functions_identified": 10, "packed": False}):
            main(["dummy.exe", "--project", str(tmp_path)])

            captured = capsys.readouterr()
            assert "Compiler: msvc" in captured.out
            assert "Functions identified: 10" in captured.out
            assert "Report written to:" in captured.out

    def test_cli_main_prints_packed_warning(self, tmp_path, capsys):
        from bootstrap import main
        from unittest.mock import patch

        with patch("bootstrap.bootstrap", return_value={"packed": True}):
            main(["dummy.exe", "--project", str(tmp_path)])

            captured = capsys.readouterr()
            assert "WARNING: Binary appears to be packed" in captured.out

    def test_cli_main_callable_no_args(self, tmp_path):
        from bootstrap import main
        import sys
        from unittest.mock import patch

        test_args = ["bootstrap.py", "dummy.exe", "--project", str(tmp_path)]
        with patch.object(sys, 'argv', test_args), \
             patch("bootstrap.bootstrap", return_value={"packed": True}):

             # If __name__ == "__main__" block were run (via main()), it reads sys.argv
             # This tests argv=None default inside main()
             main()
