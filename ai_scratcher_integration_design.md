# AI-Scratcher: separate repository and Open Trader integration (design, 2026-09-23)

Decision: **AI-Scratcher** — the Synergy Context Gate (Syngate) tree toolkit together with its AI
agent — lives in its own repository (`~/dev/ai-scratcher`, package `ai_scratcher`, CLI `syngate`),
moved mostly as it was, and stays a local tool anyone can download and run. Open Trader consumes that repository as a
build dependency, provisions a Python environment for it, starts the Syngate server as a managed
child process, and shows the Syngate UI inside a docked panel rendered by the Chromium Embedded
Framework (CEF) in off-screen mode. Python is not pinned: the toolkit targets any modern Python
(3.10+). CEF tracks the latest stable Chromium line.

# 1. The AI-Scratcher repository (done 2026-09-23)

```
ai-scratcher/
  pyproject.toml            name = "ai-scratcher"; [project.scripts] syngate = "ai_scratcher.syngate:main"
  src/ai_scratcher/         syngatelib.py  syngate.py  syngate_ui.py  syngate_ui.html  synthetic.py
                            check_self_approval.py  render_syngate_report.py
  tests/                    toolkit tests + conftest (coverage emitter; aliases the bare module names
                            the frozen routines import, so their spans stayed byte-identical)
  syngate/                  the toolkit's own tree: AI-SCRATCHER ← {SYNGATE, SYNGATE_UI, SYNTHETIC}
  README.md                 design and operating guide (the former syngate/README.md, rebranded)
```

Moved from Open Trader with their git content: the six modules, ten toolkit test files and the
`SYNGATE`, `SYNGATE_UI` (with `AI_CHAT`) and `SYNTHETIC` item subtrees (48 items; only the three
branch roots were reparented to the new `AI-SCRATCHER` root, so every leaf stamp is intact:
`syngate validate` → OK, 49 items; 90 toolkit tests pass). Kept in Open Trader: the `syngate/` tree
(149 items after the move), `INFRA` → `INFRA-065` CI items and their tests, the coverage emitters,
`publish_check_run.py`, `build_agent.py`, `check_license.py`, `import_requirements.py`,
`migrate_doorstop.py`.

Root resolution replaced the `__file__`-derived constant: `--root PATH`, else `SYNGATE_ROOT`,
else upward search from the working directory for `syngate/`; `syngatelib.set_root()` binds the
module once per CLI run (the frozen CLI tests require the loaders to be called without
arguments, so root is module state, not a parameter). Test discovery scans `scripts/tests` and
`tests`. The UI's run menu shells out to `python -m ai_scratcher.syngate --root <root>`, and the
URL line is flushed so a host can read it from a pipe.

Open Trader side: `ci/venv.sh` installs the package into `.venv-syngate` (editable from a sibling
`../ai-scratcher` checkout when present, else `git+https://github.com/l2xl/ai-scratcher.git`,
override `AI_SCRATCHER=<spec>`); `ci/gate.sh` and the CI `approval` job use it;
`scripts/syngate.py` and `scripts/check_self_approval.py` are shims binding the CLI to the
repository root (the frozen CI test still matches `syngate.py report` in the workflow).
`syngate/README.md` is a stub pointing at AI-Scratcher. Not done: pushing the new repository to
GitHub (no remote yet; CI outside a sibling checkout depends on it) and the first commit, which
the maintainer signs.

# 2. Open Trader: Python integration

## 2.1 Decision: managed child process, not an embedded interpreter

The Syngate server runs as `python -m ai_scratcher.syngate ui …` supervised by Open Trader. Reasons:

- It already spawns subprocesses itself (`pytest`, Catch2 binaries, the `claude` CLI) and holds a
  blocking HTTP server with worker threads; in-process it would need GIL discipline on the GTK/CEF
  main thread for every call.
- CEF's own multi-process model and process-singleton rules are simpler without a Python runtime in
  the browser process.
- The tool's Python version and packages stay independent of the C++ build.
- The trading-bot API is already decided as Python, out-of-process, via MCP (2026-09-02). The same
  supervisor and lifecycle serve both; nothing is designed twice.

`pybind11` embedding stays an additive option for future in-process hot paths; it is not part of
this design.

## 2.2 CMake: `cmake/AiScratcher.cmake` — done 2026-09-23, updated 2026-09-28

Mirrors the existing `ThorvgBuild.cmake` venv-and-stamp pattern, verified via the mandatory
`build-error-locator` procedure:

