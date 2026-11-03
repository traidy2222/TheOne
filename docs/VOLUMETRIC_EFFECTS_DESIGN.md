# Smoke, fire and fuel explosions

Status: original design and continuing research direction, 2026-11-03. The dense
local implementation now exists; [VOLUMES.md](VOLUMES.md) documents its actual API,
validation and deliberate approximations. Proposals below such as sparse fluid
allocation and residual-based solver termination are not implemented features.
First acceptance target selected by the user:
**a fuel explosion becoming a rising smoke plume**.

## What realism means for this feature

The shape must follow evolving airflow and combustion. Illumination must follow
participating-medium optics. Contact with a floor, wall, ceiling, wind and another
plume must change the result. A convincing stationary screenshot is insufficient.

This is a coupled system of combustible material, hot gases, soot, lighting and
solid interactions. Raymarching displays those fields; it does not generate the
physical motion. Artist controls select source conditions and explicitly named
approximations rather than painting a time-dependent sphere of opacity.

The first scope is visually plausible fuel deflagration and its aftermath. A
low-Mach plume solver with combustion expansion does **not** solve compressible
detonation or a supersonic shock front. Blast impulse, distortion, debris and
audio must not be presented as evidence that it does. Physical blast propagation
would require a separately validated compressible solver.

## Existing engine constraints

The inspected public baseline is `da5ce28`.

| Current owner | Constraint for the new feature |
| --- | --- |
| `shaders/scene.frag` | Surface shading applies distance fog, tone mapping and output encoding before composition. Display-referred smoke blending would give incorrect light/energy relationships. |
| `render/vulkan/render_targets.cpp` | The color target is the swapchain format; scene depth has `DONT_CARE` store behavior. |
| `render/vulkan/render_surface.cpp` | Depth images have attachment usage, without sampled-depth access. |
| `render/vulkan/passes.cpp` | Surface draws, blended draws and ImGui share a pass. Volume transport needs opaque depth and linear scene radiance before UI/output conversion. |
| `render/vulkan/shadow_resources.cpp` | The directional light is fixed in code. Volume and surface lighting require a common explicit lighting input. |
| `effects/particles.cpp` | This is an existing sample billboard emitter, not a smoke/combustion simulation. Keep it available for existing consumers. |
| TheOneGames tornado shaders | A specialized procedural participating-medium demonstration. Its field, game shader overrides and approximate lighting are useful evidence, not a reusable combustion solver. |

A production volume feature therefore begins with HDR/depth/lighting integration,
not with adding another special-case surface fragment shader.

## Ownership and data contract

Introduce `TheOne::Volumes` as a separate engine module. It depends on Core;
public volume types expose no Vulkan, Jolt, ImGui or game-specific types.
Renderer owns GPU execution and volume rendering. Physics supplies optional
collision snapshots through an adapter, not private Jolt pointers.

Proposed responsibilities, not declarations of APIs already implemented:

| Public concept | Responsibility |
| --- | --- |
| `VolumeDomain` / generational `VolumeHandle` | Bounded world-space simulation region, cell spacing, configuration, resource budget and lifecycle. Local storage coordinates remain separate from world position. |
| `FuelMaterial` | Oxidizer requirement, heat release, soot yield, ignition/reaction parameters and optical/radiation model, with units and calibration provenance. |
| `CombustionSource` | Injection of fuel mass, enthalpy and momentum over a physical source footprint/duration; optional particulate or droplet source later. |
| `SmokeSource` | Cold or hot aerosol injection for noncombustion sources. Distinct from ignition and heat release. |
| `VolumeObstacleSnapshot` | Immutable solid occupancy/signed distances and boundary velocities at a stated generation. |
| `VolumeLighting` | The same sun, sky and local-emitter inputs consumed by surface lighting. |
| `VolumeSnapshot` | A completed simulation generation, bounds and immutable render-visible field handles. Simulation ticks once per world tick, not once per camera. |
| `VolumeStats` | Memory, occupied cells, CFL/substeps, time debt, solver residual, conservation ledgers and completed GPU timings. |

Keep emitter triggers, gameplay damage, source fuel selection, camera controls and
showcase UI in TheOneGames. A new `games/effects-lab` consumer will own the public
demonstration. Engine test executables remain focused regression fixtures.

Multiple nearby explosions inject into the same physical gas field. A source is
not an independent simulated box: two plumes exchange momentum, heat and species
through a shared domain or conservatively joined active regions. Optical overlap
alone cannot provide fluid interaction. Initially use one bounded shared domain;
sparse shared regions follow the same transport contract. Do not count the same
shared field twice when collecting render-visible domains.

