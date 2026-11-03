"""Compare only matching phase/focus/resolution/present-mode groups; expose gaps."""
import argparse
import json
from pathlib import Path
from trace import compatibility, metadata, read_frames, summarize

METRICS = ["frame_wall", "renderer_cpu", "cpu_cycles_total", "sampled_input_to_submit",
           "polled_input_to_submit", "presentation_wait", "fence", "submit_to_gpu",
           "texture_bytes", "pooled_staging_bytes", "pending_draws"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--phase", help="Compare one replay phase, retaining focus/surface separation")
    args = parser.parse_args()
    before, after = metadata(args.before), metadata(args.after)
    def selected(path):
        frames = read_frames(path)
        return {n: f for n, f in frames.items() if not args.phase or f["phase"] == args.phase}
    a, b = [summarize(selected(p), METRICS) for p in (args.before, args.after)]
    report = {"warnings": compatibility(before, after, compare_length=not args.phase), "phase": args.phase,
              "builds": [before["exe_sha256"], after["exe_sha256"]],
              "flags": [before["flags"], after["flags"]],
              "unmatched_before": sorted(a.keys()-b.keys()), "unmatched_after": sorted(b.keys()-a.keys()),
              "matched": {key: {"before": a[key], "after": b[key]} for key in sorted(a.keys() & b.keys())}}
    if not report["matched"]:
        report["warnings"].append("No matching capture contexts; no performance comparison is valid")
    result = json.dumps(report, indent=2)+"\n"
    if args.output:
        args.output.write_text(result, encoding="utf-8")
        print(f"{len(report['matched'])} matched contexts; {len(report['warnings'])} warnings; {args.output}")
    else:
        print(result)


if __name__ == "__main__":
    main()