1. `CPMAddPackage(NAME ai_scratcher GITHUB_REPOSITORY l2xl/ai-scratcher GIT_TAG main DOWNLOAD_ONLY YES)`
   — the short `GITHUB_REPOSITORY` form (matching every other dependency in `CMakeLists.txt`: TBB,
   SQLiteCpp, glaze, Elements) rather than a full `GIT_REPOSITORY` URL, and **tracking `main`**
   (ai-scratcher's default branch — the first `GIT_TAG master` attempt failed configure, caught by
   the mandatory `build-error-locator` build) rather than a pinned commit, exactly as the Elements
   dependency tracks its own `master`. CPM's own README flags this as "not recommended… such
   packages will only be updated when the cache is cleared" — FetchContent fetches the branch tip
   once per fresh build tree and then keeps that checkout, so newer commits on `main` need
   `cmake-build-*/_deps/ai_scratcher-src` (or the whole build tree) wiped. A local checkout can
   still be substituted with CPM's own `-DCPM_ai_scratcher_SOURCE=<path>`.
2. `${Python3_EXECUTABLE} -m venv ${CMAKE_BINARY_DIR}/ai-scratcher-venv` + `pip install <source dir>`
   (no pinned Python; any 3.10+ works). The stamp depends on the fetched `pyproject.toml`; since the
   source directory's path no longer changes per commit (a branch, not a tag, is tracked), the
   reinstall trigger is now purely the file's mtime after a fetch, not a new path.
3. Verified: `cmake --build <dir> --target ai_scratcher_venv` creates the venv and
   `<venv>/bin/python -c "import ai_scratcher"` succeeds. The target is `ALL`, so a plain
   `cmake --build <dir>` (no target) provisions it too; **`--target trader` alone does not**, since
   nothing in `trader` depends on it yet — correct for now, as no C++ code consumes the venv until
   `python_host` (§2.3) exists. That dependency edge is this section's own follow-up, not a defect.
4. `configure_file` → `ai_scratcher_config.hpp` and the `--ai-scratcher-python` override are
   deferred to §2.3, since nothing consumes them yet; adding them now would be speculative.
5. Option `OPENTRADER_AI_SCRATCHER` (default ON) gates the whole file; the CEF dependency (§3) is
   not gated by it and stays a separate option.

`ci/venv.sh` installs the same package into `.venv-syngate` (done, §1) — a second, independent
venv for the CI/dev tooling context, which must exist even without a C++ build.

## 2.3 Runtime: `python_host` (library-layer, `src/common/`)

A small process supervisor, one per application:

- `start(argv, cwd)` spawns with stdout piped, in its own process group (POSIX `setpgid`; Windows
  Job Object with kill-on-close), so a crash or exit of Open Trader takes the server and its
  children (`claude`, test runs) down with it.
- A reader thread forwards stdout lines to a callback; the first line matching the URL pattern
  resolves a `std::future<std::string>` for the page URL.
- `stop()` sends SIGTERM / CTRL-BREAK, waits briefly, then kills the group.
- Implementation: a ~150-line POSIX + Win32 wrapper, or `reproc` via CPM (MIT, C++ API, terminate /
  kill actions). Boost.Process v2 is rejected: since Boost 1.86 it is a compiled library requiring
  Boost.Asio, next to the project's standalone asio.

Command line the supervisor runs:

```
<venv>/bin/python -m ai_scratcher.syngate ui --root <project root> --port 0 --no-browser --build-dir <build dir>
```

Project root defaults to the compile-time source directory (this is a developer-facing panel over
the repository) and is overridable with `--project-root`. `--build-dir` is what `syngate test`
uses to find Catch2 binaries.

# 3. Open Trader: Chromium view

## 3.1 Dependency: `cmake/CefBuild.cmake`

- `CPMAddPackage(NAME cef URL https://cef-builds.spotifycdn.com/cef_binary_<ver>_<platform>_minimal.tar.bz2 URL_HASH SHA1=… DOWNLOAD_ONLY YES)`.
  Platform suffix from `CMAKE_SYSTEM_NAME` / `CMAKE_SYSTEM_PROCESSOR` (`linux64`, `linuxarm64`,
  `windows64`, `windowsarm64`, `macosx64`, `macosarm64`). The Minimal distribution (headers, wrapper
  sources, Release binaries, resources; linux64 about 327 MB compressed) is sufficient.
