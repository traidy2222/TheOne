# The One — Vulkan import sandbox

A C++20 engine foundation with a playable sample game, an editor, and asset inspection/conversion tools. Version 0.3.0 targets the point where we can assess a first game port against tested capabilities.

## Run

```powershell
.\run.ps1 -Configuration Release
```

Requires Windows, a Vulkan-capable GPU/driver, CMake 3.24+, Vulkan SDK with `glslc`, and Visual Studio C++ desktop tools. The first build downloads pinned, SHA-256-verified source dependencies through CMake. Later builds use the local cache. Windows x64 is the tested platform; the SDL platform layer is portable, but other operating systems have not been built or tested.

The sample opens an imported GLB level with a skinned animated robot, physics crates, collectible gameplay, particles, lighting, shadows, and a scene editor.

| Control | Action |
| --- | --- |
| WASD | Move |
| Right mouse + motion / arrow keys | Look |
| Tab or toolbar | Switch fly camera / walking player |
| Q / E | Fly down / up |
| Shift | Move faster |
| Space / gamepad south button | Jump while walking |
| F | Interact / push nearby physics crates |
| R | Reset camera and player |
| F1 | Toggle editor panels |
| F5 | Reload the current scene |
| F6 / F9 | Save / load gameplay |
| Escape | Exit |

Keyboard gameplay bindings are in `assets/input.json`. Gamepad sticks control movement/look; hardware gamepad behavior remains unverified. Audio loads a WAV and plays interaction/jump/collection cues. Automated tests submit audio to SDL's dummy device; a real WASAPI stream was opened in the interactive Windows session, without an independent listening check.

## Editor and assets

Select an entity in the hierarchy or by clicking its geometry. Edit position, rotation, scale, parent, animation clip, visibility, and physics mode in the inspector. Transform edits pause simulation. Undo/redo and duplicate are available. Parent changes preserve local transforms; they can change the object's world position.

Use **Save scene** / **Load saved** for `saves/scene.one`. F6/F9 save gameplay entity transforms/visibility, body velocities, player position/velocity, camera, and time under `saves/`. Reloading builds a replacement world before swapping the live scene. Original source assets are preserved by these save operations.

Drop a `.glb` or `.gltf` onto the window, or enter a path in the asset panel. The editor places imported content in front of the camera. Source files must remain available: scenes store references, rather than embedding complete asset files.

Convert additional model formats through Assimp:

```powershell
.\build\Release\one_import.exe path\model.fbx build\model.glb
.\build\Release\one_inspect.exe build\model.glb build\model-report.json
```

`one_import` embeds referenced material textures, exports GLB, validates it through the runtime loader, and writes a conversion report. It refuses to overwrite an existing output. Enabled converters: FBX, OBJ, Collada, 3DS, MD2, MD3, MD5, PLY, STL, IQM, and glTF. The automated conversion fixture specifically tests OBJ + MTL + PNG; other converters need representative files before claiming fidelity.

`one_inspect` checks a GLB/glTF, samples its clips, and reports bounds, meshes, images, skins, and import diagnostics. Unsupported required extensions fail with an error. Diagnostics identify approximations such as sampler settings, UV transforms, and alpha-cutout shadows.

## Verify and package

```powershell
.\run.ps1 -Test
.\run.ps1 -Test -Configuration Release
.\package.ps1
```

The core test covers projection, scene hierarchy/validation/roundtrip, embedded textures, skin deformation, bone animation, imported mesh collision, rigid bodies, capsule grounding/jumping/wall blocking, and game collection events. A conversion test roundtrips a textured OBJ through Assimp to the runtime.

GPU tests render 120 frames, resize twice, minimize/restore, save/reload a scene, submit audio to the dummy device, and read back a frame. The coverage check detects missing scene output; it is not a pixel-reference comparison. Vulkan validation errors fail the validation run, including errors during renderer teardown.

Optional external compatibility tests:

```powershell
python tools/fetch_test_samples.py
python tools/make_compatibility_scene.py
.\run.ps1 -Test -Configuration Release
```

These download the Khronos Fox and WaterBottle samples into ignored `build/external`, preserve provenance/license metadata, and test them together in the GPU sandbox. They are not included in the normal runtime package. The optional test is registered when its scene and the SDK validation layer are available. `build/capture.ppm` contains the latest test frame.

## Code boundaries

- `one_content`: glTF import, world serialization, camera math, and Jolt physics adapter.
- `one_sample`: sample game module, depending on content/services rather than Vulkan or editor code.
- `one_ui`: Dear ImGui and SDL/Vulkan backends.
- `theone`: SDL application, editor, input, audio, and Vulkan renderer.
- `one_import` / `one_inspect`: conversion and compatibility reports.
- `tools/generate_fixtures.py`: original GLB, animation, texture, sound, and conversion fixtures; deterministic regeneration.

Read [docs/PORTING.md](docs/PORTING.md) for the port-selection checklist, tested capabilities, and remaining limits. Dependency provenance and notices are listed in [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md).
