"""Keep runtime reports together without changing explicitly supplied paths."""
from pathlib import Path

LOG_DIR = Path(__file__).resolve().parents[1] / "logs"


def resolve_log_path(path):
    """Bare filenames go to project logs; absolute/custom directory paths stay put."""
    if path is None:
        return None
    path = Path(path)
    if not path.is_absolute() and path.parent == Path("."):
        path = LOG_DIR / path.name
    return path


LOG_HELP = "Log path; a bare filename is saved in esp32c61/logs (parent created automatically)"