- Version: **the latest stable line** from `https://cef-builds.spotifycdn.com/index.json`
  (`channel: stable`; 154.0.23 / Chromium 154.0.8037 on 2026-09-22). The pinned version and hash
  live in one CMake variable pair and are bumped as new stables appear; Chromium now releases every
  two weeks and CEF proposes building even milestones only, so expect a bump about monthly.
- `set(CEF_ROOT ${cef_SOURCE_DIR})`, `list(APPEND CMAKE_MODULE_PATH ${CEF_ROOT}/cmake)`,
  `find_package(CEF REQUIRED)`, `add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)`,
  `ADD_LOGICAL_TARGET("libcef_lib" ${CEF_LIB_DEBUG} ${CEF_LIB_RELEASE})`.
- The wrapper builds as C++20 with `-fno-exceptions -fno-rtti -Werror`. **Do not apply CEF's
  `SET_EXECUTABLE_TARGET_PROPERTIES` to project targets**; add `${CEF_INCLUDE_PATH}` and
  `${CEF_COMPILER_DEFINES}` to `trader` explicitly and link `libcef_lib libcef_dll_wrapper
  ${CEF_STANDARD_LIBS}` (`X11` on Linux). Mixing the C++20 wrapper with C++23 code is standard use.
- `COPY_FILES` for `${CEF_BINARY_FILES}` and `${CEF_RESOURCE_FILES}` into the `trader` output
  directory; ship only the `locales/` needed.
- A second executable `trader_helper` (`CefMainArgs` + `CefExecuteProcess`, no app-specific code) is
  the browser subprocess, so `trader` is never re-executed for renderer / GPU processes.
- `USE_SANDBOX` OFF for developer builds on Linux (`settings.no_sandbox = true`); packaged builds
  ship the SUID `chrome-sandbox` helper next to the executable. Windows and macOS sandbox models
  (bootstrap executable; five helper app bundles + `libcef_sandbox.dylib`) are packaging work for
  those platforms' iterations, not part of this one.

Runtime files, linux64: `libcef.so` (~110–130 MB stripped), `icudtl.dat`, `v8_context_snapshot.bin`,
`resources.pak`, `chrome_100_percent.pak`, `chrome_200_percent.pak`, `locales/en-US.pak`; GPU
libraries (`libEGL.so`, `libGLESv2.so`, `libvk_swiftshader.so`, `libvulkan.so.1`) are not needed with
the GPU switches below.

## 3.2 Configuration

`CefSettings` (in `cef_runtime`):

| Field | Value | Why |
|---|---|---|
| `windowless_rendering_enabled` | 1 | off-screen mode |
| `external_message_pump` | 1 | CEF work is driven from the GTK loop (§3.3); `multi_threaded_message_loop` = 0 (unavailable on macOS and would put render callbacks off the UI thread) |
| `browser_subprocess_path` | absolute path of `trader_helper` | main executable never re-executed |
| `no_sandbox` | `!CEF_USE_SANDBOX` | developer builds |
| `root_cache_path` | `<data-dir>/cef` (absolute) | required since CEF 120; process-singleton lock lives here, so implement `CefBrowserProcessHandler::OnAlreadyRunningAppRelaunch` and treat a false `CefInitialize` as "panel unavailable", not a crash |
| `cache_path` | empty | in-memory profile: no cookies or storage persisted; `persist_session_cookies` = 0 |
| `resources_dir_path`, `locales_dir_path` | explicit, next to the executable | deterministic |
| `log_file`, `log_severity` | `<data-dir>/cef.log`, `LOGSEVERITY_WARNING` | |
| `command_line_args_disabled` | 1 | the app's own argv is not Chromium's |
| `background_color` | opaque | windowless defaults to transparent |

`CefBrowserSettings`: `windowless_frame_rate = 60`, `background_color` opaque.
`CefWindowInfo`: `SetAsWindowless(x11 parent window)` with `shared_texture_enabled = 0` and
`external_begin_frame_enabled = 0` (CPU `OnPaint` path, the only one that is stable on Linux across
NVIDIA / Wayland).

Switches appended in `CefApp::OnBeforeCommandLineProcessing` (browser process only):
`--disable-gpu --disable-gpu-compositing` (cefclient's own choice for CPU off-screen mode: higher
frame rate, lower CPU; WebGL is not needed), `--ozone-platform=x11` on Linux (Wayland GDK objects
assert under off-screen mode; the app already pins `GDK_BACKEND=x11` for headless snapshots),
`--disable-extensions --disable-component-update --disable-background-networking --disable-sync
--no-first-run`; `--remote-debugging-port=<n>` behind a developer flag for DevTools.

