# Game integration boundary

TheOne supplies reusable rendering, glTF content, scene/animation and physics services. A consuming game links `TheOne::Content` and `TheOne::Renderer` from its own build, calls `theone_copy_shaders(target)`, and owns its executable and runtime directories. Engine targets must never refer back to game source directories.

Keep gameplay rules, controllers, missions, game HUD, editor application behavior, saves and asset extraction/conversion specific to an original game in that game's repository. Generic import tools stay in the engine. A new game must not add switches to an engine executable: there is no engine-owned game executable.

Model/scene formats are not complete game-engine converters. The importer cannot reconstruct original scripts, missions, AI, networking or gameplay. Validate actual representative assets and behavior before claiming a port. The current engine tests exercise content loading, animation, scene validation, Jolt collision, OBJ conversion and CPU/GPU rendering parity. Consuming applications must test Vulkan startup, rendering, resize, teardown and gameplay independently.

The sample and Robots applications were moved into a separate games workspace on 2026-11-01, preserving the existing source changes and local data. Their research and fidelity evidence live with those applications. Engine-only fixtures under tests/fixtures/assets intentionally remain as small content/physics test inputs.

For imported content, replace per-frame `World::bake` with `World::prepare` and the `RenderScene` renderer overload. Keep CPU baking only where actual world-space triangles are required, such as collision construction or a picking click. Dynamic particles can be submitted alongside the GPU scene. Keep asset geometry immutable until `Renderer::clearAssets()`; model ownership remains in the GPU cache until that reset.

For already-generated procedural vertices, use `renderCached` with a monotonically changing mesh revision. Advance it on every accepted worker result, mesh clear, replacement and coordinate rebase. Camera movement alone does not require a vertex upload. Preserve the normal renderer overload for geometry that changes every frame. Use `theone_copy_shaders(target)` to deploy all shaders on incremental builds. Applications overriding engine shaders must run their override target after `${target}_shaders`.

## Module boundary

New applications link the specific `TheOne::Assets`, `TheOne::Scene`, `TheOne::Physics`, `TheOne::Input`, `TheOne::Effects` and `TheOne::Renderer` targets they use. `TheOne::Content` aggregates the original content-facing modules for compatibility. Public headers are rooted at `include/theone`; `include/compat` forwards the old flat header names. Do not add the engine's private `src` directory to a game's include paths. Header-only geometry/math consumers can link `TheOne::Core`.

`one::Renderer` owns an opaque Vulkan implementation. Rebuild consuming binaries after this refactor; source compatibility does not imply ABI compatibility. Module implementation edits now rebuild their own translation units instead of being compiled into every game that includes the renderer header.
