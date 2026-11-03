"""Capture a focused visible replay and bind its evidence to binary/source identities."""
import argparse
import json
import os
import platform
import subprocess
import time
from pathlib import Path
from identity import digest, source_identity
from trace import read_frames, context
from presentation_trace import PresentationTrace


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--scene", type=Path, required=True)
    parser.add_argument("--cwd", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--engine", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--frames", type=int, default=1440)
    parser.add_argument("--game-arg", action="append", default=[])
    parser.add_argument("--presentmon", type=Path)
    parser.add_argument("--timeout", type=float)
    args = parser.parse_args()
    if args.presentmon and not args.presentmon.is_file():
        parser.error("PresentMon executable does not exist")
    exe, scene, cwd, output = [p.resolve() for p in (args.exe, args.scene, args.cwd, args.output)]
    output.parent.mkdir(parents=True, exist_ok=True)
    # Captures are evidence: never silently overwrite an earlier comparison.
    if output.exists() or Path(str(output)+".meta.json").exists():
        parser.error("Output already exists; choose a new capture name")
    before = source_identity(args.engine.resolve(), cwd, exe.stem, exe)
    env = dict(os.environ, THEONE_PROFILE_WAITS="1", THEONE_PROFILE_CPU="1")
    flags = {k: v for k, v in env.items() if k.startswith("THEONE_REFERENCE_") or
             k in ("THEONE_PROFILE_WAITS", "THEONE_PROFILE_CPU", "THEONE_PROFILE_RELOAD_REPEAT", "THEONE_ASSET_WORKERS", "SDL_TIMER_RESOLUTION")}
    data = {"schema": 1, "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "exe": str(exe), "exe_sha256": digest(exe), "scene": str(scene), "scene_sha256": digest(scene),
            "platform": platform.platform(), "source_before": before, "flags": flags,
            "frames_requested": args.frames, "extra_args": args.game_arg,
            "shaders": {p.name: digest(p) for p in sorted((exe.parent/"shaders").glob("*.spv"))}}
    command = [str(exe), "--scene", str(scene), "--profile-visible", str(output),
               "--profile-frames", str(args.frames), *args.game_arg]
    start = time.monotonic()
    with Path(str(output)+".log").open("w") as stdout, Path(str(output)+".err").open("w") as stderr:
        game = subprocess.Popen(command, cwd=cwd, env=env, stdout=stdout, stderr=stderr,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        presentation = PresentationTrace(args.presentmon, game.pid, output) if args.presentmon else None
        try:
            game.wait(timeout=args.timeout or max(180, args.frames / 15 + 60))
        except subprocess.TimeoutExpired:
            game.terminate()
            game.wait()
            data["timed_out"] = True
        finally:
            if presentation:
                data["presentation"] = presentation.finish()
    data["exit_code"] = game.returncode
    data["elapsed_seconds"] = time.monotonic()-start
    data["source_after"] = source_identity(args.engine.resolve(), cwd, exe.stem, exe)
    data["source_unchanged"] = before["sha256"] == data["source_after"]["sha256"]
    data["binary_unchanged"] = data["exe_sha256"] == digest(exe)
    data["gpu"] = next((line[5:] for line in Path(str(output)+".log").read_text().splitlines()
                        if line.startswith("GPU: ")), None)
    if output.exists() and game.returncode == 0:
        try:
            frames = read_frames(output)
            data["frames_measured"] = len(frames)
            data["contexts"] = sorted({str(context(frame)) for frame in frames.values()})
        except (ValueError, TypeError, KeyError) as error:
            data["trace_error"] = str(error)
    Path(str(output)+".meta.json").write_text(json.dumps(data, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({k: data.get(k) for k in ("exit_code", "frames_measured", "source_unchanged", "binary_unchanged", "gpu")}, indent=2))
    return game.returncode


if __name__ == "__main__":
    raise SystemExit(main())