No JS↔C++ bridge is needed: the page talks HTTP and Server-Sent Events to the local server, exactly
as in a browser. Should one ever be wanted, `CefMessageRouter` installs a native V8 binding, not a
script, so the page's nonce-only Content-Security-Policy is unaffected; it would require the
helper to carry the app's render-process handler.

## 3.3 Message loop: `cef_runtime` (`src/app/`)

- `main()`: `CefMainArgs`, `CefExecuteProcess` (returns −1 in the main process), `CefInitialize`
  before the Elements `app` is constructed, `CefShutdown` after the last browser reported
  `OnBeforeClose` and after the Elements window is gone. `CefInitialize` false ⇒ run without the
  panel type.
- Pump: `OnScheduleMessagePumpWork(delay_ms)` is called from any CEF thread; it arms a GLib timeout
  (or immediate idle for ≤ 0) on `g_main_context_default()`, whose callback runs
  `CefDoMessageLoopWork()` on the GTK thread. Reference implementation:
  `tests/shared/browser/main_message_loop_external_pump_linux.cc` (custom `GSource` + wake-up pipe,
  reentrancy guard, timer capped at 1000/30 ms). Never call `CefDoMessageLoopWork` from inside a CEF
  callback or a nested loop. On Windows / macOS the same class arms a native timer; the Elements
  `view::post` mechanism is not used because it needs a live view.

## 3.4 Element: `WebViewElement` (`src/app/`), panel: `WebPanel` (`src/cockpit/`)

`WebPanel : ContentPanel` adds `PanelType::AiScratcher` and holds the URL supplier; it knows no UI
library. `WebViewElement : cycfi::elements::element` is the twin of `VectorSceneElement` and owns a
`CefRefPtr<CefBrowser>` through a `CefClient` implementing:

- `CefRenderHandler`: `GetViewRect` (element bounds in DIP), `GetScreenInfo` (host scale factor →
  `device_scale_factor`), `GetScreenPoint`, `OnPaint(PET_VIEW)` copies the BGRA, upper-left-origin,
  `width*4`-stride buffer into a cairo `ARGB32` image surface (byte-compatible, premultiplied) and
  invalidates the dirty rectangles via `view::refresh(rect)`; `OnPaint(PET_POPUP)` keeps a second
  surface composited at the rectangle from `OnPopupShow` / `OnPopupSize`; `OnCursorChange` maps to
  `set_cursor`; `StartDragging` returns true and enters the in-page drag loop.
- Element events → host: `click` → `SendMouseClickEvent` (left / middle / right, mouse-up on
  release, click count), `drag` / `cursor` → `SendMouseMoveEvent` (leave with `mouseLeave`), `scroll`
  → `SendMouseWheelEvent`, `key` → `SendKeyEvent(RAWKEYDOWN)` then `SendKeyEvent(CHAR)` on press,
  `KEYUP` on release (`windows_key_code` from keysym, `native_key_code` = hardware keycode,
  `character` = unicode of the keyval, as cefclient's GTK mapping does), `text` → `CHAR`, `focus` →
  `SetFocus(bool)`; `layout()` size change → `WasResized()`; deck deselection / tab switch →
  `WasHidden(true)` (paint callbacks stop) and `WasHidden(false)` + `Invalidate` on reselection.
- Drag and drop: the Syngate page reorders tree rows with HTML5 drag events. After `StartDragging`
  the element feeds `DragTargetDragEnter` once, `DragTargetDragOver` on each move,
  `DragTargetDrop` on release, then `DragSourceEndedAt` + `DragSourceSystemDragEnded`. About 50
  lines, and mandatory: without it in-page drag and drop silently does nothing.
- Lifetime: `CloseBrowser(true)` in the destructor; the `CefClient` outlives the element until
  `OnBeforeClose`, matching the `PanelNode` RAII cascade. `WebPanel`'s `Refresh` posts a
  `view::refresh` like `PostRefresh` does today.
- Known gaps, accepted: no input-method composition (Elements has no pre-edit API; Latin and
  Cyrillic typing work, CJK needs `ImeSetComposition` wiring later); no accessibility tree for web
  content.

`UiBuilder::MakePanel` gains the `AiScratcher` case, `MainWindow` a menu entry; `MakeLeaf` is
unchanged. The first AI-Scratcher panel asks `python_host` to start the server (lazy, once per
application) and navigates to the future's URL when it resolves, showing "starting Syngate…" until
then; further panels reuse the running server.

# 4. Sequence

1. `trader` starts: CEF initialised (helper path, settings above), Elements window opens.
2. User adds an AI-Scratcher panel to a tab or split. `MakeLeaf` builds `WebPanel` + `WebViewElement`.
3. `python_host` spawns `<venv>/bin/python -m ai_scratcher.syngate ui --root … --port 0 --no-browser
   --build-dir …`, reads the URL line.
4. `CefBrowserHost::CreateBrowser` with that URL; `OnPaint` frames land in the docked rectangle
   next to instrument panels; input flows back.
5. Inside the page everything is as in a browser: tree editing with CAS autosave, test / review runs
   streamed over Server-Sent Events, the AI chat calling the local `claude` CLI with the project root
   as working directory, file panels.
6. On exit: browsers closed, `CefShutdown`, `python_host.stop()` terminates the server's process
   group.

# 5. Iterations

Each is a shippable step with its own syngate leaves, placed per §6's tree (the top-level
`AI-SCRATCHER` branch and its `AI-SCRATCHER-GATE` / `AI-SCRATCHER-SERVER` / panel children, not a
separate `INFRA` branch):

