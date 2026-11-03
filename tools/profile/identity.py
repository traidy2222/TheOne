"""Small, deterministic identities for the executable and its relevant source inputs."""
import hashlib
import subprocess
from pathlib import Path


def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def source_identity(engine, consumer, game, binary):
    extensions = {".cpp", ".h", ".hpp", ".inl", ".cmake", ".vert", ".frag", ".glsl"}
    roots = [("engine", engine, ["src", "include", "shaders", "cmake", "CMakeLists.txt"]),
             ("consumer", consumer, ["shared", f"games/{game}/src", "CMakeLists.txt"])]
    files = []
    for label, root, folders in roots:
        for folder in folders:
            target = root / folder
            candidates = [target] if target.is_file() else target.rglob("*")
            for path in candidates:
                if path.is_file() and (path.suffix in extensions or path.name == "CMakeLists.txt"):
                    files.append((f"{label}/{path.relative_to(root).as_posix()}", path))
    h = hashlib.sha256()
    newer = []
    for name, path in sorted(files):
        h.update(name.encode() + b"\0" + bytes.fromhex(digest(path)))
        if path.stat().st_mtime_ns > binary.stat().st_mtime_ns:
            newer.append(name)
    commits = {}
    for label, root, _ in roots:
        result = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True, text=True)
        commits[label] = result.stdout.strip() if result.returncode == 0 else None
    return {"sha256": h.hexdigest(), "files": len(files), "commits": commits,
            "sources_newer_than_executable": newer}
