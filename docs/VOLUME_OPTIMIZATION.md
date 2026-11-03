# High-detail volume optimization, 2026-11-03

The 64 x 96 x 64 case was compared at 1280 x 720 on the RTX 3080, using
identical source inputs, 48 Jacobi iterations, CFL limit, simulation clock and
optical settings. No grid reduction or lower velocity bound was used.

| Path | Mean GPU simulation, frames 30-119 |
| --- | ---: |
| Reference solver | 80.82 ms |
| Cached coefficients/faces, ordinary Jacobi dispatches | 32.78 ms |
| Cached coefficients/faces, paired Jacobi dispatches | 30.03 ms |

This is a 2.69x simulation-stage improvement in that comparison, not an FPS
claim. An earlier reference run measured 65.84 ms; run-to-run clocks and load
matter. Real-time catch-up can still issue more substeps than fixed-clock
captures. High-detail real-time performance remains unfinished work.

The engine caches pressure coefficients once per substep using the illumination
buffer as temporary scratch, reuses already constrained MAC faces, and evaluates
face-aligned velocity interpolation directly. A pressure kernel performs two
ordinary Jacobi iterations with a complete two-cell halo, preserving the
iteration budget. It is selected only on devices supporting its 46912-byte
shared-memory footprint and 256-thread workgroup; other devices use the cached
single-iteration kernel. Moving-mask remapping enforces the stored-face invariant.

Diagnostics reduce maximum values within workgroups. Per-group mass/energy
reductions still contend on global accumulation, which remains an approximately
9-10 ms bottleneck in this high-detail measurement. Phase timestamps expose
velocity, transport, forces, RHS, pressure, projection, diagnostics and occupancy.

The final two-second field comparison against the reference measured relative
L1 differences of 6.65e-8 for species, 6.27e-8 for enthalpy and 1.08e-6 for velocity.
The flame-distance field was identical. Four captured images differed by at most
one 8-bit channel value. The focused GPU volume test passed with synchronization
validation, including conservation, optics, obstacles, cadence and lifetime.

`THEONE_VOLUME_REFERENCE_SOLVER=1` selects the reference path for comparison;
`THEONE_VOLUME_DISABLE_TILED_PRESSURE=1` selects cached single iterations;
`THEONE_VOLUME_PROFILE_PHASES=1` logs completed phase timestamps. These are
diagnostic process settings, not game quality presets. The separate effects-lab
capture helper records those choices and supports fixed-clock/realtime runs,
frame wall times and explicit final-grid dumps.