1. **Repository split** — done (§1); pushed to `github.com/l2xl/ai-scratcher` (public, https).
   Reconnected to the tree, and its two roles split into distinct branches (§6, below), since the
   first cut had conflated them.
2. **Python host**: `cmake/AiScratcher.cmake` venv provisioning done above; `python_host` supervisor with a
   Catch2 test that spawns a script, reads a line, terminates the group.
3. **CEF runtime**: `cmake/CefBuild.cmake`, `trader_helper`, `cef_runtime` pump inside the GTK loop,
   headless smoke test under the `snap-app` Xvfb flow (page loads `about:blank`, one `OnPaint`).
4. **WebViewElement + WebPanel**: paint, input, drag loop, popup, cursor; manual checklist on the
   Syngate page: textarea typing, clipboard shortcuts, tree drag and drop, both panes scrolling,
   keyboard navigation, run menu streaming, chat exchange.
5. **Platform packaging**: Windows (bootstrap sandbox model or `no_sandbox`, Job Object) and macOS
   (framework + five helper bundles) when Elements hosts for those platforms are exercised.

# 6. Requirements tree structure — updated 2026-09-28

AI-Scratcher has two roles in Open Trader — a development tool tracking Open Trader's own
architecture (as it always has), and, once §2–4 land, an AI-agent panel embedded in the running
application. The first reconnection pass put both under one `INFRA` branch; that conflated a
CI/dev-tool concern with an application-runtime one, so it is split:

```
OPEN-TRADER
└── AI-SCRATCHER                              top-level product capability (new)
    ├── (INFRA/)AI-SCRATCHER-GATE              dev-tool usage; parents: [INFRA, AI-SCRATCHER]
    │   └── AI-SCRATCHER-GATE-010              gate resolves the CMake installation (§6.1)
    └── AI-SCRATCHER-SERVER                    embedding-side backend; parent: AI-SCRATCHER
        ├── AI-SCRATCHER-SERVER-010            build-time CMake dependency, the sole install (§2.2, §6.1)
        ├── AI-SCRATCHER-SERVER-020            app spawns this install's server (§2.3); not yet implemented
        └── AI-SCRATCHER-SERVER-030            standalone-CPython packaging (§6.2); not yet implemented

HUD
└── CONTENT_PANEL
    └── CHROME_PANEL                           new specialization: content painted by an
        └── AI-SCRATCHER-PANEL                 embedded off-screen Chromium view (§3–4);
                                                parents: [CHROME_PANEL, AI-SCRATCHER];
                                                not yet implemented (childless, no tests)
```

- **`AI-SCRATCHER`** is a new top-level child of `OPEN-TRADER`, a sibling of `HUD` / `ENGINE` /
  `INFRA`, so the product's own rollup covers both of AI-Scratcher's roles under one item.
