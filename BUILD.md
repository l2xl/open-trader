# Build Configuration
- The only CMakeLists.txt manages build at the project root
- Use cmake with ninja generator
- The only C++ compiler supported: clang-23 for build
- Use the existing build folder to speedup build (currently it is cmake-build-debug-clang)
- Building `trader` also provisions the AI-Scratcher toolkit (syngate) into `<build dir>/ai-scratcher-venv` via `cmake/AiScratcher.cmake` (CPM fetch of `l2xl/ai-scratcher` `main` + pip into a venv; target `ai_scratcher_venv`). This is the only install path. Every configure re-fetches the tip of `main`; a configure that lands a new revision deletes the venv, and the next build re-provisions it from scratch. `-DOPENTRADER_AI_SCRATCHER=OFF` skips it; `-DCPM_ai_scratcher_SOURCE=<path>` uses a local checkout
- AI assistants build through `python3 scripts/build_agent.py auto [--target <target>]` (configures if needed, then builds, log in `<build dir>/build.log`), never through raw cmake

## Fast start

From the project root, on a fresh clone:

```
cmake -S . -B cmake-build-debug-clang -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23
cmake --build cmake-build-debug-clang --target trader
cmake --build cmake-build-debug-clang --target unit_tests
ctest --test-dir cmake-build-debug-clang -LE live --output-on-failure
cmake-build-debug-clang/trader --data-dir=./db
```

The first configure fetches all dependencies through CPM into `cmake-build-debug-clang/_deps`; the first `trader` build also builds ThorVG and provisions the AI-Scratcher venv.

## Targets

| Target | Produces |
|---|---|
| `trader` | the application executable, with `resources/` (fonts, panel SVG templates) copied next to it; depends on `core`, `thorvg_build`, `ai_scratcher_venv` |
| `core` | static library shared by `trader` and every test executable |
| `thorvg_build` | ThorVG static library, built by meson from a build-local `thorvg-venv` (`cmake/ThorvgBuild.cmake`) |
| `ai_scratcher_venv` | `<build dir>/ai-scratcher-venv` with the `ai_scratcher` package, `syngate` CLI and pytest (`cmake/AiScratcher.cmake`) |
| `unit_tests` | umbrella target: every test executable below |
| `test_<name>` | one Catch2 executable per file under `test/` (`test_currency`, `test_data_feed`, `test_quote_scratcher`, …), registered with CTest under the file path; `test_data_manager` carries the `live` label (hits ByBit) |
| `test_time_ruler` | image-series renderer, not registered with CTest; writes PNGs to `<build dir>/test_output/time_ruler/` |

## Tasks

Configure (fresh clone, or when the cache was lost):
```
cmake -S . -B cmake-build-debug-clang -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23
```

Build and run the application (config and sqlite live in `db/`, so run from the project root):
```
cmake --build cmake-build-debug-clang --target trader
cmake-build-debug-clang/trader --data-dir=./db
```

Provision AI-Scratcher only and run syngate on the tree (command list in [CONTRIBUTING.md](CONTRIBUTING.md)):
```
cmake --build cmake-build-debug-clang --target ai_scratcher_venv
"$(bash ci/venv.sh)" scripts/syngate.py validate
"$(bash ci/venv.sh)" scripts/syngate.py ui
```

Build the tests — all of them, or a single executable:
```
cmake --build cmake-build-debug-clang --target unit_tests
cmake --build cmake-build-debug-clang --target test_currency
```

Run the tests — offline suite, live suite, one executable, one Catch2 case by item tag, with syngate coverage records:
```
ctest --test-dir cmake-build-debug-clang -LE live --output-on-failure
ctest --test-dir cmake-build-debug-clang -L live --output-on-failure
ctest --test-dir cmake-build-debug-clang -R test_currency --output-on-failure
cmake-build-debug-clang/test_currency "[CURRENCY-001]"
SYNGATE_COVERAGE_FILE=cmake-build-debug-clang/syngate_coverage.jsonl ctest --test-dir cmake-build-debug-clang -LE live
```

Run the Python tooling tests (`scripts/tests`, pytest lives in the AI-Scratcher venv):
```
"$(bash ci/venv.sh)" -m pytest scripts/tests
```

Test placement, naming and tagging rules are in [test/README.md](test/README.md).
