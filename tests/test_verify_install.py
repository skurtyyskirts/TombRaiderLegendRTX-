import sys
from pathlib import Path
from unittest.mock import patch, MagicMock
import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import verify_install

class TestVerifyInstall:
    @pytest.fixture(autouse=True)
    def clear_results(self):
        verify_install.results.clear()
        yield
        verify_install.results.clear()

    @patch("verify_install.importlib.import_module")
    def test_check_python_deps_success(self, mock_import_module):
        mock_mod = MagicMock()
        mock_mod.__version__ = "1.2.3"
        mock_import_module.return_value = mock_mod

        verify_install.check_python_deps()

        assert len(verify_install.results) == 5
        assert ("pip:pefile", verify_install.PASS, "v1.2.3") in verify_install.results
        assert ("pip:capstone", verify_install.PASS, "v1.2.3") in verify_install.results

    @patch("verify_install.importlib.import_module")
    def test_check_python_deps_fallback_version(self, mock_import_module):
        mock_mod = MagicMock()
        del mock_mod.__version__
        mock_mod.VERSION = "4.5.6"
        mock_import_module.return_value = mock_mod

        verify_install.check_python_deps()

        assert len(verify_install.results) == 5
        assert ("pip:pefile", verify_install.PASS, "v4.5.6") in verify_install.results

    @patch("verify_install.importlib.import_module")
    def test_check_python_deps_no_version(self, mock_import_module):
        mock_mod = MagicMock()
        del mock_mod.__version__
        del mock_mod.VERSION
        mock_import_module.return_value = mock_mod

        verify_install.check_python_deps()

        assert len(verify_install.results) == 5
        assert ("pip:pefile", verify_install.PASS, "v?") in verify_install.results

    @patch("verify_install.importlib.import_module")
    def test_check_python_deps_import_error(self, mock_import_module):
        mock_import_module.side_effect = ImportError("No module named foo")

        verify_install.check_python_deps()

        assert len(verify_install.results) == 5
        assert ("pip:pefile", verify_install.FAIL, "No module named foo") in verify_install.results

    @patch("verify_install.importlib.import_module")
    def test_check_python_deps_mixed_results(self, mock_import_module):
        def side_effect(name):
            if name == "pefile":
                raise ImportError("No module named pefile")
            mock_mod = MagicMock()
            mock_mod.__version__ = "1.0"
            return mock_mod

        mock_import_module.side_effect = side_effect

        verify_install.check_python_deps()

        assert len(verify_install.results) == 5
        assert ("pip:pefile", verify_install.FAIL, "No module named pefile") in verify_install.results
        assert ("pip:capstone", verify_install.PASS, "v1.0") in verify_install.results
