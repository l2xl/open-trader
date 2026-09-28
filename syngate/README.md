# Synergy Context Gate tree of Open Trader

The item files under this folder are the product's architecture and contract tree. The toolkit
that reads, validates, edits and gates it — the Synergy Context Gate (`syngate` CLI and local
editor) and its AI agent — lives in the separate
[AI-Scratcher](https://github.com/l2xl/ai-scratcher) repository (Python package `ai_scratcher`);
its README is the design and operating guide for the tree model, item schema, test binding,
coverage JSONL, status rollup and process rules.

Open Trader specifics:

- The package is installed by the build system and nothing else: `cmake/AiScratcher.cmake` fetches
  `l2xl/ai-scratcher` (tip of `main`) through CPM and provisions `<build dir>/ai-scratcher-venv` as
  the `ai_scratcher_venv` target, a dependency of `trader` (see [BUILD.md](../BUILD.md)).
  `bash ci/venv.sh` prints that venv's interpreter; `ci/gate.sh` runs the gate through it.
- `scripts/syngate.py` and `scripts/check_self_approval.py` are shims binding the CLI to this
  repository as the managed project: `"$(bash ci/venv.sh)" scripts/syngate.py ui`. The commands are
  listed in [CONTRIBUTING.md](../CONTRIBUTING.md).
- Bindings: `@pytest.mark.syngate("UID"[, "name"])` in `scripts/tests/` (emitter:
  `scripts/tests/conftest.py`), `TEST_CASE("…", "[UID][name]")` under `test/` (emitter:
  `test/syngate_coverage_listener.cpp`); `syngate test/review` find Catch2 binaries in
  `--build-dir` (default `cmake-build-debug-clang`).
