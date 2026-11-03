# Visible gameplay and editor hitches

Measured on 2 November 2026, Windows / RTX 3080 / Release / 1920 x 1080, with FIFO presentation. This iteration covers visible hitches, editor transactions, resident draw preparation, texture transition churn and warm prepared-cache reads.

## Visible baseline and changes

The real Robots application renders its normal editor and gameplay UI during the replay. The script moves the player/camera, opens the inspector, drags a static entity for 60 frames, undoes/redoes, reloads its asset and reloads the scene. It changes only this process's in-memory scene. It does not save a scene or gameplay state. The optional profiler lives in the consumer's `shared/import-host/visible_profile.*`; normal launches do not record traces.

The initial 1,200-frame baseline caught a 194.072 ms scene-reload frame. Frame start occupied 189.124 ms, of which only 20.455 ms was a fence wait: 168.669 ms remained in maintenance. Most other long frames were fence waits. This guided the retirement work instead of changing presentation settings.

The first matched 1,200-frame replay after the changes measured:

| Measurement | Before | After |
|---|---:|---:|
| Worst visible frame, ms | 194.072 | 48.220 |
| Maximum frame start minus fence wait, ms | 168.669 | 4.850 |
| Idle editor UI mean, ms | 0.419 | 0.202 |
| Idle geometry preparation mean, ms | 0.223 | 0.031 |
| Idle main-pass recording mean, ms | 0.496 | 0.355 |
| Collision requests during one 60-frame drag | 60 | 1 on release |
| History payload for that drag | World snapshots | 2 entity values |
| Texture bytes at replay end | 8,250,880 | 8,250,880 |

These are observations from this machine, not hard frame-time guarantees. FIFO fence waits still produce long wall frames. The extended repeat also confirmed UI timings around 0.22 ms and geometry preparation around 0.03 ms, but aggregate renderer CPU timing varies with scheduling. The CPU-0-pinned sustained run was particularly noisy (unrelated visibility stages also spiked), so do not treat its aggregate speedup as an isolated renderer measurement.

The final 1,440-frame input-isolated replay (`hitches-visible-verified.csv`) completed all four collision operations: no requests during drag frames 480-539, one at release 540, undo at 600, redo at 660, then asset replacement. Every sampled frame remained visible. Its worst frame was 50.560 ms (46.962 ms fence wait), maximum frame-start work excluding fence wait was 4.950 ms, idle UI averaged 0.189 ms and idle geometry preparation 0.026 ms. At completion there were no pending draws, texture demands, loads or retired resources; texture memory was exactly 8,250,880 bytes. Peak retired GPU storage during reload was 32,975,200 bytes and drained to zero. The retirement allowance is a latency/memory tradeoff while the previous scene is released. The two consumer source files were hash-checked against the successful build after this replay.

## Responsibilities and behavior

- `src/render/vulkan/asset_retirement.cpp` collects only fence-complete resources, stopping between callbacks after 0.75 ms or 32 callbacks in a frame. The allowance covers both frame start and asset synchronization. One driver destruction call can exceed the allowance; this is not a hard deadline. Clear/close explicitly drain after device idle. GPU destruction stays on the rendering thread; the final retired-model lease releases on a worker. Existing memory accounting exposes the temporary retired GPU backlog.
- Consumer `shared/import-host/editor_history.*` stores bounded entity changes for undo/redo. A transform gesture captures one entity, commits once, and submits collision work on release. Bulk changes temporarily compare entity arrays but retain only changed entries. Visibility/animation controls avoid unrelated rebuilds; static clip changes rebuild collision. Scene replacement retires history on the loading worker.
- `src/render/vulkan/draw_preparation.cpp` resolves resident materials once, with generation invalidation. Geometry references are refreshed when source sequences change or assets are invalidated. Sorted/copied draws retain transient reference indices. Main recording preserves opaque/blend ordering while skipping unchanged material descriptor binds and constants. `THEONE_REFERENCE_DRAW_PREPARATION=1` retains map lookups and repeated material-state emission for focused A/B checks.
- `src/render/residency_policy.hpp` keeps the existing eight-frame promotion delay and uses 32 stable frames before demotion. Memory-pressure reclamation remains immediate. `textureChangesQueued`, `textureChangesCommitted`, `texturePromotions` and `textureDemotions` count binding transitions; aliases can share a physical upload. The older `textureTransitions` counter is outstanding demand, not completed changes. `THEONE_REFERENCE_RESIDENCY=1` restores the previous symmetric eight-frame policy.
- `src/assets/cache/dependency_manifest.*` stores the dependency list so warm hits skip glTF parsing. Every dependency is still hashed in full, including source content; timestamps are not trusted as a replacement. `mapped_file.*` maps prepared archives read-only on Windows, avoiding a full temporary payload copy. The decoder owns all returned storage. Header, size and payload checksum checks remain. Invalid manifests fall back to parsing, invalid payloads fall back to source import, and manifests publish through temporary files.

