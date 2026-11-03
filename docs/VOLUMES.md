# Reactive smoke and fuel explosions

The engine owns the gas simulation, participating-medium transport and HDR
rendering. The separate [effects lab](https://github.com/traidy2222/TheOneGames/tree/main/games/effects-lab)
owns scenarios, geometry, controls and capture tools. The feature has no dependency
on game assets, physics-library internals or a particular game executable.

## Integrate a source

Link `TheOne::Renderer` and `TheOne::Volumes`, and call
`theone_copy_shaders(your_target)`. Use the engine's `*_hdr` shader contract for
HDR; older game overrides of `scene.frag` continue to belong to the legacy path.

```cpp
#include <theone/render/renderer.hpp>
#include <theone/volumes/volume.hpp>

one::VolumeScene smoke;
smoke.domain.origin = {-6, 0, -6};
smoke.domain.extent = {12, 18, 12};
smoke.domain.resolution = {48, 72, 48};
smoke.domain.memoryBudget = 512ull * 1024 * 1024;
one::addFuelExplosion(smoke, {0, 1.5f, 0}, .18f);
renderer.setLighting(one::SceneLighting{});

// Advance a world clock, including when a source has stopped injecting.
smoke.timeSeconds = elapsedWorldSeconds;
renderer.setVolumeScene(smoke); // copies inputs and enables HDR
// beginFrame(), ImGui frame and normal renderer.render(...) calls follow.
```

All renderer calls belong to the owning render thread. Rendering again at the
same requested simulation time does not advance the fluid twice. The fixed
CFL-safe quantum is bounded by `maximumVelocity`; when `maximumSubsteps` cannot
catch up, `timeDebtSeconds` reports the remaining work. Chemistry does not take a
larger timestep to conceal debt. Pausing means keeping `timeSeconds` unchanged.

Increment `generation` to replay or change initial state, fuel, sources or solver
configuration. Bounds/resolution changes and rewinding the clock also reset the
domain. Obstacle AABBs/velocities can update without replaying. The bottom of the
domain is a solid floor; other faces are ambient open boundaries by default.
Use matching visible geometry for obstacles. Voxel coverage is conservative,
so thin obstacles can occupy a full cell. There is no automatic Jolt adapter.

`clearVolumes()` retires gas resources behind submission completion. HDR targets
remain reusable; `enableHdr(false)` returns to the legacy rendering path.
`captureVolumeState()` explicitly waits and copies the completed gas grid for
diagnostics; use `volumeStats()` for routine completed-frame telemetry.

## Model and units

| Input/field | Meaning |
| --- | --- |
| Domain/source/solid coordinates | metres; velocities m/s; time seconds |
| Fuel, oxygen, soot, products and nitrogen | cell-average kg/m³; transported conservatively |
| Enthalpy / heat capacity | J/m³ / J/(kg K); temperature derived in kelvin |
| Source rates | fuel/soot kg/s and heat W, normalized over discrete unblocked cells |
| Heat release, oxygen ratio, soot yield | J/kg fuel, kg oxygen/kg fuel, kg soot/kg fuel |
| Absorption/scattering | m²/kg soot; multiplied by soot concentration to obtain 1/m |
| `VolumeCell` snapshot | `species`: fuel/O₂/soot/products; `thermal`: nitrogen/enthalpy/divergence target/flame distance; `velocity`: positive MAC faces/previous solid flag |

The solver uses limited finite-volume scalar transport, staggered velocity,
solid-aware velocity tracing, buoyancy, variable-density pressure projection and
bounded vorticity confinement. A transported flame distance propagates through
flammable fuel. Reaction consumes available oxygen and produces products, soot
and heat; the source helper supplies fuel and a separate localized ignition
pulse. Fuel presets are illustrative gaseous-fuel parameters, not calibrated
predictions for a named liquid explosive.

`initialTurbulenceRms` (m/s) and `initialTurbulenceScale` (m) describe optional
resolved ambient airflow. The initial velocity is a deterministic divergence-free
sum of modes, limited to wavelengths of at least four cells. It is initialized
once and then advected/projected by the same solver; no time-animated noise is
added to the smoke density or its silhouette. The default is still air; the lab
uses 0.8 m/s and a 1.5 m scale. Vorticity confinement is a numerical approximation,
so a grid-size comparison remains necessary for authored effects.

Smoke concentration has no lifetime fade. It moves, dilutes, leaves open
boundaries or remains in the field. Heat losses have an explicit ledger.
Nearby sources enter the same gas grid, exchanging species, heat and momentum.
This version exposes one shared bounded dense domain per renderer.

The optical path integrates Beer–Lambert extinction and source radiance along
world-space distance, clipped against retained opaque depth. Hot soot emits
three visible Planck bands. Sun lighting includes scene shadows, medium
attenuation and a normalized Henyey–Greenstein phase function. Brick emission
integrals and energy-weighted centroids feed an occluded irradiance cache for
smoke and diffuse surface illumination. A bounded neighbor-light approximation
adds indirect scattering. A fixed HDR storage pre-exposure preserves bright
emission in RGBA16F; bloom, exposure and output encoding precede unaffected UI.
Alpha surfaces integrate the medium prefix to their own depth.

## Deliberate limits

- This is low-Mach fuel deflagration and smoke transport. It does not resolve
  compressible blast shocks, detonations, liquid droplet evaporation, debris or
  damage. Those require separate systems and validation.
- Pressure projection uses a configured Jacobi iteration budget. Initial and
  final divergence residuals and velocity clamps are reported; a finite result
  is not a convergence guarantee. Increase resolution/iterations and check these
  counters for each authored effect.
- `closedBoundary` is a fixed-volume diagnostic mode with expansion suppressed.
  It conserves reaction/transport ledgers but does not model sealed-vessel
  thermodynamic pressure. Production plume scenes use open boundaries.
- Moving voxel masks redistribute newly covered gas to adjacent free cells.
  Motion should be less than a cell per update. Teleporting solids, trapping gas
  without any free recipient, and topology-changing closed cavities require a
  more general conservative remapping/cut-cell solver.
- Gray optical coefficients, optically thin thermal cooling, three-band emission,
  twelve-sample secondary attenuation and the irradiance cache are real-time
  approximations. No spectral chemistry, full surface GI or offline transport
  equivalence is claimed. Refraction and heat distortion are unsupported.
- Simulation is dense and local. Eight-cell brick metadata skips empty *rays*,
  not simulation allocation. There is no sparse/planet-wide fluid solver, temporal
  reconstruction or cross-GPU bitwise determinism guarantee.

Resources include both field copies, pressure, reduction ledgers, lighting,
per-slot inputs/readbacks, HDR/depth images and in-flight retired grids.
Requests exceeding the budget fail explicitly. HDR requires RGBA16F render/sample
support, sampled depth, eight per-stage storage buffers and nine storage buffers
in the combined pipeline layout. These extra requirements are checked only when
HDR is requested.

## Focused checks

```powershell
cmake --build build --config Release --target theone_volume_reference theone_volume_gpu --parallel 1
ctest --test-dir build -C Release -R '^engine_volume_' --output-on-failure
```

The CPU fixture checks analytic transport/subdivision, zero extinction, phase
normalization, reaction stoichiometry/heat, source integration and memory bounds.
The GPU fixture checks analytic slab/depth/camera-inside images, transparent
prefix transport, UI preservation, directional lighting, thermal emission,
chemistry budgets, wind, cadence, blocked-wall flux, flame propagation, moving
obstacle conservation, resize, removal and shutdown with synchronization
validation. These numerical gates supplement actual effects-lab frame sequences;
they do not establish photorealism on their own.

The original [design](VOLUMETRIC_EFFECTS_DESIGN.md) records the broader research
direction; the implemented contract and limits above describe the current code.
See [validation evidence](VOLUME_VALIDATION.md) for measured numerical results and
the separate consumer's actual captures.
