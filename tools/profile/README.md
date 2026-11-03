# Focused performance captures

`capture.py` launches a visible Robots replay, records the executable/scene/shader
hashes, source identities before and after, runtime flags, hardware name and
frame contexts in a `.meta.json` sidecar. Source timestamps newer than the binary
are flagged as uncertain build provenance; a hash alone is not proof of a build.
Captured data is never silently overwritten. Compilation is a separate step.

From the engine checkout:

```powershell
python tools/profile/capture.py --exe '../Games 2/build/bin/robots/Release/robots.exe' --scene '../Games 2/games/robots/build/robots-village.one' --cwd '../Games 2' --output build/example-before.csv --frames 1440
```

For a 240-frame first-camera/input probe, pass `--frames 240
--game-arg=--profile-probe`. This deliberately skips the editor/reload sequence.
The normal 1,440-frame replay exercises those phases. Optional
`--presentmon <path-to-PresentMon.exe>` records Windows presentation events for
only the launched PID in a uniquely named session, which is stopped on exit.

```powershell
python tools/profile/compare.py build/example-before.csv build/example-after.csv --output build/comparison.json
```

Comparisons separate phase, focus, pixel dimensions, Vulkan present mode and
visibility. Unmatched contexts are listed rather than silently pooled. `--phase
gameplay` selects just that phase. Metadata warnings expose changed/missing
inputs, changed binaries during capture, incomplete runs, different command
options or shaders, and differing Windows composition paths when both captures
include presentation events. Warnings are evidence limitations, not performance
regression thresholds. Optional Windows event captures include startup and do not
automatically provide a frame-perfect mapping to Vulkan submissions.

`sampled_input_to_submit` measures CPU keyboard-state sampling to GPU submission,
not physical click-to-display latency. `polled_input_to_submit` starts at event
polling. `cpu_cycles_*` are raw per-thread cycle deltas and must not be converted
to elapsed milliseconds; `renderer_thread_ms` is coarse Windows CPU accounting.

Run only the parser/comparison checks after changing these tools:

```powershell
python tools/profile/test_trace.py
```

Reference controls for this iteration: `THEONE_REFERENCE_INPUT_BOUNDARY=1`,
`THEONE_REFERENCE_SURFACE_REUSE=1`, `THEONE_REFERENCE_MATERIAL_ADMISSION=1`, and
`THEONE_REFERENCE_MATERIAL_WARMUP=1`. Set only the desired reference for an
isolated comparison, and remove it afterwards. Never compare a capture recorded
during compilation against an idle-machine run without identifying that difference.

Presentation diagnosis also records `presentation_wait_id`, the requested
`presentation_wait_budget` in milliseconds, the QPC `presentation_wait_host`
start time, and coarse `presentation_wait_thread_cpu`. The latter uses Windows
thread accounting and cannot resolve individual short waits accurately.
`THEONE_REFERENCE_PRESENT_WAIT_BUDGET=1` restores the former 50 ms blocking wait;
the default checks a completion worker between sleeps of at most 1 ms against a
16 ms deadline. Only the worker enters the driver wait in the normal path.
`presentation_wait_polls` records the status-check count. OS scheduling can exceed the budget,
so inspect measured waits and the downstream `fence` and `acquire` tails as well.
The wait avoids another sleep when its last observed duration would exceed the
remaining budget. `presentation_check` and `presentation_sleep` separate main-thread
status checks from sleep/wakeup time. `presentation_worker_wait` is the last driver
wait duration, paired with `presentation_worker_id`; the same sample can appear
in multiple rows. In reference mode `presentation_check` is the blocking driver call.
`SDL_TIMER_RESOLUTION` overrides are recorded in capture metadata for diagnostic
comparisons; the engine keeps SDL's normal timer policy.
