# FIFO presentation pacing

Measured on 2026-11-02, NVIDIA GeForce RTX 3080, Windows, Robots at 1920x1080.
FIFO, three swapchain images, two resource frame slots, texture residency budgets
and upload/retirement limits remain unchanged.

## Diagnosis

The previous frame-wait measurements could identify a long fence wait but could
not locate the submission on a CPU/GPU timeline. The new optional `frame_trace.*`
module uses `VK_EXT_calibrated_timestamps` with device and Windows performance
counter domains. It records submission-to-GPU-start delay, GPU completion time,
the time the CPU observes fence completion, and calibration uncertainty. These
samples retain the existing waited/GPU frame IDs.

In the initial calibrated baseline, the worst 45.09 ms fence wait belonged to a
submission that reached the GPU 47.83 ms after submission, executed for 6.48 ms,
and was observed complete 0.10 ms after GPU completion. Calibration uncertainty
for that sample was 0.0025 ms. A later reference run reproduced the pattern:
43.80 ms fence wait, 43.83 ms submission-to-GPU delay, 4.88 ms GPU execution,
0.15 ms completion-to-observation, and 0.0027 ms calibration uncertainty.

The engine was allowing presentation backpressure to accumulate before GPU
execution and then stall resource-slot reuse. These measurements do not identify
an exact internal driver/compositor mechanism. The Vulkan presentation call itself
is not specified to block subsequent queue processing; the diagnosis concerns
the measured WSI/driver backpressure, not an assumed Vulkan queue-ordering rule.
See [Vulkan presentation semantics](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html).

## Change

`presentation_pacing.*` owns optional present-ID/wait feature negotiation,
presentation IDs and pacing. On supported devices, each successful FIFO present
gets a monotonically increasing ID. Before reusing a frame slot, the engine waits
for the older of the last two queued presentations. This permits CPU/GPU overlap
while keeping the presentation queue short. A one-presentation experiment removed
the fence stalls but slightly reduced throughput, so the final policy permits two.
Presentation-completion waiting is the intended pacing use of
[VK_KHR_present_wait](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_present_wait.html).

The pacing wait sleeps in the driver; there is no polling loop, spin wait, fixed
refresh-rate assumption, extra frame buffer or additional GPU allocation. It has
a 50 ms timeout so a presentation that stops progressing does not indefinitely
hold the optional pacing step. On timeout the normal resource fence still protects
reuse. Unsupported devices retain the existing fence path. Uncapped presentation
skips pacing. Swapchain recreation resets the present-ID history; out-of-date
results request recreation.

The profiler reports the intentional presentation wait separately and includes
it in CPU stage history. `THEONE_PROFILE_WAITS=1` enables calibrated tracing where
supported; calibration is inactive otherwise. `THEONE_REFERENCE_FIFO_PACING=1`
selects the previous presentation path for comparisons.

## Results and comparison limits

Every replay below contains 1,440 measured frames, including gameplay, editor
operations, asset reload and scene reload. Windows focus was recorded because
the desktop changed presentation paths between runs. The background comparison
uses only unfocused frames from the paced replay, so it has fewer samples and a
different phase mix; it is not a perfectly controlled whole-frame benchmark.

| Metric (ms) | Reference, unfocused (1,440 frames) | Paced, unfocused subset (835 frames) | Final paced, focused (1,440 frames) |
| --- | ---: | ---: | ---: |
| Mean frame wall time | 16.691 | 16.716 | 16.677 |
| p99 frame wall time | 44.737 | 31.332 | 19.827 |
| Maximum frame wall time | 50.721 | 38.606 | 31.328 |
| Maximum resource-fence wait | 43.796 | 0.207 | 0.329 |
| Mean submission-to-GPU delay | 24.151 | 0.459 | 0.628 |
| Maximum submission-to-GPU delay | 44.273 | 4.260 | 3.164 |

The final focused run's intentional presentation wait averaged 12.30 ms, with
a 14.68 ms p99 and 15.44 ms maximum. There were no pacing timeouts. Its worst
31.33 ms frame was the first measured frame, with 20.09 ms renderer CPU work and
a 10.31 ms presentation wait; its resource-fence wait was only 0.005 ms.
The mixed-focus paced run still had four frames above 33.3 ms. The original
40–50 ms fence backlog is removed in these captures; this is not a promise that
all CPU, desktop or display outliers disappear, nor an average-FPS increase.

