# Presentation completion stall

Measured 2026-11-02 on Windows 11, RTX 3080, Robots at 1920x1080, FIFO,
three swapchain images and two resource frame slots.

## Trace and diagnosis

The earlier `cpu-input-after.csv` recorded a sustained 46.59 ms mean frame period,
with 42.70 ms mean presentation wait and 4.93 ms GPU work. That entire-run state
has not been reproduced in this investigation. A fresh normal capture,
`present-stall-baseline.csv`, averaged 16.69 ms per frame.

A diagnostic capture with `SDL_TIMER_RESOLUTION=0`,
`present-stall-timer0.csv`, reproduced individual 47.23 and 47.05 ms presentation
waits (frames 891 and 890). The corresponding GPU samples took 2.39 and 2.43 ms;
resource fences took 0.005 and 0.004 ms. PresentMon's matching QPC neighborhood
contains composed-to-independent-flip transitions, display-update intervals of
50.09 and 33.37 ms and display latency reaching 83.08 ms. This locates the stall
in presentation/compositor completion rather than rendering work. PresentMon
observes the driver's DXGI path; this is temporal correlation, not a one-to-one
mapping between DXGI events and Vulkan present IDs. The diagnostic timer override
is not the engine's normal policy and does not prove the cause of the earlier
sustained slowdown.

Simply lowering the driver timeout from 50 to 16 ms was insufficient:
`present-budget-bounded.csv` still measured a 28.17 ms wait. A deadline around
nominally nonblocking completion checks still reached 27.78 ms in
`present-deadline-default.csv`. Splitting driver-check and sleep timings in the
next experiment (`present-stall-final.csv`, despite its provisional filename)
located up to 25.71 ms inside **zero-timeout driver calls**, while accumulated
sleep time peaked at 7.24 ms. Merely polling Vulkan on the main thread cannot
enforce this pacing budget. These experiments are retained rather than reported
as successful fixes.

Vulkan permits implementation-dependent timeout rounding and does not promise an
exact relationship between actual display and present-ID completion. See
[vkWaitForPresentKHR](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForPresentKHR.html).
Windows 11 may disregard a process's requested timer resolution when its window
is occluded; see [timeBeginPeriod](https://learn.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod).

## Implementation

`presentation_pacing.*` retains extension negotiation, present IDs and the normal
two-presentation pacing target. `presentation_completion.*` owns one sleeping
worker, its latest requested/completed IDs and driver error. It calls the driver
outside its state mutex, so the main thread never waits on a mutex held during
the driver call. `presentation_wait.hpp` owns the main-thread wait policy:

- Check the worker's completion state, without calling Vulkan on the main thread.
- Sleep for up to 1 ms through SDL's Windows high-resolution waitable-timer path.
- Stop at a 16 ms elapsed budget, or earlier if repeating the last observed sleep
  would exceed the remaining budget. There is no busy-spin or global timer change.
- Retry pacing next frame after timeout. A timeout never advances completion or
  permits reuse before the existing resource fence. FIFO and image acquisition
  remain responsible for presentation correctness.

The worker coalesces requests to the latest requested ID; completed higher IDs
satisfy earlier waits. Driver timeout does not mark an ID complete. Swapchain reset
stops and joins the worker before destruction and clears all IDs/results. Vulkan
explicitly permits presentation waiting concurrently with other swapchain use,
except swapchain destruction. The worker is idle on a condition variable without
pending work; an immediate driver timeout receives a sleep before retry.

The deadline is a responsiveness policy, not a hard real-time guarantee: a single
OS scheduling delay can still exceed it. Out-of-date and suboptimal results request
swapchain recreation; other failures retain normal error handling.

Frame traces now include the waited present ID, budget, start QPC time, check
count, accumulated main-thread status-check/sleep time and coarse thread CPU
accounting. The last worker driver duration is paired with its own present ID
(it may repeat across frame rows; it is not the current frame's GPU time).
`THEONE_REFERENCE_PRESENT_WAIT_BUDGET=1` restores the original single 50 ms driver
wait for same-binary comparisons. `capture.py` also records diagnostic SDL timer
overrides in metadata. Source/binary identities bind each capture to its build.

No texture residency, upload, staging, frame-slot or swapchain-image budgets changed.
There is one worker thread per active renderer, with fixed state and no per-frame
job allocations or GPU allocations. Its CPU stack is the small additional resource.

## Final validation

Both final worker captures contain 1,440 measured frames, stable source/binary
identities, current build inputs and unchanged scene/shader hashes. They use SDL's
normal timer policy. Times below are milliseconds, not FPS improvements:

| Capture | Frame mean | Frame p99 / max | Main pacing wait p99 / max | Fence max | Acquire max |
| --- | ---: | ---: | ---: | ---: | ---: |
| `present-worker-default.csv` (focused) | 16.678 | 19.213 / 23.063 | 14.957 / 15.560 | 0.030 | 0.026 |
| `present-worker-background.csv` (428 focused, 1,012 unfocused) | 16.677 | 19.817 / 20.980 | 14.933 / 15.557 | 0.177 | 0.049 |

The second run's present-call maximum was 1.192 ms, so the removed blocking wait
did not move into presentation submission. Main-thread completion-state checks
averaged 0.0061 ms per frame. The two runs had no pacing timeouts or 47 ms main-thread
presentation stalls. That does not establish a universal OS scheduling bound.

`present-worker-background-reference.csv` uses the **same executable** with the
old blocking policy. Its comparison with the worker run has nine matching
phase/focus/surface contexts and no metadata warnings
(`present-worker-background-comparison.json`). Both runs were predominantly
independent flip (1,560 of 1,563 PresentMon events), including while unfocused.
The healthy reference also ran at roughly 16.7 ms: this is stall isolation, not a
claimed mean-throughput gain. For example, matched unfocused scene reload p99
was 19.69 ms before and 19.89 ms after. The older composed-heavy captures provide
the long-stall evidence; their display conditions must not be silently pooled
with these final captures. The original sustained 46.59 ms state remains
unreproduced. The deterministic blocked-driver test covers a driver call that
stays blocked past the main-thread deadline.

Both worker runs ended at 8,347,136 GPU texture bytes, with zero pooled staging
and retired bytes. Peak staging remained below 4 MiB (4,075,520 / 3,788,800 bytes).
Scene reload reused all 50 models, replaced none and transferred zero GPU bytes;
pending draws ended at zero. No residency or loading tests were rerun.

Focused checks only:

```powershell
ctest --test-dir '../Games 2/build' -C Release -R '^engine_presentation(_pacing)?$' --output-on-failure
```

- `engine_presentation_pacing`: unsupported/uncapped/initial paths, ID reset,
  timeout/error propagation, synthetic 47 ms completion, deadline recovery,
  coarse/oversleep behavior, blocked-worker isolation, in-flight join/reset and
  worker error propagation. Passed.
- `engine_presentation`: FIFO/uncapped, one/two slots, idempotent frame boundary,
  same-size and actual resize, minimize/restore, asset reset, image parity and
  shutdown with Vulkan synchronization validation. Passed with unchanged checksum
  `13028279735593214689` and no validation errors.

These two tests took 3.28 seconds after the final implementation. The full engine
and game test suites were not run. Captures were serialized with compilation and
GPU checks; existing concurrent work in `editor_history.cpp` was preserved.