## Texture churn and loading

The same 1,800-frame 1080p movement path recorded 698 queued/committed binding transitions with the reference policy and 634 with the new policy: 9.2% fewer. Promotions fell from 343 to 313; demotions from 355 to 321. Peak live texture memory was approximately 8.805 MB versus 8.820 MB (about 15 KB higher); average memory increased by about 125 KB while retaining detail through short movements. The moving-camera endpoint can differ until demotions settle. The matched visible replay ended at the same 8,250,880 bytes, and the focused residency check verifies eventual distance/pressure reclamation and correct descriptor refresh.

Three warm scene loads in each binary:

| Metric | Before | After |
|---|---|---|
| Total ms | 520.315 / 577.486 / 614.749 | 477.733 / 445.273 / 441.048 |
| Mean ms | 570.850 | 454.685 |
| Parsed glTF ms per load | 108.873 / 117.993 / 127.803 | 0 / 0 / 0 |
| Prepared hits | 50 each | 50 each |
| Content fingerprint | 109327585889636832 | 109327585889636832 |

Warm loading improved about 20%. The cache-format payload remains compatible with the previous version. Mapping reduces temporary-copy storage, but these measurements do not separately attribute a speedup to mapping versus skipping parsing.

## Focused validation

Six checks passed: `editor_history_focused`, `engine_streaming_memory`, `engine_texture_residency`, `engine_shadow_submission`, `engine_prepared_cache`, and `engine_residency_policy`. GPU checks enabled Vulkan validation and synchronization validation. A separate reference draw-preparation run matched all 40 capture checksums exactly. No whole-engine or whole-game suite was run.

The retirement fixture invalidates 80 small models and verifies the per-frame count limit, backlog and eventual drain. Cache checks cover exact payload/rig round trips, shared images, external dependencies, same-size edits with restored timestamps, corrupt manifest/payload recovery, cancellation and unwritable-directory fallback. Editor checks cover coalescing, no-ops, collision classification, unrelated entity preservation, undo/redo, creation, bulk reload and the 32-command bound.

Concurrent editor-history work was preserved; the static-clip fix was merged into the current file and its focused test rerun. An extended replay was found to have desktop-input interference and is not used for collision-comparison claims. The replay now filters incidental input while retaining window/quit events, validates the static edit target, and requires all four intended collision requests (drag, undo, redo, asset replacement).

## Reproduction and artifacts

From `Games 2`, run:

```powershell
./build/bin/robots/Release/robots.exe --scene ./games/robots/build/robots-village.one --profile-visible ./build/hitches-visible.csv --profile-frames 1440
./build/Release/runtime_profile.exe ./games/robots/build/robots-village.one --1080p --frames=1800
```

The consumer build was configured with `THEONE_BUILD_TESTS=ON` to reuse its already-built engine dependencies. Explicit nested targets use names such as `engine/tests/theone_prepared_cache`; this does not run their tests automatically.

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^(editor_history_focused|engine_residency_policy|engine_prepared_cache|engine_streaming_memory|engine_texture_residency|engine_shadow_submission)$'
```

Raw visible traces and logs are in `Games 2/build/hitches-visible-{before,after,verified}.{csv,log,err}`. Sustained A/B logs are `Games 2/build/hitches-runtime-{reference,after}.log`. Loading logs are `TheOne/build/hitches-cache-{before,after}.log`. Focused results and image-reference checks are `TheOne/build/hitches-focused-checks.log`, `hitches-focused-details.log`, `hitches-draw-reference.log` and `hitches-editor-merged.log`. `TheOne/build/hitches_summary.py` summarizes visible traces. These generated artifacts are local build outputs, not source assets.
