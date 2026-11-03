# Reactive-volume validation, 2026-11-03

Implementation baseline: engine `cec2f70`, effects-lab consumer `6f856c9`.
Subsequent evidence/documentation commits do not change the tested engine code.
Hardware: NVIDIA GeForce RTX 3080, 10 GiB, driver 617.42, Windows x64,
Visual Studio 2022 Release and Vulkan SDK 1.4.304.0.

## Focused regression result

Seven checks passed: `game_engine_separation`, `engine_gpu_validation`,
`engine_module_boundaries`, `engine_streaming_memory`,
`engine_shadow_submission`, `engine_volume_reference`, `engine_volume_gpu`.
The build included every standalone public-header translation unit. GPU volume,
memory and shadow fixtures enabled Vulkan synchronization validation. No Vulkan
errors were reported. The existing unused-vertex-attribute performance warning
is not a synchronization error.
An additional CPU/GPU image-parity check passed for the HDR shader contract with
animated skinned meshes, nonuniform scale and procedural particles. The change
adds coverage to the existing geometry fixture without changing runtime code.

For this final combined build, tests were enabled in the independent consumer's
`build/publication` directory to reuse its compiled engine libraries:

```powershell
# From TheOneGames; default consumer builds still leave engine tests disabled.
cmake -S . -B build/publication -DTHEONE_BUILD_TESTS=ON -DTHEONE_BUILD_GPU_TESTS=ON
cmake --build build/publication --config Release --parallel 1 --target `
  effects_lab engine/tests/theone_volume_gpu engine/tests/theone_volume_reference `
  engine/tests/theone_module_smoke engine/tests/theone_gpu_tests `
  engine/tests/theone_shadow_features engine/tests/theone_streaming_memory
ctest --test-dir build/publication -C Release --output-on-failure `
  -R '^engine_volume_|^engine_module_boundaries$|^engine_gpu_validation$|^engine_shadow_submission$|^engine_streaming_memory$|^game_engine_separation$'
```

The standalone engine build also passed the preceding volume, shadow, memory and
geometry checks. The final combined build checks the additional initial-airflow
input and independent engine consumption. This is focused validation, not a claim
that every historical engine/game test was rerun.

## Numerical evidence

| Gate | Observed result |
| --- | --- |
| CPU constant-medium subdivision | radiance error < 1e-11; transmittance error < 1e-12 |
| CPU normalized HG integral | absolute error < 1e-8 |
| GPU homogeneous absorbing slab | pixel 149; analytic display value 149.456 |
| Camera inside medium | pixel 200; analytic 200.498 |
| Alpha surface inside medium | pixel 182; analytic prefix composition 182.004 |
| Directional scattering response | front/back pixels 6 / 119 |
| Temperature-driven emission | cold/hot pixels 2 / 255 |
| Chemistry fixture | 0.1 kg injected fuel; 0.0892013 kg consumed; 0.00490607 kg soot |
| Total reaction mass error | 6.10e-6 kg |
| Fuel / oxygen balance error | 7.50e-9 / 1.56e-6 kg |
| Enthalpy ledger error | 6.16 J against 197338 J net expected change |
| Wind counterfactual | soot centroid moves an additional 0.77906 m |
| 30/60 Hz render cadence | relative soot-field error 0 on this GPU |
| Blocking wall | measured soot density beyond the wall 0 |
| Initial resolved air | interior MAC divergence 0; nonzero velocity energy |
| Gravity counterfactual | hot-soot centroid rises an additional 0.324021 m |
| Open-boundary soot ledger | error 7.00e-9 kg |
| Projection fixture | initial/final divergence residual 0.00570158 / 0.000437688 s^-1 |
| Flame-front counterfactual | burned fuel 0.00998571 kg without propagation; 0.0886751 kg with propagation |
| Moving obstacle | soot before/after 0.0212625 / 0.0212625 kg; difference < 1e-7 kg |

Images additionally check ray-step changes, opaque foreground depth, unchanged
UI, resize without losing soot, one/two frame slots, removal/reallocation and
shutdown. These small controlled fixtures do not bound error for every fuel,
resolution or obstacle topology.

## Visual and performance evidence

The separate consumer's [capture report](https://github.com/traidy2222/TheOneGames/blob/main/docs/EFFECTS_VALIDATION.md)
records actual frame sequences, six physical scenarios, additional view/light
directions, source/binary identities and stage timings. No generated concept art
is used as implementation evidence.

The images demonstrate the implemented lifecycle and obstacle/light interactions.
They also expose finite-grid softness and flame structure. They are not a claim
of cinematic photorealism, grid convergence or equivalence to offline spectral
transport. In particular, the 64-wide grid costs substantially more than the
48-wide grid. Simulation is the dominant cost; dense pressure projection and
advection need further optimization for higher production resolutions.

The [implemented model and deliberate limits](VOLUMES.md) remain part of this
validation record. A smaller velocity bound was also explored for speed, but it
produced velocity clamps; the published lab keeps the conservative 12 m/s bound.
