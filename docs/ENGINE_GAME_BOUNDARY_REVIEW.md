# Engine and game ownership review

Reviewed 2026-11-03 against TheOne and the separate TheOneGames collection.
This is a source ownership review, not a claim of game fidelity or complete
runtime acceptance.

## Promoted into TheOne

| Previously in TheOneGames | Engine owner | Reason and verification |
| --- | --- | --- |
| `shared/import-host/editor_history.*` | `TheOne::Scene`, `theone/scene/editor_history.hpp` | Entity-delta transactions, collision classification and bounded undo/redo need no application UI or gameplay. The existing focused history regression now lives in the engine. |
| `shared/import-host/scene_asset_reuse.*` | `TheOne::Physics`, `theone/physics/scene_reuse.hpp` | Reusing unchanged immutable models and adopting collision owners is an engine resource-lifetime operation. A CPU-only engine test covers unchanged reuse, retirement ownership, collision identity, edited images and explicit no-reuse policy. |

The consumer retains its diagnostic environment flag and passes an explicit
boolean to `reuseSceneAssets`. Renderer invalidation, history retirement, game
callbacks, UI and final scene publication remain coordinated by its host.
Engine modules never include game headers or depend on game directories.

## Reviewed and retained in TheOneGames

| Area | Decision |
| --- | --- |
| Sandbox/Robots application, editor widgets, background-loading UI, handoff profiler | Application lifecycle, ImGui interaction and game callbacks stay with consumers. The reusable jobs, picking, history and lifetime operations are engine-owned. A future standalone editor library could separate more UI without moving game behavior into the runtime. |
| Robots archives, native motion sampler, mode/clip mapping and double jump | Original-format decoding and game rules stay game-owned. Generated/exported commercial assets stay ignored and local. |
| Transformers catalogs, attachments, pose tracks, interrupted blends, combat and audio events | These depend on port-specific catalogs, player state, bones and event formats. Do not move complete classes into the engine. Decoded WAV storage/voice playback and local-TRS pose blending are future extraction candidates after they have game-independent inputs and tests. |
| Planets material field, excavation inventory, saves and terrain meshing | Currently coupled to this game's geological representation and editing rules. A reusable chunk/density/meshing API needs a separate contract and bounded-world tests before promotion. |
| Tornado field, debris simulation and volumetric shaders | Showcase-specific model and rendering. Custom shader deployment belongs with that executable. |
| Unity density storage, volume meshing, lighting and planetary flight | Reusable ideas, but implemented against Unity/C# types and lifecycle. Keep separate from the native C++ engine; no untested mechanical translation. |
| Blender/Unreal/Fab bridges | Engine-neutral authoring tools, not native runtime dependencies. Retained under collection tooling; credentials and installed runtime state are outside both repositories. |

## Remaining engine specialization

TheOne still has a built-in player capsule, movement/jump/respawn behavior,
mandatory sample input actions and a `spin` scene behavior. These should become
configurable primitives or consumer policy in a separate compatibility change.
This publication does not claim that all historical game assumptions are removed.

`World::find` remains a linear entity lookup. Large scenes or many physics bodies
need representative scaling measurements before claiming large-world readiness.
Lighting and animation orchestration remain limited; no general animation graph,
script runtime, navigation or networking service is introduced by this review.

## Repository and validation rules

- TheOne builds libraries, generic asset tools and engine-only fixtures/tests.
- TheOneGames owns executables, gameplay, application UI, source assets and game checks.
- Games use public headers and link targets. They must not add engine `src` to their include paths.
- The game collection records the tested engine revision in `engine-revision.txt`;
  `THEONE_ROOT` remains configurable and defaults to a sibling TheOne checkout.
- Historical performance reports retain their original evidence and limitations.
  Build output, caches, extracted game content and credentials are not published.

Focused publication validation is recorded in the game collection's
`docs/PUBLICATION_VALIDATION.md`. Existing game sessions are not terminated to
replace locked executables; a separate verification build is used instead.