- **`AI-SCRATCHER-GATE`** (renamed from the first cut's `AI_SCRATCHER`) keeps only the CI/dev-tool
  leaf and is multi-parented to `INFRA` (it *is* verification infrastructure) and to `AI-SCRATCHER`.
- **`AI-SCRATCHER-SERVER`** is new and holds the CMake dependency leaf, which the first cut had
  mis-parented under the gate branch: that leaf's purpose is supplying the *application's* Python
  environment. It also now holds two follow-up leaves: the application spawning the installed
  server (§2.3) and, once packaging is real, a self-contained Python runtime (§6.2). Per "no
  sequential relations between items" each stays independently defined by its own test; this
  branch groups them only by which side of AI-Scratcher's two roles they serve.
- **`ENGINE` was considered and rejected** for the server/scripts items: `ENGINE`'s own description
  scopes it to "the exchange trading process domain" (`CPP-API`, `PYTHON-API`, `MCP` under
  `TRADE-API` are all trading-API bindings). Spawning a generic tooling server and hosting a browser
  view is not exchange-trading logic, so it does not fit that branch's stated contract; it is cross-
  cutting product infrastructure, which is what the new top-level `AI-SCRATCHER` item is for.
- **`CONTENT_PANEL/CHROME_PANEL/AI-SCRATCHER-PANEL`** — accepted. `CHROME_PANEL` is a new content
  panel specialization sibling to `INSTRUMENT_PANEL` / `WALLET_PANEL` / `BOT_PANEL`, representing
  the panel technology (an embedded off-screen web view) rather than one specific tool, so the
  order-form and wallet Chrome panels named in the original pivot request are future siblings of
  `AI-SCRATCHER-PANEL` under the same node, not a redesign of it. `AI-SCRATCHER-PANEL` is also
  parented back to `AI-SCRATCHER`, so both DAG paths — "a kind of content panel" and "a facet of
  AI-Scratcher" — read directly off the tree. It is deliberately left childless and untested: the
  panel does not exist yet (§3–4), and pre-creating a roadmap of unimplemented leaves ahead of
  actual work would violate test-first practice.

## 6.1 One installation, every consumer resolves it — done 2026-09-28

The gate/dev venv and the CMake build-time venv started as two independent installs of the same
package (`ci/venv.sh` ran its own `pip install ai-scratcher @ git+…`, unrelated to
`cmake/AiScratcher.cmake`). Unified: **`cmake/AiScratcher.cmake` is now the only place AI-Scratcher
is ever installed from**, and every other consumer resolves that one installation rather than
creating its own.

- **`cmake/ai_scratcher/CMakeLists.txt`** is a standalone entry point: `project(ai_scratcher_provision NONE)`
  (no language, so no C++ compiler is probed) that just `include()`s `cmake/AiScratcher.cmake`.
  `cmake -S cmake/ai_scratcher -B <dir> && cmake --build <dir> --target ai_scratcher_venv`
  provisions the exact same venv the full application build would, in complete isolation from it.
- **`ci/venv.sh`** now only resolves and prints a path, never installs directly: it reuses
  `<app build dir>/ai-scratcher-venv/bin/python` when a build tree already exists (default
  `cmake-build-debug-clang`, override `OPENTRADER_BUILD_DIR`), else provisions one standalone via
  the entry point above into `.ai-scratcher-provision/`. Verified both branches locally: reuse
  picks up an existing build tree's venv instantly; the standalone branch configures and builds
  with the system's plain `python3` and no compiler, producing a working `import ai_scratcher`.
- **`ci/gate.sh`** captures `PY="$(bash ci/venv.sh)"` instead of hardcoding `.venv-syngate`.
- **CI** (`.github/workflows/validate.yml`): the `approval` and `syngate` jobs each resolve the
  interpreter through `ci/venv.sh` (cached via `actions/cache` keyed on
  `cmake/AiScratcher.cmake` + `cmake/ai_scratcher/**`, populated by whichever job runs first) and
  no job hardcodes `.venv-syngate` any more. Open Trader's own `scripts/tests` (workflow-config
  structure checks) never needed AI-Scratcher at all — they were only sharing its venv for
  convenience — so that pytest step was decoupled onto the job's plain system interpreter with a
  two-package `pip install pytest pyyaml`, which also removes the accidental coupling where
  bumping AI-Scratcher's dependencies could break an unrelated test step.
- `AI-SCRATCHER-GATE-010`'s item description was narrowed to state exactly this: resolve the
  CMake-provisioned installation, never install by any other means.

## 6.2 Self-contained Python runtime for packaging — not yet implemented

