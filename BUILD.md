# Build Configuration
- Use cmake with ninja generator
- The only CMakeLists.txt manages build at the project root
- Use clang-23 for build
- Look for an existing build folder and use it to speedup build (currently it is cmake-build-debug-clang)

## Skia
Skia is downloaded via CPM at configure time (shallow clone, ~several hundred MB) and built in-place with its native GN+Ninja toolchain. It is **always built in release mode** regardless of the project build type.

The build sequence triggered on first `cmake --build`:
1. `python3 tools/git-sync-deps` — fetches Skia's third-party dependencies
2. `bin/gn gen out/release --args=...` — configures the GN build
3. `ninja -C out/release skia` — compiles `libskia.a`
4. `bin/gn gen out/cmake-ide --ide=json ...` — runs GN→CMake translator for IDE source navigation

To speed up repeated configures, set `CPM_SOURCE_CACHE` to a shared directory (e.g. `~/.cache/CPM`) so CPM reuses the Skia clone across projects:
```
export CPM_SOURCE_CACHE=~/.cache/CPM
```
