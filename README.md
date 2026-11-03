# TheOne

A Windows C++20/Vulkan engine for imported and procedural 3D scenes. The engine
provides rendering, glTF assets, animation sampling, scene management, Jolt
physics, input bindings and particle primitives.

Games are maintained separately in [TheOneGames](https://github.com/traidy2222/TheOneGames).
This repository contains no game executable or extracted commercial game data.

## Build and check

Install Visual Studio 2022 with C++ tools, CMake 3.24+ and the Vulkan SDK.
Dependencies are downloaded from archives pinned by SHA-256.

```powershell
git clone https://github.com/traidy2222/TheOne.git
cd TheOne
.\run.ps1 -Configuration Release -Target theone_module_smoke -TestRegex '^engine_module_boundaries$'
```

Select the targets and checks relevant to your change. `-AllTests` explicitly
builds and runs the complete engine suite; it is not required for routine edits.
`package.ps1` creates a source SDK under ignored `build/packages`.

## Consume from another project

```cmake
set(THEONE_BUILD_TESTS OFF CACHE BOOL "")
set(THEONE_BUILD_TOOLS OFF CACHE BOOL "")
add_subdirectory("../TheOne" "engine" EXCLUDE_FROM_ALL)
target_link_libraries(my_game PRIVATE TheOne::Scene TheOne::Physics TheOne::Renderer)
theone_copy_shaders(my_game)
```

Public headers live under `include/theone`. Link only the modules you need;
`TheOne::Content` remains a compatibility aggregate. Consumers own their entry
point, editor UI, gameplay, saves, assets and runtime directory.

## Current scope

The renderer includes GPU skinning, material textures, cascaded shadows,
mipmaps, geometric LOD, culling, instancing, budgeted uploads and texture mip
residency. Background asset/collision jobs and selective reload support iteration.
These features do not establish a finished game, a general scripting framework,
planet-scale streaming, or a universal performance guarantee.

- [Development and module ownership](docs/DEVELOPMENT.md)
- [Game integration boundary](docs/PORTING.md)
- [Dependencies and provenance](docs/DEPENDENCIES.md)
- [Review of engine/game ownership](docs/ENGINE_GAME_BOUNDARY_REVIEW.md)
- [Latest cache performance evidence and limitations](docs/CACHE_CORRECTION_PERFORMANCE.md)

Historical benchmark documents describe their own source versions, fixtures and
hardware. Their results are not automatically measurements of the current build.