`cmake/AiScratcher.cmake` currently builds the venv from whatever `find_package(Python3)` finds on
the **build machine**; that says nothing about the **end-user machine** a packaged build ships to.
Research (below) confirms this matters only once packaging is real: a venv is not a Python
installation, and shipping one alongside `trader` still requires *some* real CPython — either the
end user's system one (fragile: version/ABI drift, "no Python installed" support burden) or a
build fetched via CPM (e.g. `python-build-standalone`, the same portable-CPython project `uv
python install` uses) and used as the interpreter the venv is built from. `AI-SCRATCHER-SERVER-030`
is left unreviewed and childless — the packaging step (a CPack rule or an `install()` of the venv
directory next to the installed executable) does not exist yet, so there is nothing to test.

## 6.3 Embedding vs. an external process — settled, no SWIG, no pybind11 for either use case

Researched two questions together, since both are "run/expose Python from C++": how the app should
run AI-Scratcher's own server, and how the future `PYTHON-API` trading-bot binding should work.

- **Running AI-Scratcher's server stays an external process** (§2.1's decision, now reinforced):
  `pybind11::scoped_interpreter` forbids more than one live embedded interpreter per process and
  is fragile across finalize/re-init; it also needs `Development.Embed` (Python headers *and*
  `libpythonX.Y`) at build time, which **a venv alone never provides** — those exist only in the
  base CPython install (system or a full standalone build) the venv was created from. An external
  subprocess needs none of that: no compiler-time Python dependency at all, no GIL interleaving
  with the GTK/CEF host loop, and it is what `python_host` (§2.3) already does.
- **`PYTHON-API` (`syngate/engine/PYTHON-API.yml`) is settled the same way, and SWIG is rejected**:
  SWIG generates a `_module.so` so *Python code can call into C++* — the opposite direction from
  "run a Python package" — and current sentiment (SWIG 4.5.1, 2026) treats it as legacy next to
  pybind11/nanobind for that direction too (opaque generated code, weak modern-C++ support). Since
  bots are already decided as out-of-process via MCP, `PYTHON-API`'s item now describes a Python
  *client package* over that same out-of-process protocol — a Pythonic call surface with no
  compiled binding at all, embedded interpreter or SWIG extension — rather than opening a second,
  inconsistent access mechanism next to MCP.
- No official C++ MCP SDK exists yet; a future in-process client for `PYTHON-API` means either a
  small hand-rolled JSON-RPC layer over stdio/socket (matches the existing wire shape) or, only if
  low-latency in-process calls become a hard requirement later, pybind11 (never SWIG) as a
  separate, deliberately scoped follow-up.

# 7. CI redesign: entry checks as required status checks, AI-Scratcher as a build artifact — done 2026-09-28

Reviewed the CI job graph on request and simplified it from five jobs to four.

## 7.1 License and re-approval: required status checks on `main`, not informational jobs

The `license` and `approval` jobs only ever reported a violation after the fact; nothing made
either check's outcome required, so a bad commit reached the shared repository regardless. The
defect was never *where* the checks ran but that nothing was *required*, and github.com offers
exactly one content-aware server-side block: a ruleset on `main` with the workflow's jobs as
required status checks. A push to `main` whose tip commit lacks a passing run is then rejected by
the server (`GH006 … Required status check "license" is expected`), and since checks bind to the
commit SHA the maintainer's PR-free flow survives — push a topic branch, let CI go green,
fast-forward `main` to the same commit.

- `license` stays a standalone, dependency-free job (plain `python3`, ~10 s) for fast feedback.
- The re-approval check is a step of the `syngate` job, not a job of its own: the
  `scripts/check_self_approval.py` shim now imports `ai_scratcher`, so it needs the interpreter
  that job already resolves through `ci/venv.sh`. Range logic (`before..HEAD`, merge-base fallback
  for a new branch) is the old `approval` job's, minus the `pull_request` arm: `INFRA-066` is a
  reviewed, frozen requirement that pull requests are deliberately not a trigger, which also means
  fork pull requests get no checks and therefore cannot merge — consistent with the push-only flow.
- The ruleset itself lives in the tree as `.github/rulesets/main.json` (the format GitHub's
  Settings → Rules → Rulesets → Import accepts): active, empty bypass list (admins included), every
  Validate job required, force pushes and deletions blocked, signed commits required — which turns
  `CONTRIBUTING.md`'s "every commit must be GPG-signed" from prose into a server-side rule too.
  Applying it is a repository-admin action outside the tree (§8).
- Git hooks were tried first (`.githooks/` + a documented `git config core.hooksPath`, and before
  that a CMake side effect) and dropped: `.git/hooks` cannot be versioned, a tracked hook directory
  needs a per-clone instruction, and either is bypassed by `--no-verify` — none of it is a
  guarantee, and the server gate makes the extra instruction surface pointless.

