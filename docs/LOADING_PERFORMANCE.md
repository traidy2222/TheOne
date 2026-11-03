# Loading and reload performance

Measured on 2026-11-01 with Windows x64 Release and an NVIDIA GeForce RTX 3080. The isolated loader uses the actual local Robots village scene: 6,186 mesh primitives, 1,240 uploaded textures and 20,821,428 bytes of indexed GPU geometry. It loads the scene, builds collision, clears/replaces the previous world, prepares the render scene, renders once and checks one warm frame. It runs no gameplay, resize sequence or engine/game suite.

## Profile and result

Times below exclude renderer startup. Reload rows average the two reloads within each run. Validation was disabled for before/after timing and enabled separately for correctness. The OS file cache was not flushed, so initial load means first load in that process, not cold-disk performance. These are wall-clock observations, not fixed performance assertions.

| Measured stage | Before | After |
| --- | ---: | ---: |
| Initial load path (summed stages) | 3.89 s | 1.98 s |
| Reload path (summed stages) | 5.65 s | 2.39 s |
| Clear old GPU assets | 1.42 s | 0.30 s |
| First render after reload | 2.43 s | 0.40 s |
| Mesh allocation/staging | 555.8 ms | 8.8 ms |
| Synchronous upload submissions per load | 1,241 | 3 |

The measured reload path is **2.36x faster**. Its total includes scene loading, collision rebuild, cache clear, world replacement, render preparation and the first render; it is not a measurement of the whole game process. After this GPU-upload change, scene parsing/image decoding dominated at about 1.5–1.6 seconds and were left unchanged in that pass. The scene-import follow-up below addresses this cost. Mesh indexing was only about 0.11 seconds, so its algorithm was also left unchanged.

## Changes by responsibility

- `src/render/vulkan/geometry_cache.cpp`: newly encountered primitives are packed into a shared vertex/index allocation. Each mesh retains its own offsets and model ownership. Reload releases one allocation per upload batch instead of thousands of tiny allocations. Later asset additions append another allocation without discarding existing meshes.
- `src/render/vulkan/draw.cpp`: buffer bindings track vertex offsets as well as buffer handles, so distinct meshes sharing a buffer render correctly.
- `src/render/vulkan/textures.cpp` and `texture_upload.cpp`: image data is collected into staging batches, with a 32 MiB target (a single oversized texture may exceed it). Each batch makes one synchronous submission and grouped image transitions, replacing one submission/wait per texture. The final pending batch completes before scene recording. Staging CPU capacity is reused; images/materials still use individual GPU allocations.
- `src/render/vulkan/materials.cpp`: asset reset discards pending texture batches along with cached GPU resources.
- `Renderer::assetUploadStats()`: per-render CPU wall times and counts expose cache-miss indexing, allocation, transfer and material costs. Material time includes texture time; texture time includes texture submission/wait time. These are not GPU timestamp-query measurements.

The code still invalidates assets explicitly with `clearAssets()`. It does not reuse stale file contents or introduce an unbounded cross-scene asset cache. Imported geometry byte counts are unchanged.

## Focused verification

- Actual Robots scene: one initial load, two reloads and warm-frame residency checks, repeated with Vulkan validation; zero validation errors.
- Small existing fixture: pre-change and optimized renders have identical SHA-256 `544c37c113e45b62b408a6d4fe5d61231022e781992edeb8c33838cfec09baaa`.
- Incremental mesh/texture additions keep existing GPU allocations resident; the resulting image matches the CPU geometry reference. Clearing/rebuilding the cache produces the identical GPU image.
- No full engine/game suite or gameplay benchmark was run for this change. Robots and Sandbox were rebuilt to link the updated engine.

```powershell
# Build only the focused loader and its dependencies.
cmake --build build --config Release --target theone_load_profile --parallel 8

# Built-in small fixture reload check.
ctest --test-dir build -C Release -R '^engine_asset_reload$' --output-on-failure

# Profile an actual scene (optional --validate, with the validation layer available).
.\build\Release\theone_load_profile.exe 'path/to/scene.one'
```

Local evidence: `build/load-before.log`, `build/load-pooled.log`, `build/load-after.log`, `build/load-validation.log`, `build/load-comparison.json`, and `build/profiling/load-fixture-after/focused-check.log`.


## Scene-import follow-up

A separate CPU-only profile of `World::load` on the same Robots scene (50 unique models, 6,186 primitives, 917,157 expanded vertices and 1,238 source images) measured the remaining import cost. The machine uses an Intel Core i7-12700F. This profile excludes collision, renderer initialization/uploads and world destruction; output fingerprinting happens after the timer stops. Three fresh imports per process are measured, with no cross-load model cache and no OS-cache flush. This is not cold-disk startup or a whole-game benchmark.

| Import stage (three-load mean) | Before | After |
| --- | ---: | ---: |
| Complete scene load | 1561.6 ms | 507.6 ms |
| Mesh decoding | 754.0 ms | 103.8 ms |
| Image decoding | 448.0 ms | 170.7 ms |
| Animation import | 141.2 ms | 20.4 ms |
| glTF file read + parse | 117.4 ms | 117.2 ms |
| glTF buffer loading / base64 decoding | 88.3 ms | 82.9 ms |

Scene loading dropped from about **1.56 s to 0.51 s (3.1x faster)**. Two measured costs were addressed:

- `src/core/validation.hpp` accepts error messages as `std::string_view`, constructing an owning exception message only on failure. Previously the successful per-component accessor checks repeatedly allocated temporary strings. Bounds, finite-value checks, exception type and diagnostic text are preserved.
- `src/assets/gltf/image_decode.cpp` owns the single-image decoding and validation for embedded buffers, data URIs and external files. `images.cpp` owns bounded batch scheduling: at most four workers including the caller, sequential decoding for fewer than eight images or one available hardware thread. Workers write fixed image slots, preserve glTF indices, join before returning and propagate failures. There is no persistent cache, background work after load, or extra model-level parallelism.

`Model::load(path, ModelLoadStats*)` and `World::load(path, SceneLoadStats*)` optionally accumulate CPU stage times; existing calls omit the stats pointer. Uninstrumented calls do not read the profiling clock. Model stage times are nested inside scene `assetsMs`, not additive to it. Parsing includes file I/O; image time includes source decoding and RGBA copying.

Focused verification only:

- Real-scene initial import and two reloads retained the baseline decoded-image/vertex fingerprint `109327585889636832`, with matching counts; the small fixture retained `212049730218371606`.
- `engine_image_loading` checks embedded PNG, base64 and escaped external paths, empty/small/batched image lists, output ordering and exact pixel equality against serial decoding. It injects a corrupt image, verifies the worker diagnostic and verifies a subsequent successful batch. It also checks validation diagnostics from a non-null-terminated string view.
- `engine_scene_loading` checks three imports of the small fixture. Both focused CTest checks passed. No renderer, physics or gameplay suite was run for this follow-up.

```powershell
cmake --build build --config Release --target theone_scene_load_profile theone_image_loading_tests --parallel 8
ctest --test-dir build -C Release -R '^engine_(scene|image)_loading$' --output-on-failure
.\build\Release\theone_scene_load_profile.exe 'path/to/scene.one'
```

Evidence: `build/scene-before.log`, `build/scene-validation-optimized.log`, `build/scene-after.log`, `build/scene-comparison.json`, `build/scene-fixture-before.log`, `build/scene-fixture-after.log` and `build/Testing/Temporary/LastTest.log`.
