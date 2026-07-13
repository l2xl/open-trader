# Project Standards

## Two-tier code organisation

The project uses a deliberate two-tier convention. The lower tier is **library-layer code** (e.g. `src/datahub/`, `src/connect/`) — generic, reusable building blocks written in the idiom of the C++ Standard Library and Boost.
The upper tier is **application-layer code** (e.g. `src/cockpit/`, `src/app/`) — concrete OOP classes that orchestrate the library blocks. Study the existing files in the same component and follow the established style.

## Pointers & ownership

- **Raw pointers are prohibited in any form** — no raw owning pointers, and no raw non-owning pointers either. Use appropriate c++ stdlib smart pointers (including std::optional) for optionally nullable pointers, or references for non-owning function parameters with strictly bounded lifetime.
- When usage of the stdlib shared pointers is impossible due to third-party library design – explicitly create own pointer wrappers to build strong memory ownership model 
- **`const char*` is prohibited** as a string type and must be replaced with `const std::string&` (or `std::string_view` where the callee genuinely just observes a borrowed range). Isolated special cases — interop with C APIs, `extern "C"` boundaries, compile-time string literals consumed by templates that demand pointer-to-char — are the only permitted exceptions and should be commented to record why.
- Any class designed to be used through a smart pointer must hide its constructor behind a private passkey tag (typically `struct ensure_private {}` / `struct EnsurePrivate {}`) and expose a static factory: `create(...)` for the library tier, `Create(...)` for the application tier. The factory is the single point of construction and is the correct place to perform `shared_from_this`-dependent initialisation, two-phase init, or registration with collaborators.
- Every library-tier factory must additionally have a free `make_<type_name>(...)` wrapper that perfect-forwards to `create()` so template arguments are deduced at the call site.
- Inherit from `std::enable_shared_from_this<Self>` whenever the class needs to hand out `shared_ptr<Self>`/`weak_ptr<Self>` to its own asynchronous machinery.
- Apply RAII aggressively. Resources are acquired in constructors (or in `Create` / `create`) and released in destructors. There is no manual `cleanup()` / `shutdown()` step that callers must remember.
- Prefer move semantics. Pass by value + `std::move` when the callee will store the argument; pass by rvalue reference for sink parameters; pass by `const&` only when the callee genuinely just observes. Eliminate redundant copies on sight.

## Layering & dependency direction

- Strict layered architecture: application layer depends on library layer, never the reverse. Within each layer, dependencies flow from higher-level components to lower-level ones (e.g. `app/elements → cockpit → datahub/connect/common`).
- Apply the SOLID principles. Keep concerns separated; review every new class for a single, articulable responsibility.
- Study the patterns already in place in the surrounding component before adding new code, and conform to them. Consistency with the local idiom takes precedence over personal preference.

## Library-layer design idioms

- **Policy-parameterised templates over virtual dispatch.** When variation is known at compile time (entity type, key field, handler callable, acceptor tuple), express it as a template parameter rather than a virtual base class. Reserve `virtual` for genuine runtime polymorphism boundaries (e.g. `data_subscription::handle_data`).
- **Concept-constrained generics.** Constrain template parameters with C++20 concepts (`std::ranges::input_range`, `std::convertible_to`, `std::invocable`, …) so misuse fails at the call site with a readable diagnostic.
- **Range views, not containers.** Accept and pass `std::ranges::subrange` (or other view types) for batched data. Never assume a parameter is a container — do not call `.size()`, `.push_back()`, or assume random access unless the concept guarantees it.
- **Reactive pipeline pattern.** Producer/consumer wiring is built from the family `data_dispatcher → data_adapter → data_provider / data_feed → data_sink → data_subscription`. New stages must follow the established contract: `subscribe(weak_ptr<subscription>)`, `handle_data(data_type&&)`, snapshot-on-subscribe semantics, weak-ptr subscriber lists pruned on `lock()` failure, `update_kind::snapshot | delta` to distinguish initial state from incremental changes.
- **Specification objects** (`data_condition` and similar) encapsulate filter/predicate logic as first-class values that can be plugged into pipeline stages.
- **JSON boundary.** Use Glaze with `glz::opts{.error_on_unknown_keys = false}` for external API payloads that may add fields. Use `glz::array` meta only when the wire format demands array-encoded fields (e.g. ByBit `[price, size]`); otherwise rely on default reflection.

## Application-layer design idioms

- Concrete classes are factory-constructed (`Create` + `EnsurePrivate`), and own their collaborators through `shared_ptr` / `unique_ptr`.
- Use the **composite pattern** for hierarchical UI / domain trees (e.g. `PanelNode` → `LeafPanelNode` + `SplitPanelNode`). Each node is independently shared-ptr-managed, owns its sub-tree, and cleans up via its destructor.
- Use **two-phase initialisation** (`Create()` followed by `Initialize()`) when callbacks need a fully-formed `shared_from_this()` to capture, but keep both phases inside the factory whenever possible so callers see a single construction step.
- A single controller (e.g. `MainWindow`) orchestrates lifecycle and inter-component wiring; UI-emitted callbacks capture `weak_ptr<>` and route to controller handlers.

## Asynchrony & concurrency

