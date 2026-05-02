import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

import pytest
import logging
from unittest.mock import patch, call
from retools.dumpinfo import _load_dump

def test_load_dump_success():
    with patch('retools.dumpinfo.MinidumpFile.parse') as mock_parse, \
         patch('logging.disable') as mock_disable:
        mock_parse.return_value = "mock_dump"
        result = _load_dump("dummy_path")

        assert result == "mock_dump"
        mock_parse.assert_called_once_with("dummy_path")

        mock_disable.assert_has_calls([call(logging.CRITICAL), call(logging.NOTSET)])
        assert mock_disable.call_count == 2

def test_load_dump_exception():
    with patch('retools.dumpinfo.MinidumpFile.parse') as mock_parse, \
         patch('logging.disable') as mock_disable:
        mock_parse.side_effect = Exception("parse error")

        with pytest.raises(Exception, match="parse error"):
            _load_dump("dummy_path")

        mock_parse.assert_called_once_with("dummy_path")
        mock_disable.assert_has_calls([call(logging.CRITICAL), call(logging.NOTSET)])
        assert mock_disable.call_count == 2