## Simulation

### Fields and units

Use a staggered MAC velocity grid in m/s; cell-centered total gas density,
fuel/oxidizer/product mass fractions, soot mass concentration in kg/m3, specific
enthalpy and temperature in kelvin. Pressure is a projection variable with a
documented relationship to the low-Mach equation of state. Solids and boundary
velocities are separate fields. A displayed opacity value is never a conserved
simulation quantity.

Fuel plus oxidizer becomes products, soot and heat. The reaction update is bounded
by available fuel and oxidizer; emitted heat cannot exceed consumed fuel times
the material's heat release. Soot yield changes composition, not total mass.
Combustion expansion and any injected mass contribute to the velocity-divergence
constraint. Do not inject an outward velocity and then project all expansion away.

Initial implementation uses gaseous fuel. A subsequent liquid-fuel extension
requires droplet drag, evaporation, latent heat and conservative transfer to the
gas grid. Do not label a gas source as a validated liquid-fuel spray.

### Per-tick sequence

1. Acquire source/obstacle snapshots and determine CFL-safe substeps.
2. Deposit source mass, enthalpy and momentum with resolution-independent totals.
3. Advect velocity and scalar fields, then apply explicitly bounded reactions.
4. Derive mixture temperature/density consistently from enthalpy/composition and
   the equation of state, then apply buoyancy, gravity and external wind.
5. Solve variable-density pressure projection against the combustion/mass-source
   divergence target and solid boundary conditions.
6. Correct face velocities; update temperature consistently from transported
   enthalpy, composition and the chosen equation of state.
7. Record transported outflow, source, reaction and cooling ledgers; publish a
   completed generation for optics and rendering.

Use a limited higher-order advection scheme for appearance; retain an independently
checked conservative reference for species/energy transport. Semi-Lagrangian or
MacCormack advection alone is not a conservation guarantee. No mass-conservation
claim is accepted until the ledger tests pass. Solver iterations stop on a
residual criterion with a bounded maximum; report failure to converge.

Walls impose no penetration and defined slip conditions. Moving objects supply
boundary velocity. Solid-aware tracing prevents scalar transport through walls;
setting velocity to zero inside a solid is insufficient. Thin-wall/cut-cell
coverage and trapped-cell behavior need explicit tests. Open boundaries track
outflow; they are not arbitrary density-erasing planes through the visible plume.

Entrainment and dilution should reduce visible concentration. Any physical or
art-directed decay has a named rate and ledger entry. There is no universal
"smoke fades after N seconds" rule.

### Turbulence and the explosion lifecycle

Resolved velocity produces the main rolling structures and buoyant vortex ring.
Vorticity confinement may compensate for numerical dissipation, with a measured
resolution-dependent strength. Additional subcell detail is transported with the
flow and bounded so it does not change a cell's integrated aerosol mass. Noise
does not create the main silhouette or grow curls independently of the fluid.

The source/chemistry drives ignition, expansion, fuel consumption, cooling and
plume formation. Fire goes dark as the emitting material cools or is consumed;
the soot remains and is advected. Ventilation and obstacles affect these stages.
Do not animate them using a shared fireball radius and opacity lifetime.

Sparks are colliding emissive particles; fragments are solid bodies; impact dust
is a separate material source. These supplement the volume with distinct motion
and interaction. Not every explosion requires visible sparks or a shock ring.
Any approximate blast impulse is an explicit event shared with physics and
source accounting, not a claim of resolved pressure-wave dynamics.

## Optical model and lighting

Convert material concentrations into absorption `sigma_a` and scattering
`sigma_s`, in inverse metres. Extinction is `sigma_t = sigma_a + sigma_s`.
Use calibrated coefficients or clearly labeled artistic parameters; black soot,
mineral dust and condensed water do not share one gray density-to-alpha curve.

For a constant segment of length ds:

```text
T_segment = exp(-sigma_t * ds)
L_segment = j * (1 - T_segment) / sigma_t
L += T_accumulated * L_segment
T_accumulated *= T_segment
```

`j` is emission plus in-scattered radiance per unit length. Use the stable
zero-extinction limit `L_segment = j * ds`. Accumulate in linear scene radiance;
sample count must not set opacity. Sum overlapping media coefficients and sources
before integrating a ray segment. Do not draw independent overlapping plumes as
unordered translucent boxes.

Direct illumination includes opaque-scene shadows and attenuation along the light
path through all relevant media. Use a normalized anisotropic phase function with
an explicit view/light direction convention. Sky illumination is directional,
not a constant gray addend. Sparse/low-resolution light-transmittance caches can
replace secondary marches after their error is measured.

