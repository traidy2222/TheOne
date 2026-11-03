"""Read long-form engine traces and compare matching phase/focus/surface contexts."""
import csv
import json
import statistics
import math
from collections import defaultdict
from pathlib import Path


def read_frames(path):
    frames = defaultdict(dict)
    # Startup uses frame -1 repeatedly. Keep it out of measured distributions.
    with Path(path).open(newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["frame", "phase", "metric", "value"]:
            raise ValueError("Unsupported trace columns")
        for row in reader:
            frame = int(row["frame"])
            if frame < 0:
                continue
            entry = frames[frame]
            entry["phase"] = row["phase"]
            value = float(row["value"])
            if not math.isfinite(value):
                raise ValueError("Trace contains a non-finite metric")
            entry[row["metric"]] = value
    if not frames or any("frame_wall" not in frame for frame in frames.values()):
        raise ValueError("Trace has no complete measured frames or ends mid-frame")
    return dict(sorted(frames.items()))


def context(frame):
    return (frame["phase"], frame.get("focused"), frame.get("width"), frame.get("height"),
            frame.get("present_mode"), frame.get("visible"))


def distribution(values):
    values = sorted(values)
    return {"count": len(values), "mean": statistics.mean(values), "p50": statistics.median(values),
            "p95": values[int((len(values)-1)*.95)], "p99": values[int((len(values)-1)*.99)],
            "max": values[-1]}


def summarize(frames, metrics):
    groups = defaultdict(list)
    for frame in frames.values():
        groups[context(frame)].append(frame)
    return {str(key): {metric: distribution([f[metric] for f in group if metric in f])
                      for metric in metrics if any(metric in f for f in group)}
            for key, group in groups.items()}


def metadata(path):
    return json.loads(Path(str(path) + ".meta.json").read_text())


def compatibility(before, after, compare_length=True):
    issues = []
    keys = ["scene_sha256", "gpu", "schema", "extra_args"]
    if compare_length:
        keys.append("frames_requested")
    for key in keys:
        if before.get(key) != after.get(key):
            issues.append(f"Different {key}")
    for name, data in (("before", before), ("after", after)):
        if data.get("exit_code") != 0 or not data.get("source_unchanged") or not data.get("binary_unchanged"):
            issues.append(f"{name}: failed capture or binary/source changed during capture")
        if data.get("frames_measured") != data.get("frames_requested"):
            issues.append(f"{name}: requested frame count was not captured")
        if not data.get("gpu") or data.get("schema") != 1:
            issues.append(f"{name}: missing GPU identity or unsupported metadata schema")
        if data.get("source_before", {}).get("sources_newer_than_executable"):
            issues.append(f"{name}: source inputs are newer than the executable; rebuild provenance is uncertain")
    if before.get("shaders") != after.get("shaders"):
        issues.append("Shader binaries differ; verify visual equivalence before claiming an optimization")
    if before.get("presentation") and after.get("presentation"):
        if set(before["presentation"].get("modes", {})) != set(after["presentation"].get("modes", {})):
            issues.append("Windows composition paths differ; treat display timing comparisons separately")
    elif bool(before.get("presentation")) != bool(after.get("presentation")):
        issues.append("Only one capture has Windows presentation events")
    return issues