- The async backbone is Boost.Asio with C++20 coroutines. Each long-lived asynchronous component is **strand-confined**: it owns a `boost::asio::strand<...>` and serialises all internal state mutations through it.
- **All coroutine methods are `static`** and take `std::weak_ptr<Self>` (and any other required state) as parameters: `static boost::asio::awaitable<T> co_method(std::weak_ptr<Self> ref, ...)`. Inside, lock the weak pointer at each suspension boundary and bail out if it expired.
- Never write a non-static coroutine that touches `this`. Never pass `this` (raw or shared) into coroutines, asynchronous handlers, or detached tasks — always pass `weak_ptr`.
- Do not use lambdas as coroutine bodies. A lambda that returns `awaitable<T>` is forbidden by project policy; convert to a static method.
- Where producers and a serialising consumer must rendezvous on a shared resource (e.g. a single websocket stream), use `boost::asio::experimental::channel` as a mailbox; a single coroutine drains the channel and performs the protected operation.
- Prefer lock-free primitives (`boost::lockfree::spsc_queue`, `std::atomic`) over mutexes for hot paths between exactly one producer and one consumer.

## Formatting & includes

- Do not wrap lines shorter than 220 characters.
- Never use include paths that traverse up-folder (`../`); arbitrary in-project relative paths are forbidden. Always include via the canonical path that CMake exposes.
- Place file-local helper functions in an anonymous namespace at the top of the `.cpp` file. Do not expose them as free functions in headers unless they are part of the component's public surface.
- Make default class members defined within the class body. 
- If a constructor contains only the initialization list, then define it within the class body.

## Comments & self-documentation

- The best documentation is the code itself: prefer self-describing identifiers over prose.
- Write comments only to capture decisions that the code cannot express — non-obvious invariants, protocol quirks, deliberate trade-offs, references to external specifications. Routine "what this does" comments are noise and must be removed.
- Doxygen-style `@brief` blocks are acceptable on public library-layer types when they document a contract that callers must respect; they are not required and should never restate obvious behaviour.

## Language level & toolchain

- Target is clang-23 with C++23 enabled. This permits `glz` reflection of plain aggregates without `glz::meta` injections; rely on it.
- Use `iostream`-family streams (`std::clog`, `std::cerr`, `std::cout`) for type-safe formatted output rather than `printf`.

## Build system & repository

- Any source addition, rename, removal, or directory restructuring must be reflected immediately in the corresponding CMake target. Build-system changes are part of the same changeset, not a follow-up.
- See [BUILD.md](BUILD.md) for build invocation details.

# Requirements & the TDD gate

Project requirements are tracked formally in a [Doorstop](https://github.com/doorstop-dev/doorstop) tree under `req/` (documents `PRODUCT ← CORE/DATA_MODEL/TRADER_HUD/APP/INFRA`), decomposed from the reviewable narrative in [requirements_plan.md](requirements_plan.md). See [req/README.md](req/README.md) for the operating guide.

Binding process rules:
- **Test-first, then freeze.** For a requirement leaf, the covering Catch2 `TEST_CASE` is tagged with the leaf UID (`[DATA_MODEL-042]`) and bound via the leaf's `references:` before implementation. The user approves by running `doorstop review <UID>`, which fingerprints the item and its test files' SHAs.
- **`doorstop review` / `doorstop clear` are user-only.** Review state is the record of the user's approval; no contributor or agent stamps or clears a review. The whole tree currently stands unreviewed (drafted, pending review).
- **Frozen tests are immutable.** Editing a reviewed item's referenced test reddens the gate until the user runs a sanctioned `doorstop clear` + re-review.
- **The gate must pass.** `ci/gate.sh` (and `.github/workflows/validate.yml`) run `doorstop --error-all` plus the frozen-test and coverage checks; they bite only on reviewed items.

# The Exchange Scratchpad Components

## Data Pipeline

Project exclusively uses the internally developed `datahub` library to automate data pipelines. Any other ways to organize data flow are forbidden.

For any work involving the `datahub` component — using its API, extending the pipeline, or modifying its internals — see [src/datahub/README.md](src/datahub/README.md).

## Connection Abstraction

The Data Pipeline begins from one or a number of data sources. The project uses a thin abstraction library over boost/beast called `connect` to implement communication over network. The `connect` library is also used to send any requests/data over the network.

For any work involving the `connect` component — opening HTTP queries or WebSocket connections, handling transport errors, or modifying the abstraction — see [src/connect/README.md](src/connect/README.md).

## Data Model

The data model is the main Data Pipeline parameter. It is defined by entity types and per-provider implementations derived from the `IDataController` interface.

For any work involving data entities, `IDataController` implementations, or persistence — using, extending, or modifying them — see [src/data/README.md](src/data/README.md).

## Trader Cockpit

User experience is orchestrated by the central Exchange Scratchpad component — `The Trader Cockpit`.

For any work involving `TradeCockpit`, `ContentPanel`, or panel registration and data routing — using, extending, or modifying them — see [src/cockpit/README.md](src/cockpit/README.md).

## Application layer

Application lifecycle management and UI implementation.

For any work involving application startup, panel tree nodes, `UiBuilder`, or UI-engine-specific panel implementations — using, extending, or modifying them — see [src/app/README.md](src/app/README.md).

## Tests

Catch2 v3 test suite with per-component test executables mirroring the src/ layout.

For any work involving tests — adding a new test executable, modifying existing tests, or running a single test — see [test/README.md](test/README.md).

## Other components

* `common` - common utilities
* `core` - core library (BOOST ASIO-based task scheduler for now) and generic_handler callback implementation helper