Hot soot uses temperature-dependent graybody emission derived from a tabulated
spectral/blackbody model and absorption, rather than a hand-painted orange alpha
gradient. Gas reaction-zone emission needs its own labeled approximation; hot
transparent gas is not automatically a blackbody. Emission is extinguished by
cooler foreground smoke.

Represent multiple scattering with a bounded irradiance approximation after the
single-scattering reference passes. This approximation must be identified and
checked against offline/reference cases; avoid arbitrary brightness multipliers
inside dense smoke. A real-time implementation is not an unbiased path tracer.

The same emitted energy drives a stable lower-resolution emissive-light cache for
nearby smoke and surfaces. Independent flickering point lights unrelated to the
simulation are not the illumination model. Full surface global illumination is
a separate capability; document what the emissive-light approximation covers.

## Vulkan rendering integration

Introduce an explicit linear HDR scene path with a final output transform. The
initial target is `RGBA16F`, subject to device-format capability checks. Depth is
stored and sampleable. The opt-in path preserves the existing display-referred
consumer contract until each custom shader has been migrated and checked.

Frame order:

```text
simulation / obstacle updates
opaque shadow maps
opaque surfaces -> linear HDR + retained depth
volume coefficient / lighting preparation
depth-bounded volume integration -> radiance and transmittance
resolve: HDR = L_volume + T_volume * HDR_opaque
transparent surfaces with matching prefix volume transport
bloom/exposure/output transform
UI -> swapchain -> capture/present
```

The volume ray ends at actual opaque scene depth, including when the camera is
inside the medium. Keep radiance and transmittance separately through filtering
and composition. Depth-aware upsampling must preserve thin silhouettes and avoid
halos; half-resolution raymarching is a measured quality option, not a requirement
for the first correctness reference.

For an ordinary alpha-blended surface at distance z, shade its radiance through
the prefix transport as `L(0,z) + T(0,z) * L_surface`, then blend against the
already volume-resolved background in linear HDR. Test multiple transparent
surfaces and consistency with the same volume integration. Refraction through
glass/heat gradients is not solved by this alpha-compositing contract and must
be implemented or explicitly unsupported separately.

Custom Planets and Tornado shaders currently override engine shader outputs.
Give the HDR path an explicit shader contract/version; do not silently interpret
their already tone-mapped output as linear radiance. Remove legacy distance fog
from the HDR surface path and represent atmosphere/haze consistently, so it is
not applied again over the new medium.

Start with full-resolution deterministic integration and no temporal reuse as a
reference. Add jitter, half-resolution rendering and temporal reuse separately.
Reject history on camera cuts, disocclusion, source ignition/extinction, domain
generation changes and large optical-depth/velocity differences. Explosion
brightness must not leave a temporal trail or lag behind moving smoke.

## Execution, budgets and lifetime

Start with bounded dense local domains behind an allocator/backend abstraction;
prove the solver before introducing sparse active bricks. Sparse allocation must
retain a halo, transport into empty neighbors and release only genuinely inactive
bricks. The design supports 8-cubed bricks, but sparse execution is not a feature
until transport across brick boundaries passes the same tests.

An initial reference quality case is a 16 x 24 x 16 m domain at 96 x 144 x 96
cells (about 1.33 million). This is a proposed experiment, not a measured fit or
frame-rate promise. Account explicitly for face velocity, scalar ping-pong,
advection scratch, pressure, obstacles, optical fields, lighting/history targets
and in-flight retirement. A "density texture size" alone is not a memory budget.

Use compute on the existing graphics queue first. Track producer/consumer
barriers between compute writes, optical reads and fragment/compute raymarch
reads, including cross-frame use. Existing two-frame rendering does not permit
overwriting a field still being sampled. Add asynchronous compute only after
queue ownership and profiling show a benefit.

Fixed simulation ticks and CFL substeps have a configured maximum. If the budget
is exceeded, expose time debt/quality reduction rather than silently losing a
source impulse or changing chemistry with display frame rate. Prewarm domain
resources before an explosion; effect death retires resources behind completed
submission serials. No routine `vkDeviceWaitIdle` or per-frame full-grid GPU/CPU
readback is allowed. Diagnostics can request explicit snapshots.

Initial measurement goal: quantify a single hero effect at 1920x1080 on the local
RTX 3080, with separate simulation, pressure, lighting, march, resolve, history
and memory counters. Select an actual budget from that experiment before setting
production quality defaults. FIFO frame wall time is not a throughput result.

