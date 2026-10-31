# Dependency provenance

The engine uses these upstream projects rather than implementing their subsystems from scratch. CMake pins the downloaded archive contents with SHA-256 hashes. See `CMakeLists.txt` for exact commits/tags and hashes. Unmodified sources remain under ignored `build/_deps`.

| Project | Version / pin | Purpose | Main notice |
| --- | --- | --- | --- |
| SDL | 3.2.28 / 7f3ae3d | Platform, input, audio | SDL LICENSE.txt |
| Jolt Physics | 5.3.0 / 0373ec0 | Physics | Jolt LICENSE |
| Dear ImGui | 1.91.9b / f5befd2 | Editor/UI | ImGui LICENSE.txt |
| cgltf | 1.15 / bbeb5b0 | glTF parsing | cgltf LICENSE |
| stb | 003501f | Image decoding | stb LICENSE |
| nlohmann/json | 3.12.0, archive hash pinned | JSON scenes/reports | LICENSE.MIT |
| GLM | 1.0.1, archive hash pinned | Math | copying.txt |
| Assimp | 5.4.3 / c35200e | Asset converter | Assimp LICENSE, including bundled notices |

Assimp also builds bundled zlib and supporting libraries; SDL includes supporting platform code. The package script collects license/copying notices from fetched sources into its `licenses` directory while retaining relative paths. Upstream project URLs are encoded in the CMake download URLs.

The default level, robot, crate, checker textures, particle image, conversion fixture, and interaction sound are generated locally by this project. The optional Khronos compatibility files have separate upstream metadata preserved under `build/external`; they are excluded from the normal runtime package.