Deliberately no auto-fix bot (HawkEye, addlicense, `insert-license` can push a fixing commit): a
bot commit is unsigned, so it would violate the signing rule and be rejected by the ruleset.
`check_license.py` without `--check` already is the fixer, run locally under the maintainer's key.

One real bug this surfaced and fixed: `.ai-scratcher-provision/` (this session's own gitignored
scratch directory, §2.2/§6.1) was never added to `.licensecheck-ignore`, so a real `pre-commit` run
exploded into hundreds of false "MISSING header" hits against pip's and PyYAML's own vendored
files. Invisible while the check only ran in CI against a directory that never existed there;
immediately visible the moment the check became a real local gate. Fixed alongside this change.

## 7.2 `syngate` job installs from a build artifact, not a second fetch

`build` already fetches AI-Scratcher's source via CPM during its CMake configure step (the sole
place it is ever fetched, per §6.1) before any C++ is compiled. That source is now uploaded as a
build artifact (`ai-scratcher-source`, from `build-ci/_deps/ai_scratcher-src`); the `syngate` job
downloads it and does a local, network-free `pip install` from it. `ci/venv.sh` gained a middle
tier for this (`AI_SCRATCHER_SOURCE_DIR`), tried after "reuse an app build tree" and before failing.

## 7.3 `cmake/ai_scratcher/CMakeLists.txt` removed, not kept as a fallback — revised on request

The standalone provisioning project (built to let a fresh checkout run `syngate` without ever
building the C++ application) is gone, along with the fallback tier in `ci/venv.sh` that used it.
Decided explicitly, not a compromise: not building the project first is not a supported starting
point here. Every contributor is expected to configure and build (an early-development project,
not a published tool with outside users yet), and a scoped "just provision AI-Scratcher" step
already exists as an ordinary named target — `cmake --build <dir> --target ai_scratcher_venv` —
inside the one project, which is exactly the kind of "special option to configure the syngate
scripts only" that would otherwise have motivated a second `CMakeLists.txt`.

To make that target actually reachable through the *documented* build command, `trader` now
depends on it (`add_dependencies(trader ai_scratcher_venv)`) — closing a gap the design had left
open since §2.2 first shipped, where building `--target trader` alone did not provision the venv.
`ci/venv.sh`'s third tier is now a clear failure with the exact command to run, not a fetch:

```
ci/venv.sh: no AI-Scratcher installation found under <build dir>.
Configure and build the project first, e.g.:
  cmake -B <build dir> -G Ninja -DCMAKE_BUILD_TYPE=Debug
  cmake --build <build dir> --target ai_scratcher_venv   # or --target trader, which depends on it
```

## 7.4 Net result

| | Before this section | After |
|---|---|---|
| Jobs | `license`, `approval`, `build`, `test`, `syngate` | `license`, `build`, `test`, `syngate` (re-approval a step of `syngate`) |
| License / re-approval | CI-only, informational (nothing required their success) | required status checks in the `main` ruleset; a failing commit cannot enter `main` |
| Ruleset | (did not exist) | `.github/rulesets/main.json`, applied once by the repository admin |
| `syngate` job's AI-Scratcher install | its own standalone CMake configure + CPM fetch | `pip install` from the `build` job's already-fetched source (artifact download, no network fetch) |
| A checkout that never builds | `syngate`/the hooks would fetch and provision for it | not supported; clear error naming the build command |
| `cmake/ai_scratcher/CMakeLists.txt` | existed, CI's only path | removed |

Verified after the redesign: the tree validates (added `INFRA-071`, the required-status-checks
leaf, alongside the existing AI-Scratcher items), the gate passes, the Open Trader `scripts/tests`
suite passes (`INFRA-071`'s binding asserts the `license` job, the re-approval step and that the
ruleset requires every Validate job), and `build-error-locator` confirmed building `--target
trader` from a clean `ai-scratcher-venv` now provisions it as a side effect.

# 8. Open points for the user

- GitHub remote for `ai-scratcher` and the first (signed) commit.
- Whether the shims `scripts/syngate.py` / `scripts/check_self_approval.py` stay (they keep the
  frozen CI test and the documented commands working) or the workflow is re-reviewed to call the
  `syngate` entry point directly.
- Apply `.github/rulesets/main.json` to the repository (Settings → Rules → Rulesets → New ruleset →
  Import a ruleset). Until then the checks run but nothing requires them (§7.1).