## Acceptance scenes and tests

| Gate | Required evidence |
| --- | --- |
| Optical reference | Vacuum leaves the image unchanged. A homogeneous slab matches Beer-Lambert transmission and analytic constant-source radiance. Changing ray step count converges without changing intended opacity. |
| Composition | Two overlapping domains match the equivalent summed medium. Camera-inside, opaque foreground/background and thin silhouettes have correct path lengths. UI is unaffected by volume lighting. |
| Light transport | Backlight/frontlight produce expected directional changes; opaque walls block light; smoke self-shadows; hot emission is occluded by cold soot. Low/high quality agree against a reference tolerance. |
| Fluid reference | Projection reduces divergence against its expansion target. Closed-domain transport has a recorded mass/energy error; obstacle flux and open-boundary outflow are measured. Grid refinement converges. |
| Reaction | No negative species, oxidizer-limited fuel consumption, bounded heat release and correct zero-fuel/zero-oxidizer behavior. Temperature/enthalpy changes agree with the stated material model. |
| Physical counterfactuals | Change gravity, wind, source mass and ventilation. The plume and reaction change accordingly without changing a silhouette preset. A ceiling causes lateral flow; a wall splits flow; moving solids displace it. |
| Lifecycle | Capture ignition, expanding flame, cooling transition and late plume from the same state history. Test near/far cameras and at least four lighting/view directions. |
| Numerical control | Compare fixed-tick results under 30/60/120 Hz rendering, verify CFL handling and report debt. Replay a source seed; no bitwise cross-GPU determinism claim. |
| Vulkan lifetime | Synchronization validation for two frame slots, domain removal/reallocation, resize, camera cut, cancellation and shutdown. No stale handles or descriptor reuse while in flight. |
| Performance | Stable source/binary identity, fixed resolution, identical source/obstacle/lighting sequences and matched focus/presentation contexts. Report warm and ignition peaks separately. |

For each numerical gate, establish a CPU/reference tolerance and an independent
GPU tolerance before implementation is called accepted. Record error norms and
resolution, not merely a `PASS` message. Visual gates need actual frame sequences
and recorded physical counterfactuals; a nonempty screenshot is insufficient.

## Implementation order and review boundaries

1. **Linear HDR/depth contract:** scene lighting input, retained depth, output/UI
   separation and legacy-path image parity. No combustion code in this change.
2. **Volume transport reference:** medium descriptors, exact segment integration,
   homogeneous slabs and depth-clipped GPU integration. No noise-shaped explosion
   presented as the final effect.
3. **Flow and obstacles:** bounded MAC solver, transport, pressure residuals,
   gravity/wind and source accounting, verified against CPU/reference fixtures.
4. **Combustion and expansion:** fuel/oxidizer/enthalpy/soot, source limits and
   divergence coupling. Validate ignition-to-plume behavior and counterfactuals.
5. **Light transport:** scene shadows, self-shadow, thermal emission and the
   explicitly bounded emissive-light/multiple-scattering approximations.
6. **Consumer effects lab:** playback, deterministic source scenarios, obstacle
   arrangements, field-debug views and repeatable captures in TheOneGames.
7. **Efficiency:** measured sparse execution, empty-space skipping, light caches,
   resolution scaling and history, each compared with the correctness reference.
8. **Complete acceptance:** all relevant optical, reaction, obstacle, lifecycle,
   multi-effect and GPU resource gates, with limitations and measured budgets.

Each stage is a separate reviewable change with focused checks. Stages 1-2 are
rendering infrastructure, not a completed realistic explosion. The feature is
not complete until the coupled effect passes the scene/physics/lighting gates.

## Technical basis

These sources inform the method, not a claim that their implementations or
performance transfer to this engine:

- [Crane, Llamas and Tariq: Real-Time Simulation and Rendering of 3D Fluids](https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids): fluid fields, obstacle interaction, advection and volume-rendering integration.
- [Nguyen, Fedkiw and Jensen: Physically Based Modeling and Animation of Fire](https://www.graphics.stanford.edu/papers/fire-sg02/): fuel/products, expansion, buoyant soot and temperature-dependent emission.
- [Feldman, O'Brien and Arikan: Animating Suspended Particle Explosions](https://jamesobrien.com/papers/Feldman-ASP-2003-08/): combustion-product expansion without pretending an incompressible model resolves the blast wave.
- [Hillaire: Towards Unified and Physically-Based Volumetric Lighting in Frostbite](https://advances.realtimerendering.com/s2015/): a shared extinction/light-transport representation for participating media.
