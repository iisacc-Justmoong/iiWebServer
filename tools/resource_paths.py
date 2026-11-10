"""Resolve project-owned test resources without following escaping links."""
from pathlib import Path

def resource_subdir(project, *parts):
    project = Path(project).resolve(strict=True)
    root = project / "resources"
    if not parts or any(not part or part in (".", "..") or Path(part).name != part for part in parts):
        raise ValueError("Resource path components must be simple names")
    current = root
    for part in (None, *parts):
        if part is not None:
            current = current / part
        if current.is_symlink():
            raise ValueError("Resource paths cannot traverse symbolic links")
    if not current.resolve().is_relative_to(root.resolve()):
        raise ValueError("Resource path escapes its root")
    return current
