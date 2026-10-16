import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "pc_tools"))
import log_paths
from sdio_udp_receiver import EventWriter


class LogPathTests(unittest.TestCase):
    def test_bare_name_uses_project_logs(self):
        expected = Path(__file__).resolve().parents[1] / "logs" / "capture.jsonl"
        self.assertEqual(log_paths.resolve_log_path("capture.jsonl"), expected)
        self.assertEqual(log_paths.resolve_log_path("./capture.jsonl"), expected)

    def test_explicit_paths_are_preserved(self):
        for path in (Path("logs/capture.jsonl"), Path("custom/capture.jsonl"),
                     Path(tempfile.gettempdir()).resolve() / "capture.jsonl"):
            self.assertEqual(log_paths.resolve_log_path(path), path)
        self.assertIsNone(log_paths.resolve_log_path(None))

    def test_benchmark_siblings_stay_together(self):
        path = log_paths.resolve_log_path("run.jsonl")
        for suffix in (".device.json", ".stdout.log", ".stderr.log"):
            self.assertEqual(path.with_suffix(suffix).parent, log_paths.LOG_DIR)

    def test_writer_creates_directory_and_keeps_existing_events(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / "nested" / "logs"
            with patch.object(log_paths, "LOG_DIR", root), contextlib.redirect_stdout(io.StringIO()):
                for sequence in (1, 2):
                    writer = EventWriter(Path("capture.jsonl"))
                    writer.submit({"event": "summary", "sequence": sequence})
                    writer.close()
                    self.assertIsNone(writer.error)
            content = (root / "capture.jsonl").read_text(encoding="utf-8")
            self.assertEqual(len(content.splitlines()), 2)
            self.assertIn('"sequence": 1', content)
            self.assertIn('"sequence": 2', content)


if __name__ == "__main__":
    unittest.main()