PresentMon 2.6.0 captured Windows presentation events alongside the engine traces.
The reference used Composed Flip and reported display completion for 941/1,528
events. The final paced run used Hardware Composed Independent Flip for 1,554 of
1,558 events and reported all 1,558 displayed. Its display-change interval had
a 16.77 ms p99 and 33.37 ms maximum. These counts include startup. Because focus,
composition path and completion coverage differ, they are corroborating context,
not an isolated claim that pacing alone halved physical display latency.
Metric definitions are in the [PresentMon documentation](https://github.com/GameTechDev/PresentMon/blob/main/README-ConsoleApplication.md).

`gpu_done_to_fence` is the age of the GPU result when the CPU observes its fence;
after pacing it includes intentional waits and frame-slot rotation. A large value
with an already-signaled fence is not a CPU wake-up stall.

## Memory and focused checks

Reference and final paced runs both ended at **8,347,136 texture bytes**. The
available staging pool stayed under 4 MiB and ended at zero; retired GPU bytes
also ended at zero. Both reused all 50 models on scene reload, performed zero
GPU transfers after the reload trigger, and finished with no pending draws.
No asset-residency, staging, geometry or reload policy was changed in this work.

Only the affected renderer/consumer targets and the new `engine_presentation`
check were built/run. The check covers FIFO/uncapped recreation, one/two frame
slots, resizing away and back, asset reset, image parity and shutdown, with Vulkan
and synchronization validation. It passed with pacing enabled (2.68 s) and with
the reference fallback (2.32 s). Both produced checksum `13028279735593214689`.
The only validation message was the existing unused vertex-input performance
warning. An initial fixture used too small a colored rectangle for the existing
capture-coverage guard; enlarging that fixture resolved the check without a
production rendering change. No complete engine/game suite was run.

## Reproduction and ownership

From the engine workspace, using the existing consumer build:

```powershell
cmake --build '../Games 2/build' --config Release --target robots engine/tests/theone_presentation --parallel 8
ctest --test-dir '../Games 2/build' -C Release --output-on-failure -R '^engine_presentation$'
```

From `Games 2`:

```powershell
$env:THEONE_PROFILE_WAITS = '1'
./build/bin/robots/Release/robots.exe --scene ./games/robots/build/robots-village.one --profile-visible ./build/fifo-capture.csv --profile-frames 1440
Remove-Item Env:THEONE_PROFILE_WAITS
```

Set `THEONE_REFERENCE_FIFO_PACING=1` for a reference run and remove it afterward.
For concurrent PresentMon capture, target the game process ID, request
`--qpc_time_ms`, and use a unique `--session_name`. The profile CSV preserves
enough numeric precision to correlate host timestamps. The present-ID feature
and calibrated timestamp feature are optional and independently negotiated.

| Responsibility | Files |
| --- | --- |
| Presentation capability negotiation, IDs, wait policy | `src/render/vulkan/presentation_pacing.*` |
| Optional calibrated clock conversion | `src/render/vulkan/frame_trace.*` |
| Submission and resource-slot integration | `device.cpp`, `frame.cpp`, `frame_resources.cpp`, `frame_profile.cpp`, `swapchain.cpp` |
| Public timing fields | `include/theone/render/frame_stats.hpp` |
| Focus and CSV samples | Consumer `shared/import-host/application.cpp`, `visible_profile.cpp` |
| Focused lifetime/image check | `tests/render/presentation.cpp` |

Engine build artifacts: `fifo-final-summary.txt`, `fifo-memory-check.json`,
`fifo-presentation-check.log`, `fifo-presentation-fallback.log`,
`pm-{calibrated-before,paced-first,paced-two,reference-final,paced-final}.csv`.
Corresponding engine CSVs are in the consumer build directory as `fifo-*.csv`.
The two final captures use the same production executable; the earlier two-limit
experiment differs only by a subsequent diagnostic-state initialization safeguard.
