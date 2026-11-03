# Finite stellar emitters

`TheOne::Lighting` supplies CPU radiometry and the Vulkan HDR renderer consumes
`StellarScene`. The separate stellar sandbox owns its observer controls, bodies,
geometry and UI. This is a steady-state radiative model for a first star sandbox.

```cpp
one::StellarScene scene;
scene.metresPerUnit = 1e6; // one local render unit = 1000 km
scene.originMetres = observerRegionMetres;
scene.stars.push_back(one::StellarEmitter{}); // Sun-like blackbody at world origin
scene.occluders.push_back({bodyCentreMetres, bodyRadiusMetres, bodyEntityId});
renderer.setStellarScene(scene);
```

Coordinates and radii in the public scene are doubles in metres. The CPU subtracts
the local origin before converting to renderer floats. The caller's visible
geometry and camera use the same local origin/unit. Changing origin does not
change radiated power, physical radius or the distance law. Validation rejects
nonfinite inputs and coordinates outside the supported local float range; choose
an appropriate origin and unit for each observer region.

For a blackbody sphere of radius R and temperature T:

```text
Luminosity = 4 pi R² sigma T⁴                    [watts]
Normal irradiance at distance d = sigma T⁴ R²/d² [watts/m²]
Angular radius = asin(R/d)
```

`powerScale` multiplies emission and defaults to one. There is no arbitrary
lighting radius or distance-independent illumination. The default uses 695700000 m
and 5772 K, following [IAU nominal solar quantities](https://www.iau.org/common/Uploaded%20files/IAUGA2015-Resolution-B3-recommended-nominal-conversion.pdf).
Its derived irradiance at one astronomical unit is approximately 1361 W/m²,
consistent with the [NASA solar irradiance reference](https://earth.gsfc.nasa.gov/climate/projects/solar-irradiance/science).

The renderer integrates sixteen directions over the finite stellar disk. Solid
angle is evaluated stably as `2 pi q² / (1 + sqrt(1-q²))`, where q = R/d. Surface
BRDFs receive radiance from these directions; opaque spheres can occult individual
samples, producing umbra and sampled penumbra. Set each occluder's `entity` to its
matching visible sphere entity to exclude self-intersection against its own
inscribed triangle mesh. This is analytic sphere visibility, not triangle-based
shadows for arbitrary local meshes.

The sky disk is rendered at background depth with its physical angular size.
Small unresolved emitters use a normalized pixel-footprint filter; camera bloom
and exposure happen afterward. In stellar mode the legacy directional sun and
ambient sky terms are zeroed in the HDR inputs. Unlit space stays black. Call
`clearStellarScene()` to return to ordinary scene lighting.

The visible spectrum uses three 100 nm Planck bands centered at 650, 550 and
450 nm, matching the existing thermal volume convention. Bolometric CPU flux and
luminosity include all wavelengths. RGB display is an approximation, not spectral
colorimetry or calibrated photometry. Inputs currently support 1000-10000 K and
four emitters/sixteen spherical occluders. Very bright HDR values are bounded
before FP16 storage to avoid nonfinite output.

This feature does not simulate stellar interiors/fusion, limb darkening, a corona,
atmospheres, interstellar dust, indirect surface illumination, gravity/orbits or
time-retarded light propagation. Existing volumetric smoke lighting retains its
separate sun/emissive-cache model; stellar volume scattering is not yet connected.
The sandbox contains one star and authored test bodies, not a generated galaxy.

Focused checks are `engine_stellar_reference` and `engine_stellar_gpu`. They cover
nominal solar flux/luminosity, inverse-square behavior, large origins, eclipses,
finite penumbra, absence of phantom ambient light, apparent size and GPU lifetime.
