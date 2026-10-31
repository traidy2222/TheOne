# Port readiness — v0.3.0

We can now choose a first, bounded game port based on a working import/gameplay sandbox. A source-available, small single-player 3D game is the most practical first target. A complete game has not yet been ported; asset compatibility and a sample game establish the foundation, not arbitrary engine compatibility.

## Evidence and capability

| Area | Working capability | Evidence / boundary |
| --- | --- | --- |
| Platform | SDL3 window/events, mouse, keyboard, controller polling, audio streams | Windows x64 built and run; mouse/keyboard editor checks performed; no hardware gamepad or other-OS validation |
| Assets | glTF/GLB triangles, external/embedded buffers and images, material factors and texture slots | Own fixtures plus Khronos Fox and WaterBottle parsed and rendered |
| Conversion | Assimp to self-contained GLB with texture embedding and a JSON report | Textured OBJ conversion tested; enabled FBX/Collada/older model importers require target-specific validation |
| Materials | Base color, metallic/roughness, normal, occlusion, emissive, unlit, alpha blend/mask | WaterBottle exercises several maps; approximate direct-light PBR, without reference-renderer comparisons |
| Rendering | Vulkan depth-tested scene, directional light, shadow map, fog, selection tint, transparent draw ordering | GPU frame readback and Khronos validation on RTX 3080 |
| Animation | Imported rig/bones, inverse bind matrices, CPU skinning, LINEAR/STEP clips, quaternion interpolation, clip selection | Two-bone fixture deformation checked; Fox's three clips sampled by inspector; Walk clip rendered |
| World | Stable entity IDs, local transforms, parent hierarchy, model instance sharing, versioned scene files | Cycle/missing-parent/duplicate-ID/invalid-scale rejection and scene roundtrip tests |
| Editor | Hierarchy, viewport ray selection, transform inspector, reparent, visibility, clip choice, physics mode, duplicate, undo/redo, import and save/reload | Position edit, undo, play mode, F6/F9 verified through native UI |
| Physics | Jolt static imported triangle colliders, dynamic boxes, rotation/velocity sync, capsule player, gravity, jump, impulses | Core tests check settling, grounding, jumping, wall blocking, and dynamic crate survival |
| Gameplay | Separate C++ game module, fixed simulation updates, collection events/HUD, interaction and sound | Sample collection game runs; tests verify collection state and no duplicate collection |
| Effects/UI | Billboard particles, alpha blending, immediate-mode UI/HUD | Sample emitter rendered through shared material pipeline |
| Saves | Scene transforms/components and gameplay player/camera/velocities/visibility/time | Scene roundtrip tested; F6/F9 manually exercised; not a stable cross-version save-game contract |
| Delivery | Release executable/assets/tools, package script and dependency notices | Local package smoke test must pass before delivery |

Engine coordinates are right-handed, Y-up; gameplay uses metres/seconds. glTF values are retained. Assets authored in different units need an explicit conversion or scene scale; the compatibility scene scales Fox by 0.04. JSON scene schema is currently version 2. Physics bodies must be root entities; hierarchical visual content can have parents.

## Material and animation limits to check before choosing a game

- Meshes are expanded into non-indexed vertices. World transforms and skinning run on the CPU, then upload once per frame. GPU instancing/skinning, mesh streaming, LODs, occlusion culling, and large-world performance work remain.
- Lighting uses one fixed directional shadow volume with PCF. There is no environment/IBL pipeline, point/spot shadow system, baked-lightmap support, or HDR exposure workflow.
- Textures use linear repeat sampling without mipmaps. glTF sampler overrides, secondary UV sets, and texture transforms are reported as approximations. Normal mapping derives its basis from position/UV derivatives; authored tangents are not imported.
- Alpha blending sorts draw groups, not intersecting triangles. Alpha-masked geometry casts solid shadows. Advanced glTF material extensions need adapters; unsupported required extensions are rejected.
- Only triangle primitives are accepted. Draco/meshopt, compressed texture formats, sparse vertex accessors, morph targets, CUBICSPLINE animation, root motion, blend trees, IK, and animation retargeting need conversion or implementation.
- Animation supports clip selection, not crossfades/state machines yet. CPU skinning supports four influences per vertex; converter limits bone weights to four.
- The capsule player is a simple dynamic body. It blocks walls and jumps but has no dedicated stair stepping, climbing, moving-platform attachment, or tuned slope/ground snapping.
- Physics currently supports imported static triangle geometry and dynamic boxes. Vehicles, ragdolls, joints, triggers as physics sensors, and game-specific physics matching need more work.
- The editor duplicates an entity; it does not provide a nested prefab/override system, visual scripting, a terrain editor, or a 3D drag gizmo.
- The C++ game-module interface is compiled into the application. There is no Unity C#/Unreal Blueprint runtime, Lua runtime, DLL hot reload, automatic script translator, or binary executable compatibility layer.
- There is no networking/replication, navigation/pathfinding, production AI framework, cinematic timeline, streaming audio mixer, or console/mobile build pipeline.

## Selection procedure

For each candidate, record:

1. **What we have:** source tree/build instructions, asset formats, scene data, scripts, original engine and third-party dependencies.
2. **Smallest playable slice:** one level, one controllable character, one interaction/enemy, HUD/audio, and a win/lose condition.
3. **Import trial:** convert a representative level and character; inspect materials, axes, units, rig, clips, collisions, and import diagnostics.
4. **Code adaptation:** list the original engine calls and map them to The One's input, content, physics, audio, world, and game-module services. Identify missing behavior separately.
5. **Behavior comparison:** compare movement, collision, timing, animation, interaction, and scene appearance against the source game. A visually loaded level alone does not establish a port.
6. **Performance trial:** measure the target slice in Release on the actual GPU, including CPU animation time, frame time, draw/triangle count, memory, and loading time. Current small-scene tests are not large-game performance evidence.
7. **Decision:** choose the candidate with the smallest critical gaps and an achievable playable slice. Add source-format adapters driven by that candidate.

For an initial shortlist, prefer a small first-person puzzle/exploration game, simple arena game, or compact platformer with available source and conventional meshes. A networking-heavy game, large streaming world, engine-specific visual scripting project, or proprietary-binary-only target will need substantially more infrastructure.

## Compatibility samples

Downloaded test files and their exact SHA-256 hashes are recorded in `build/external/provenance.json`. They stay outside normal shipped assets.

- [Khronos Fox](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/8f492390ddc7a2ae221a1369ef560589cf8c70fb/Models/Fox): PixelMannen model; tomkranis rigging/animation; AsoboStudio and scurest glTF conversion. Preserve the upstream README attribution and CC0/CC BY 4.0 terms when sharing this fixture.
- [Khronos WaterBottle](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/8f492390ddc7a2ae221a1369ef560589cf8c70fb/Models/WaterBottle): Microsoft, upstream CC0 metadata; textured PBR compatibility fixture.
