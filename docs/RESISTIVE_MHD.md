# Resistive magnetic plasma

`TheOne::Plasma` is a reusable CPU finite-volume MHD solver. The stellar game
supplies its own solar units, initial conditions, magnetic forcing, controls and
captures. The engine does not contain a star experiment or an eruption timeline.

The resolved domain is **2.5D**: derivatives in Cartesian x/y, with all three
velocity and magnetic components. This is real magnetic-fluid evolution in a
local region, not a whole-star or fully three-dimensional simulation.

## Equations and discretisation

In normalised units with magnetic permeability one, the conserved quantities are
density, three momenta and total energy. Magnetic stresses in the momentum flux
include both pressure and tension. The energy is
`E=p/(gamma-1)+rho*v²/2+B²/2`. Fluxes include the magnetic Poynting contribution.
The induction equation evolves the magnetic field using
`E_electric=-v cross B+eta curl B`. Resistive energy transport uses
`eta (curl B) cross B`; magnetic dissipation heats the gas instead of being removed
from total energy. Gravity contributes momentum and mechanical work.

The implementation uses monotonised-centred linear reconstruction, local
Lax–Friedrichs face fluxes and two-stage SSP Runge–Kutta time integration. Bx/By
are staggered on faces, Bz is volume-averaged. Four reconstructed corner states
and transverse upwind magnetic jumps provide edge EMFs. Updating faces by their
discrete curl preserves the face-flux divergence to roundoff. Initial Bx/By are
also a discrete curl of a corner-sampled vector potential.

The timestep bounds the sum of directional fast-magnetosonic rates and explicit
resistive diffusion. Nonpositive candidate density/pressure rejects and halves
the step; the solver does not silently add pressure or remove momentum. A finite
retry limit raises an error. Diagnostics report rejected steps and the actual
simulation clock. No constant-step animation clock drives the simulated motion.

Periodic boundaries support verification. The coronal option is periodic in x,
line-tied at the lower y boundary and zero-gradient/open at the top. The bottom
normal field is held by zero tangential edge EMF; optional out-of-plane footpoint
shear supplies boundary work. An open top may admit inflow as well as outflow.
Whole-domain mass/energy conservation claims apply to periodic, unforced boxes,
not to the driven/open solar experiment.

`injectGuideField` is an explicit external magnetic-work operation. It changes Bz
and adds exactly the corresponding magnetic energy. With no z derivatives it
introduces no discrete divergence. Subsequent acceleration and deformation are
computed by MHD; the operation does not specify a particle path or plume shape.

## Radiation bridge

`StellarScene::plasma` accepts an immutable density/temperature snapshot with a
local tangent chart and finite viewing depth. Each renderer frame slot owns its
host-coherent storage buffer, updated only after its fence. Grid size is bounded
at 128x128, and the scene validates density, temperature, basis and dimensions.

The sky raymarch bilinearly samples that field, clips against the opaque star and
registered spheres, and assigns most local samples to the patch. When a snapshot
is present, the old prescribed plasma arches are disabled for that emitter.
Photospheric convection remains procedural. The quiet corona uses the supplied
ambient density/temperature profile when available. The patch contributes its
departures from that same ambient state, avoiding a luminous artificial boundary.
Procedural spots, spicules and EUV surface emission are suppressed for this
emitter; its EUV signal comes from coronal volume emission.

The diagnostic source follows electron-density squared times an illustrative
temperature response around 1.5 MK. The monochrome composite uses a broad
log-temperature response (width 1.8); the colour diagnostic uses width 0.48.
These are inspection responses, not named observed passbands. Cool material
absorbs in the diagnostic. Monochrome EUV is available alongside the colour diagnostic, visible continuum
and coronagraph. These displays never alter light falling on other bodies.
The response and opacity are not calibrated atomic/instrument calculations.
Visible scattering retains the existing Thomson model.

## Physical scope and limits

This is compressible, single-fluid, resistive MHD with an ideal-gas closure. It
does not solve ionisation, radiative energy losses, field-aligned thermal
conduction, photospheric radiation/convection, a stellar dynamo, or kinetic
particle acceleration. In particular, it cannot reproduce a three-dimensional
kink instability or claim predictive solar flare/CME timing. The finite viewing
depth and the mapping from a Cartesian patch to a curved stellar limb are display
approximations, not a third dynamically evolved dimension.

The game uses an effective resistivity resolvable on its small grid, far larger
than microscopic coronal resistivity. Numerical diffusion is also present.
Spatial convergence of a smooth wave does not establish converged reconnection
rates, eruption morphology, radiation or observational agreement.

## References and checks

- [Athena equations](https://princetonuniversity.github.io/Athena-Cversion/AthenaEqns.html)
  provide the conservative MHD system and permeability-one convention.
- [Londrillo and Del Zanna, 2004](https://arxiv.org/abs/astro-ph/0310183)
  describes staggered upwind constrained transport and divergence preservation.
- [Mignone and Del Zanna, 2020](https://arxiv.org/abs/2004.10542)
  discusses consistency between the fluid Riemann solver and edge EMFs.
- [Fan, 2018, NCAR](https://impacts.ucar.edu/en/publications/mhd-simulation-of-prominence-eruption/)
  demonstrates the substantially richer thermal and three-dimensional physics
  involved in prominence formation and eruption; this implementation is not a
  reproduction of that research model.

`engine_mhd_reference` checks a static uniform state, circular Alfvén propagation
and spatial convergence, nonlinear Orszag–Tang conservation/positivity, Ohmic
heating with conserved periodic total energy, divergence and injected work.
The game separately compares its stressed coronal region with an unforced
control. The GPU stellar fixture verifies the snapshot temperature changes the
observed coronal emission while retaining the prior lighting regressions.

When OpenMP is available, the larger-grid face-flux and edge-EMF loops use at most
four workers. The serial fallback solves the same equations. The game caps
physics substeps per displayed frame and exposes the actual simulation time;
requested time acceleration is not a guaranteed rate on every machine.
