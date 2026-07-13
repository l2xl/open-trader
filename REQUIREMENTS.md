# Requirements Plan — Exchange Trading Client

## 0. Method & tooling

Requirements are managed with **Doorstop v3.1** (decided). Repository layout: `req/{prd,dh,dm,ck,ap,ti}`, each a Doorstop document. `prd` is the product features/cases document (derived from the domain/feature/case survey in `trading.json`); the five component documents — CORE (data backbone), DATA_MODEL (exchange integration & domain data), TRADER_HUD (trader cockpit), APP (application shell & UX), INFRA (verification & requirements infrastructure) — hold the leaf requirements below, each linking up to its covering PRODUCT item.

**Requirement semantics:** every item records mandated functionality — a property of the product or its infrastructure that must hold and stay verifiable — never an executed or planned ticket/task; one-off process actions and "remove/avoid X" work items do not belong in the tree. A requirement states the **goal**, not the exact way to reach it: the `header` attribute names the goal in a few words (used as the label in status reports); `text` is a concise normative statement that may add the preferred delivery detail where the choice itself is deliberate; the accept/test attributes carry the rest of the how. Every requirement must be verifiable in terms observable by its bound test — wording that cannot be checked (or only holds outside the verification environment) disqualifies the item as a requirement. Test binding is ideally one-to-one: each leaf references its own dedicated test; a single test covering several requirements is tolerable in seldom cases, and a many-to-many requirement-to-test relation always signals wrong decomposition (test binding is file-granular, so shared test files produce exactly that smell).

Every leaf requirement carries a Doorstop `references:` entry of keyword type whose keyword is the leaf's own UID (e.g. `CORE-007`), matching a Catch2 tag `[CORE-007]` on the TEST_CASE(s) that cover it. The tag is simultaneously the traceability link and a per-requirement Catch2 test filter (`--tags [CORE-007]`).

**TDD freeze workflow:** tests are written and reviewed before a requirement is considered done. User approval of a leaf triggers `doorstop review <ID>`, which stamps a fingerprint over the item text and its referenced files' SHAs. `doorstop` itself never re-verifies a stamped fingerprint after the fact — that gap is closed by dedicated CI tooling, not by Doorstop.

**CI gate** (`ci/gate.sh`) runs `doorstop --error-all`, `scripts/check_frozen_tests.py` (re-hashes reviewed items' referenced test files on top of Doorstop's own suspect-link fingerprints), and `scripts/check_req_coverage.py` (every normative leaf has a test reference or an explicit `verify: inspection|demo` attribute); the build and offline `ctest` run as their own Validate-workflow steps (INFRA-066, INFRA-067) ahead of the requirement status rollup (INFRA-068). Post-freeze edits to a reviewed item or its referenced tests turn the gate red until the user runs `doorstop clear` and re-reviews — Doorstop review/clear is a user-only action; no agent stamps or clears a review.

This document itself is decomposed into Doorstop items on import: items land **unreviewed**, and Doorstop's review state is exactly the mechanism that tracks the user's approval going forward. The bracketed IDs used throughout the sections below (`CORE-001`, `DATA_MODEL-034`, …) are the actual Doorstop UIDs, not placeholders.

## 1. Data backbone — datahub + connect + core (CORE)
The data backbone comprises the datahub reactive pipeline (dispatcher → adapter → sink → model + feed → subscription — per CONTRIBUTING.md the only permitted dataflow mechanism), the connect boost/beast transport wrapper, and the core ASIO scheduler. The end-to-end path works today for ByBit market data, but has correctness defects (adapter error swallowing with double-dispatch fall-through, silent SPSC overflow drop, unsynchronized feed mutation, terminal-STALE websockets, unverified TLS, immortal scheduler) and lacks primitives every higher branch depends on (DB→feed replay, removal propagation, transactional writes, sequence-gap hooks, reconnect with backoff, staleness detection, rate limiting). This branch hardens the backbone to production quality while staying venue-agnostic: exchange auth/signing, topic resubscription orchestration, and REST bootstrap remain in the DATA_MODEL branch; status/alert UI remains in the APP branch.

### 1.1 Pipeline error routing & ingress integrity
Errors raised anywhere in the pipeline must reach a designated error path exactly once, and ingress overflow must be bounded, observable, and policy-driven instead of silent.

#### CORE: Case 1.1 — Adapter error routing
Stop `data_adapter`'s catch-all (src/datahub/data_sink.hpp:38-51) from swallowing downstream exceptions and mis-routing the message to the next adapter.
- **[CORE-001]** `data_adapter::operator()` shall exclude the downstream handler invocation from its parse-failure handling so a handler exception is never reported as a JSON mismatch and never causes dispatcher fall-through to the next acceptor. *Accept:* with a throwing handler on adapter A and a counting adapter B behind it, B receives 0 messages that A successfully parsed. *Test:* Catch2 unit: dispatcher with throwing-handler adapter + counting adapter; assert count==0.
- **[CORE-002]** `data_adapter` shall accept an optional error callable receiving `std::exception_ptr`, invoked when the downstream handler throws, defaulting to log-and-continue. *Accept:* the error callable fires exactly once per throwing message and the rethrown pointer matches the thrown exception type. *Test:* Catch2 unit: handler throws custom type; assert callable count and rethrown type.
- **[CORE-003]** `data_sink::accept` shall route exceptions from `m_model->accept` into `handle_error`, not only those from `handle_data`. *Accept:* a model whose `accept` throws triggers exactly one `handle_error` call and the sink processes the next batch normally. *Test:* Catch2 unit: mock model throws once; assert error fired, then re-accept succeeds.

#### CORE: Case 1.2 — Ingress backpressure policy
Replace the hardcoded 1024-slot SPSC silent drop (src/datahub/data_sink.hpp:74,86) with configurable, observable, multi-producer-safe ingress.
- **[CORE-004]** `data_dispatcher` queue capacity shall be a construction parameter (default 1024). *Accept:* a dispatcher built with capacity N accepts exactly N unconsumed messages before overflow with the drain suspended. *Test:* Catch2 unit: io_context not run; push N then N+1; inspect acceptance.
- **[CORE-005]** Overflow shall be observable: a monotonic dropped-message counter plus an optional overflow callback invoked per dropped message. *Accept:* pushing capacity+k messages with drain blocked reports exactly k via both counter and callback. *Test:* Catch2 unit: blocked strand, push over capacity, assert k drops.
- **[CORE-006]** `data_dispatcher::operator()` shall be safe to call from multiple producer threads (WS read loop, REST handlers) without corruption or UB. *Accept:* 4 producer threads × 10 000 messages with a live drain deliver exactly pushed-minus-dropped messages intact, TSAN-clean. *Test:* Catch2 stress under TSAN: 4 producers, checksum delivered set.

### 1.2 Feed concurrency & entity lifecycle
In-memory feeds must be safe under cross-thread access and support the full entity lifecycle — removal, bounded retention, gap awareness — that order/position flows will require.

#### CORE: Case 2.1 — Thread-safe feed access
Fix the documented UI-thread vs data-thread race on feed cache and subscriber list (ByBitDataManager cross-thread hazard).
- **[CORE-007]** `subscribe()`, `set_condition()`, and snapshot access on all feed flavors shall be safe to call concurrently with acceptor pushes. *Accept:* stress run (1 pushing thread, 4 threads churning subscribe/drop/read, ≥10 000 ops) completes TSAN- and ASAN-clean. *Test:* Catch2 stress executable run under TSAN/ASAN in CI.
- **[CORE-008]** A subscriber joining during live pushes shall receive one snapshot then increments with no lost or duplicated keys relative to the pushed set. *Accept:* for 100 randomized subscribe-during-push interleavings, each subscriber's accumulated view equals the feed cache at quiescence. *Test:* Catch2: randomized interleaving harness; compare key sets at quiescence.

#### CORE: Case 2.2 — Removal propagation
`update_kind` gains `remove` so order/position deletions can flow through feeds to subscribers.
- **[CORE-009]** `update_kind` shall gain `remove`, and keyed/sorted feeds shall expose remove-by-key that erases matching cache entries and dispatches the removed entities to subscribers tagged `update_kind::remove`. *Accept:* after `remove(key)`, existing subscribers received one remove event containing the entity and a late subscriber's snapshot excludes it. *Test:* Catch2 unit per feed flavor: insert, remove, assert event and snapshot.
- **[CORE-010]** Existing snapshot/increment subscriber callables shall compile and behave unchanged; removal-aware delivery is an opt-in callable shape selected by `make_subscription` arity/signature. *Accept:* the current test-suite callables compile unmodified; a removal-shaped callable receives remove events; wrong shapes still fail with the readable static_assert. *Test:* Catch2 unit + compile-check targets in test/datahub.

#### CORE: Case 2.3 — Bounded feed caches
Stop unbounded feed cache growth over long sessions.
- **[CORE-011]** Sorted feeds shall accept an optional retention policy (max entry count or sort-key horizon) evicting oldest entries on overflow, distinct from domain removal (no remove event emitted for eviction). *Accept:* a feed capped at 1000 entries never exceeds 1000 across 10 000 pushes and retains the newest 1000 by sort key. *Test:* Catch2 unit: push 10k, assert size and boundary sort keys.
- **[CORE-012]** Eviction shall never invalidate in-flight increment ranges; if trimming would affect pending update bounds the feed downgrades that dispatch to a full snapshot (same discipline as the out-of-order-insert downgrade). *Accept:* eviction stress produces no dangling ranges (ASAN-clean) and subscriber state equals cache state at quiescence. *Test:* Catch2 stress under ASAN with retention limit of 8.

#### CORE: Case 2.4 — Sequence-gap detection hooks
Provide a generic primitive to detect missed exchange deltas (orderbook `u`/`seq`) and trigger consumer resync.
- **[CORE-013]** datahub shall provide a gap-detecting stage parameterized with a sequence-extractor callable, invoking a gap callback with (expected, received) on non-contiguous sequence while still forwarding data downstream. *Accept:* feeding seq 1,2,4 fires the callback exactly once with (3,4) and all three entities reach the downstream acceptor. *Test:* Catch2 unit: scripted sequence; assert callback args and passthrough.
- **[CORE-014]** The gap stage shall support baseline reset so a consumer-driven resync (fresh snapshot) re-arms detection without false positives. *Accept:* after reset, sequences 10,11 fire no callback; a following 13 fires (12,13). *Test:* Catch2 unit: reset mid-stream; assert callback pattern.

### 1.3 Persistence robustness
SQLite persistence must gain atomic batch writes, schema evolution safety, and numerically queryable currency columns.

#### CORE: Case 3.1 — Transactional batch writes
Reinstate the commented-out Transaction machinery (src/datahub/operations.hpp) and make batch persistence atomic.
- **[CORE-015]** A RAII `Transaction` operation (begin on construction, rollback in destructor unless committed) shall be restored in operations.hpp and covered by the DAO test suite. *Accept:* an exception mid-batch inside a transaction leaves row count unchanged; commit persists all rows. *Test:* Catch2 DAO test: inject failure mid-batch, assert rollback; commit path asserts count.
- **[CORE-016]** `data_model::accept` shall wrap multi-row batches in a single transaction instead of one autocommit upsert per row, preserving per-row changed-detection semantics. *Accept:* a 1000-row batch is all-or-nothing under induced failure and still returns only actually-changed rows on success. *Test:* Catch2 DAO: throwing codec mid-batch → 0 rows; success path asserts dedup result.

#### CORE: Case 3.2 — Schema migration
Replace bare `CREATE TABLE IF NOT EXISTS` with versioned schema so entity shape changes cannot silently misalign columns.
- **[CORE-017]** Each reflected table shall record a schema version (derived from column names/types); on open, a mismatch between stored and computed schema shall be detected and reported instead of proceeding. *Accept:* opening a DB created with an older entity shape raises a typed schema-mismatch error naming the table; matching schema opens silently. *Test:* Catch2: create legacy-shape table via raw SQL, open with new entity, assert error.
- **[CORE-018]** Additive migrations (new optional/defaulted trailing fields) shall be applied automatically via `ALTER TABLE ADD COLUMN`; non-additive changes shall fail fast unless a caller-registered migration callable handles them. *Accept:* an entity with one added `std::optional` field opens an old DB, migrates, and queries return defaults for old rows. *Test:* Catch2 round-trip: write old shape, reopen with extended entity, query.

#### CORE: Case 3.3 — Currency SQL queryability (defer)
Make price/size columns usable in SQL range and aggregate queries (needed by History & Audit queries) instead of opaque TEXT.
- **[CORE-019]** (defer) Currency fields shall be persisted in a numerically comparable form (INTEGER mantissa with per-column scale recorded in table metadata) so `BETWEEN`, ordering, and `SUM` work in SQL. *Accept:* a query with `price BETWEEN` bounds returns exactly the rows matching fixed-point comparison, and `SUM` matches in-memory fixed-point summation. *Test:* Catch2 DAO: seed known currency values, assert BETWEEN and SUM results.
- **[CORE-020]** (defer) The numeric representation shall round-trip currency exactly across the full mantissa range and be reachable from existing TEXT-based DBs through the CORE-017/018 migration path. *Accept:* write/read round-trip preserves value and scale for boundary mantissas (0, 1, 2^63−1 for uint64); legacy TEXT table migrates with equal values. *Test:* Catch2 property test with boundary values + migration fixture.

### 1.4 Historical replay bridge
Bridge persisted history back into the feed/subscription layer — the README-declared but unimplemented `db_data_feed` — so charts and tables can backfill from SQLite.

#### CORE: Case 4.1 — db_data_feed and the backfill-live seam
- **[CORE-021]** `db_data_feed<Entity>` shall be implemented per the README contract: constructed over a `data_model` and a `data_condition`, `subscribe()` delivers matching rows as `update_kind::snapshot` ordered by the sort field. *Accept:* pre-persisted rows matching the condition arrive on subscribe, sorted, complete. *Test:* Catch2 vs on-disk temp SQLite: seed, subscribe, assert order and content.
- **[CORE-022]** Large results shall be delivered in configurable chunks (default 1024 rows) rather than materializing the full query result. *Accept:* 10 000 matching rows arrive in ceil(10000/chunk) callbacks with global order preserved. *Test:* Catch2: seed 10k rows, count callbacks, verify ordering across chunks.
- **[CORE-023]** A backfill-then-live composition shall emit the DB snapshot first, then attach to a live feed with key-based dedup at the seam so overlapping rows are delivered once. *Accept:* a row present both in DB and in the live cache reaches the subscriber exactly once; total delivered keys equal the union. *Test:* Catch2: persist rows, push overlapping live batch, assert no duplicate keys.
- **[CORE-024]** `db_data_feed` queries shall execute on the owning DB strand and deliver asynchronously without blocking the subscribing caller. *Accept:* `subscribe()` returns before delivery completes; callbacks arrive serialized on the DB strand. *Test:* Catch2 with scheduler: latch on callback thread id, assert non-blocking subscribe.

### 1.5 Conditions & alert evaluation
`data_condition` must become expressive enough for real filters and gain an evaluation primitive that turns any condition into an edge-triggered alert (alert UI/notification routing belongs to APP).

#### CORE: Case 5.1 — Condition composition & SQL binding
Fix the AND-only composition and the unbound-SQL-parameter asymmetry (src/datahub/data_condition.hpp, query_builder.hpp).
- **[CORE-025]** `data_condition` shall support `or_` composition reachable through its own surface, correct for both `matches()` and `to_query_condition()` including parenthesization of mixed AND/OR. *Accept:* `(a || b) && c` evaluates correctly in-memory for all 8 truth combinations and the generated SQL WHERE returns the same row set. *Test:* Catch2: truth-table unit + DAO query equivalence check.
- **[CORE-026]** `field_predicate` shall bind its stored runtime value into the generated SQL parameters so `query(condition)` needs no re-supplied positional arguments; `QueryCondition::where`'s ignored variadic args shall be removed or honored. *Accept:* `query(equal<&E::x>(42))` returns the same rows as manual SQL with 42 bound, with zero extra call-site arguments. *Test:* Catch2 DAO: condition-only query vs hand-bound baseline.

#### CORE: Case 5.2 — Condition-triggered alert primitive
A library-tier evaluation engine for user alerts (price cross, threshold), built on data_condition over any feed.
- **[CORE-027]** A library-tier `alert_trigger` — a generic condition edge-trigger primitive; all alert-domain semantics (price alerts, mute, sounds) live in APP (consumed by [APP-068]) — shall subscribe to any feed with a `data_condition` and fire a callback on edge transition (entity set goes from no-match to match), supporting one-shot and repeating modes. *Accept:* a price sequence crossing a threshold twice fires exactly once in one-shot mode and exactly twice in repeating mode. *Test:* Catch2: sorted feed + scripted crossing sequence, count fires per mode.
- **[CORE-028]** Alert evaluation shall run on the feed dispatch path with no polling thread, and disarm shall be RAII (dropping the trigger handle stops evaluation). *Accept:* after dropping the handle, subsequent matching pushes fire zero callbacks; no timer or dedicated thread exists in the implementation. *Test:* Catch2: drop handle, push matches, assert silence.

### 1.6 Secure & resilient transport (connect)
The websocket/HTTP layer must not silently trust (unverified TLS), silently die (terminal STALE), or silently stall (unmonitored heartbeat).

#### CORE: Case 6.1 — TLS certificate verification
Peer certificates are currently accepted blindly (`ssl::context` tlsv12_client with no verify mode) — unacceptable for a client that will sign orders.
- **[CORE-029]** `connect::context` shall enable `verify_peer` with the system default verify paths and RFC 2818 hostname verification against the SNI host. *Accept:* connecting to a server presenting a self-signed or wrong-hostname certificate fails before any payload exchange; a validly-chained host succeeds. *Test:* Catch2 vs in-process TLS mock: self-signed fails, test-CA-trusted passes.
- **[CORE-030]** TLS verification failure shall surface through `handle_error` as a distinguishable typed error, not a generic exception. *Accept:* the error handler receives an error identifying certificate verification as the cause (distinct code/type from DNS or HTTP errors). *Test:* Catch2 mock TLS server; assert error taxonomy.

#### CORE: Case 6.2 — Automatic reconnect after drop
The 250 ms retry loop only runs until the first open; any later error sets terminal STALE (src/connect/websocket.cpp) — the connection must instead self-heal.
- **[CORE-031]** `websock_connection` shall recover from an established-connection drop: read/write errors transition to a reconnecting state (not terminal STALE), the transport is rebuilt, and all three loops resume on the same object. *Accept:* against a mock server that drops the socket then restarts, message delivery resumes without recreating the `websock_connection`. *Test:* Catch2 vs in-process mock WS: drop, restart, assert resumed delivery.
- **[CORE-032]** Reconnect attempts shall use immediate first retry then exponential backoff with jitter (base 250 ms, factor 2, cap 30 s, ±20% jitter), resetting after a successful open. *Accept:* against an unreachable endpoint, recorded attempt intervals grow monotonically to the cap with first retry <500 ms; after one success the schedule restarts at base. *Test:* Catch2: unreachable endpoint, timestamp connect attempts, assert schedule.
- **[CORE-033]** Connection state transitions (connected / reconnecting / disconnected) shall be reported through a registered callback so upper layers can re-auth and resubscribe (orchestration itself is DATA_MODEL scope). *Accept:* a drop/restore cycle produces the ordered callback sequence connected→reconnecting→connected exactly once each. *Test:* Catch2 mock WS drop/restore; assert callback order.
- **[CORE-034]** Outbound messages enqueued while the connection is down shall fail fast through the error path with a distinguishable not-connected error — never silently dropped and never retained for post-reconnect flush (session-level re-drive of subscriptions is DATA_MODEL scope, [DATA_MODEL-030]). *Accept:* a send attempted mid-outage invokes the error handler with the not-connected error and the message is never delivered after reconnect; zero stale frames observed post-reconnect. *Test:* Catch2 mock WS: send during outage, assert error delivery and post-reconnect absence.

#### CORE: Case 6.3 — Heartbeat staleness detection
Heartbeat is currently send-only (`m_last_heartbeat` written, never read) so a half-open connection stalls market data silently.
- **[CORE-035]** A staleness watchdog shall track last inbound frame time and trigger the reconnect path when no inbound traffic arrives within the staleness timeout. *Accept:* a mock server that accepts pings but sends nothing triggers reconnect within timeout + one heartbeat interval. *Test:* Catch2 mock WS gone-silent; assert reconnect within bound.
- **[CORE-036]** Heartbeat interval and staleness timeout shall be configurable per connection through `set_heartbeat` (default timeout 2.5× interval). *Accept:* with interval 100 ms and timeout 300 ms, a silent server triggers reconnect within 500 ms wall clock. *Test:* Catch2 timed mock; assert detection latency.
- **[CORE-055]** `websock_connection` shall measure per-connection round-trip latency from ping->pong timing and expose the latest measurement alongside the CORE-033 state callback for upper-layer display (consumed by [APP-080]). *Accept:* against a mock server delaying pong by 50 ms, the reported latency is 50 ms ± 20 ms and updates on each heartbeat cycle. *Test:* Catch2 timed mock WS: scripted pong delays, assert reported values.

#### CORE: Case 6.4 — Error-path repairs
Fix the dead and dangerous error handling inside the websocket loops.
- **[CORE-037]** The dead `catch (boost::system::error_code&)` clauses shall be replaced with `boost::system::system_error` handling so network failures reach `handle_error` with the real underlying error code instead of "unknown error". *Accept:* an abrupt socket close yields an error-handler invocation carrying the beast/asio error code. *Test:* Catch2 mock WS abrupt close; inspect delivered error code.
- **[CORE-038]** `co_read` shall not dereference a null websocket stream when the weak_ptr lock fails on the first loop iteration. *Accept:* 100 create-then-immediately-destroy cycles run ASAN-clean with no null dereference. *Test:* Catch2 create/destroy race loop under ASAN.
- **[CORE-039]** The xscratcher error category shall provide distinct messages for all five enum values and fix the signed/unsigned bounds comparison. *Accept:* `message(e)` returns a unique, non-"unknown" string for every enumerator. *Test:* Catch2 unit iterating all codes.

#### CORE: Case 6.5 — DNS cache expiry
Resolved endpoints are currently pinned for process lifetime, breaking endpoint rotation/failover. (CORE-040, a full DNS-TTL cache-expiry subsystem with configurable expiry and a counting-resolver test harness, was cut per XP minimality as speculative for a single-venue client; CORE-041 below absorbs the real failover need via reconnect-path re-resolution. Retired, never reused.)
- **[CORE-041]** A connect failure against a cached endpoint shall invalidate that cache entry, and reconnect attempts after the first backoff step ([CORE-032]) shall re-resolve rather than reuse the cached endpoint. *Accept:* after a refused connection, the subsequent attempt triggers resolution again (resolver count increments); during backoff against an unreachable endpoint, resolution recurs from the second attempt on. *Test:* Catch2: refuse first endpoint, assert re-resolution on retry via counting resolver stub.

### 1.7 HTTP robustness & rate limiting
One-shot REST must handle real-world status codes and give upper layers a transport-level budget primitive for exchange rate limits.

#### CORE: Case 7.1 — Status handling, retries, deadlines
`http_query` currently throws on anything but exactly 200 and has no retry or per-request deadline.
- **[CORE-042]** All 2xx statuses shall be treated as success and delivered to the data handler (201/204 included, empty body allowed); 3xx/4xx/5xx keep their typed errors. *Accept:* a mock returning 204 with empty body invokes the data handler without throwing; 404 still raises `http_client_error`. *Test:* Catch2 vs in-process mock HTTP server per status.
- **[CORE-043]** An opt-in retry policy (max attempts + backoff) shall cover connect/5xx/timeout failures, default off so mutating exchange calls are never blind-retried. *Accept:* with retries=3, a mock failing twice then returning 200 succeeds; with retries=0 the first failure surfaces immediately. *Test:* Catch2 mock server with scripted failure sequence.
- **[CORE-044]** A per-request timeout override distinct from the shared context timeout shall be supported. *Accept:* a request with a 100 ms override fails within 100–300 ms against a stalling server while the context default remains 10 s. *Test:* Catch2 mock server delaying response; measure failure latency.

#### CORE: Case 7.2 — Transport rate-limiting primitive
A venue-agnostic token bucket (exchange header tracking and retCode handling are DATA_MODEL scope).
- **[CORE-045]** connect shall provide a token-bucket limiter (configurable capacity and refill rate) with an awaitable `acquire()` that suspends when the bucket is empty. *Accept:* 20 acquisitions through a 10-capacity/10-per-second bucket complete in ≥1.0 s and ≤1.5 s wall clock. *Test:* Catch2 timed unit, no network involved.
- **[CORE-046]** `http_query` shall be composable with a caller-shared limiter such that token acquisition completes before connection establishment. *Accept:* a burst of N requests through one limiter arrives at a mock server spaced at no more than the configured rate. *Test:* Catch2 mock server; assert request timestamp spacing.
- **[CORE-047]** The limiter shall support two priority lanes where high-priority acquisitions are granted before queued low-priority waiters (primitive only; order/cancel-preempts-polling policy is DATA_MODEL scope). *Accept:* with an empty bucket and queued low-priority waiters, a high-priority acquire is granted on the next refill before any of them. *Test:* Catch2 unit with controlled clock; assert grant order.

### 1.8 Structured logging
Replace raw `std::clog`/`std::cerr` scattered through connect and datahub with a leveled facility that never leaks secrets and never floods the hot path.

#### CORE: Case 8.1 — Levels, redaction, hot-path discipline
- **[CORE-048]** A minimal library-tier logging facility (levels error/warn/info/debug, runtime threshold, pluggable sink) shall replace direct `std::clog`/`std::cerr` use across src/connect and src/datahub. *Accept:* with threshold=warn, info/debug sites emit nothing; a repo grep finds no raw `std::clog`/`std::cerr` in src/connect or src/datahub. *Test:* Catch2 sink-capture unit + CI grep check.
- **[CORE-049]** Outbound payload logging shall redact secret material (API key, signature, auth fields) at every level, including debug. *Accept:* logging a WS auth message containing an api_key and signature yields output with no key/signature substring present. *Test:* Catch2 unit: log auth JSON through facility, assert secrets absent.
- **[CORE-050]** Per-message hot-path logging (`Try JSON` in `data_adapter`, `WebSocket write:` payload dumps) shall be removed or demoted to debug level. *Accept:* processing 1000 messages at default (info) level produces zero adapter/write log lines. *Test:* Catch2 sink capture: run pipeline, count lines at info level.
- **[CORE-054]** The logging facility shall provide a rotating file sink with a configurable size cap and archive count, applying CORE-049 redaction uniformly to every sink. *Accept:* writing 3x the cap produces at most the configured archive count plus the active file, each ≤ cap, and no rotated file contains a secret substring from a logged auth payload. *Test:* Catch2 temp-dir unit: overflow writes, assert file census and redaction.

### 1.9 Core scheduler lifecycle
The scheduler's worker threads capture the owning shared_ptr, so its destructor can never run — it must become stoppable and destructible.

#### CORE: Case 9.1 — Stoppable, non-immortal scheduler
- **[CORE-051]** Worker threads shall not capture an owning `shared_ptr` to the scheduler; dropping the last external reference shall run the destructor, reset the work guard, and join all threads. *Accept:* after releasing the last `shared_ptr`, a retained `weak_ptr` expires and thread count returns to baseline within 1 s. *Test:* Catch2 unit: create, drop, assert weak_ptr expiry and join.
- **[CORE-052]** An idempotent `stop()` shall be provided for explicit shutdown ordering (RAII destruction remains the primary path): it resets the guard, lets `io_context::run()` return, and joins. *Accept:* `stop()` returns with all threads joined; a second `stop()` and subsequent destruction are no-ops without deadlock. *Test:* Catch2 unit: stop twice then destroy under timeout guard.
- **[CORE-053]** Scheduler teardown with in-flight static weak_ptr coroutines (websocket loops, dispatcher drains) shall be leak- and UAF-free. *Accept:* destroying the scheduler while a live `websock_connection` and dispatcher are mid-operation runs ASAN-clean. *Test:* Catch2 integration under ASAN: active pipeline, destroy scheduler.
## 2. Exchange integration & domain data (DATA_MODEL)
This branch covers src/data — the trading-domain entities, the `IDataController` contract, and the ByBit V5 implementation (`ByBitDataManager`) that wires REST/WS wire formats through datahub pipelines into SQLite and live feeds. Today only public spot market data flows end-to-end; wallet/fee/position entities are unwired or missing, private persistence violates the README "never overwritten, always versioned" contract, order commands are fire-and-log with no typed results, and the session layer has no re-auth/resync, clock-sync, rate-limit, or credential-protection discipline. The target state below makes the exchange gateway complete for manual trading: full account/position/order data flow, a hardened session lifecycle, a typed order-command surface with a pre-trade risk gateway, REST bootstrap/backfill, and linear-category support — all built strictly on the datahub pipeline (dispatcher → adapter → sink → feed) and the connect transport per CONTRIBUTING.md.

### 2.1 Account data & positions
Wire the existing-but-dead wallet/fee entities and a new position flow into the datahub pipeline so account state is live, persisted, and subscribable.

#### DATA_MODEL: Case 1.1 — Wallet balance flow
Balances flow from REST bootstrap plus the private `wallet` WS topic into a subscribable feed (entities exist in src/data/bybit/entities/wallet.hpp but are wired to nothing).
- **[DATA_MODEL-001]** `IDataController` shall expose a wallet feed (keyed snapshot feed over `CoinBalance`, keyed by coin) and a `SubscribeWallet(weak_ptr<subscription>)` method with the established RAII semantics (drop shared_ptr = unsubscribe, snapshot on subscribe). *Accept:* a late subscriber receives a `update_kind::snapshot` containing all cached coins within one dispatch; dropping the shared_ptr stops further callbacks. *Test:* Catch2 unit: inject balances via sink, assert snapshot + increment dispatch and RAII unsubscribe.
- **[DATA_MODEL-002]** `ByBitDataManager` shall bootstrap balances via signed GET `/v5/account/wallet-balance?accountType=UNIFIED` at session sync and persist per-coin rows through a datahub `data_sink`. *Accept:* against a mock REST fixture with 3 coins, the wallet feed cache and the SQLite table each contain exactly 3 rows with equity/available populated. *Test:* Catch2 vs in-process mock HTTP server: serve fixture, assert feed + DAO contents.
- **[DATA_MODEL-003]** The private stream shall subscribe the `wallet` topic after auth and route its payloads through the same wallet sink. *Accept:* a mocked WS wallet frame updates the coin's equity in the feed within one dispatch cycle and appends a persisted row. *Test:* Catch2 vs mock WS: push wallet frame, assert feed update.

#### DATA_MODEL: Case 1.2 — Fee rates
Runtime fetch of maker/taker fee rates (entity exists only as a DAO test fixture today).
- **[DATA_MODEL-004]** `ByBitDataManager` shall fetch `/v5/account/fee-rate` per configured category at session sync, persist `FeeRate` rows keyed by symbol, and expose them via an `IDataController` accessor or feed. *Accept:* after a mocked fee-rate response, maker and taker rates for a named symbol are queryable and match the fixture strings exactly. *Test:* Catch2 vs mock HTTP: fixture round-trip through sink to accessor.
- **[DATA_MODEL-005]** `FeeRate` fractional fields shall use signed `currency<int64_t>` so maker rebates (negative maker rate) parse and persist correctly. *Accept:* wire value `"-0.0001"` round-trips parse→persist→query with sign preserved. *Test:* Catch2 unit: Glaze parse + DAO round-trip of negative rate.

#### DATA_MODEL: Case 1.3 — Positions data flow
Position entity and live flow (no Position entity exists despite README listing position history as a private-data category).
- **[DATA_MODEL-006]** A `bybit::Position` entity shall mirror the V5 position schema (symbol, side, size, avgPrice, positionValue, leverage, tradeMode, markPrice, liqPrice, positionIM, positionMM, unrealisedPnl, cumRealisedPnl, positionIdx, positionStatus, updatedTime as time_point) using Glaze auto-reflection and `currency` fields. *Accept:* both a `/v5/position/list` fixture and a WS `position` topic fixture deserialize with every listed field populated and unknown keys tolerated. *Test:* Catch2 unit: parse both official-shape fixtures.
- **[DATA_MODEL-007]** Positions shall bootstrap via signed GET `/v5/position/list` per derivatives category at session sync and stay live via the private `position` WS topic, both routed through one datahub sink into a feed keyed by (symbol, positionIdx). *Accept:* after mock bootstrap (1 position) plus one WS delta changing size, the feed snapshot shows the updated size and SQLite holds both versions. *Test:* Catch2 vs mock HTTP+WS scenario.
- **[DATA_MODEL-008]** `IDataController` shall expose `SubscribePositions(weak_ptr<subscription>)` with snapshot-on-subscribe and RAII unsubscribe identical to the other feeds. *Accept:* late subscriber gets full position snapshot; expired weak_ptr is pruned on next push without error. *Test:* Catch2 unit on feed wiring.

#### DATA_MODEL: Case 1.4 — Leverage & margin commands
Guarded margin-configuration commands per position/account endpoints.
- **[DATA_MODEL-009]** `SetLeverage(symbol, buyLeverage, sellLeverage)` shall POST signed `/v5/position/set-leverage` and complete with a typed result (success or `{retCode, retMsg}` error). *Accept:* mocked retCode 0 invokes success callback; mocked retCode 110043 delivers a typed error carrying that code. *Test:* Catch2 vs mock HTTP: both fixtures.
- **[DATA_MODEL-010]** A margin-mode switch command (cross/isolated via `/v5/position/switch-isolated` or account margin mode) shall complete with a typed result and be rejected locally while working orders or positions exist for the symbol. *Accept:* switch attempt with a cached open position returns a local typed reject without any HTTP request. *Test:* Catch2 unit: gateway check + mock HTTP capture (zero requests).
- **[DATA_MODEL-084]** (defer) A position-mode switch command (one-way vs hedge via `/v5/position/switch-mode`) shall complete with a typed result and be rejected locally while working orders or positions exist for the symbol. *Accept:* a switch attempt with a cached open position returns a typed local reject with zero HTTP requests. *Test:* Catch2 unit + mock HTTP capture.
- **[DATA_MODEL-085]** (defer) An add/reduce isolated-margin command (`/v5/position/add-margin`) shall complete with a typed result, gateway-validated against the position's tradeMode. *Accept:* add-margin on a cross-mode position is rejected locally; on an isolated position it produces exactly one signed request. *Test:* Catch2 unit + mock HTTP fixture.
- **[DATA_MODEL-086]** (defer) Risk-limit tier data (`/v5/market/risk-limit`) shall be fetched per derivatives symbol and a tier-selection command exposed, with each tier's max position and MM rate available for preview (view is TRADER_HUD's, [TRADER_HUD-086]). *Accept:* fixture tiers round-trip to an accessor; a selection command posts the chosen tier id. *Test:* Catch2 vs mock HTTP fixture.

#### DATA_MODEL: Case 1.5 — Transfers & asset operations (defer)
Inter-account fund movement and asset views, consciously deferred from the trading gateway's basic set.
- **[DATA_MODEL-089]** (defer) Internal transfers between funding and unified accounts (`/v5/asset/transfer/inter-transfer`), transfer status/history queries, and view-only deposit/withdrawal status lists shall be exposed as typed commands/accessors; withdrawal initiation stays out of terminal scope; the transfer confirmation dialog is exempt from one-click mode (APP side when un-deferred). *Accept:* a transfer fixture round-trips with status tracking; deposit list renders read-only data. *Test:* Catch2 vs mock HTTP fixtures.

#### DATA_MODEL: Case 1.6 — Funding-account & spot-margin balance views (defer)
Two further trading.json balance-view cases beyond the UNIFIED-only wallet flow (DATA_MODEL-001–003), consciously deferred as a maturity-stage view, not a first-milestone need for a manual single-account terminal.
- **[DATA_MODEL-090]** (defer) A funding-account balance view (`/v5/asset/transfer/query-account-coins-balance` for `accountType=FUND`) shall expose funding-account per-coin balances as a second feed alongside the UNIFIED wallet feed (DATA_MODEL-001), letting a consumer present trading vs funding balances distinctly. *Accept:* a mocked FUND-type fixture populates a feed whose coin set is queryable independently of the UNIFIED wallet feed's cache. *Test:* Catch2 vs mock HTTP fixture: assert two independent feed caches.
- **[DATA_MODEL-091]** (defer) Spot-margin account state (collateral, liabilities, borrow rate per coin) shall be fetched from the spot-margin account endpoints and exposed via an `IDataController` accessor when spot-margin trading is enabled on the account. *Accept:* a fixture response with two coins round-trips collateral/liability/borrow-rate values unchanged through to the accessor. *Test:* Catch2 vs mock HTTP fixture.

### 2.2 Instrument reference & market categories
Remove the hardcoded-spot assumption and complete instrument/derivative reference data needed by risk checks and order sizing.

#### DATA_MODEL: Case 2.1 — Linear category support (defect)
Category is baked into instrument URL, public stream path, and CancelOrder body (`data_manager.cpp` constants and `CancelOrderRequest{.category = "spot"}`).
- **[DATA_MODEL-011]** Instrument discovery shall query `/v5/market/instruments-info` once per configured category (at minimum `spot` and `linear`) and persist instruments tagged with their `Category`. *Accept:* with categories {spot, linear} configured, both REST queries are issued and the instrument feed contains rows of both categories with correct tags. *Test:* Catch2 vs mock HTTP: two fixtures, assert per-category rows.
- **[DATA_MODEL-012]** Public WS streams shall be opened per category endpoint (`/v5/public/spot`, `/v5/public/linear`) and topic subscriptions routed to the stream matching the instrument's category. *Accept:* subscribing a linear symbol sends its `publicTrade`/`orderbook` subscribe frames on the linear stream only, and inbound frames route to that symbol's sinks. *Test:* Catch2 vs two mock WS endpoints: assert subscribe frame routing.
- **[DATA_MODEL-013]** All order/position commands shall derive `category` from the target instrument's cached `InstrumentInfo` — no hardcoded category strings. *Accept:* `CancelOrder` for a linear symbol sends `"category":"linear"` in the request body. *Test:* Catch2 vs mock HTTP: capture and assert request body.

#### DATA_MODEL: Case 2.2 — Derivative instrument info & tickers
Extend reference data with leverage/funding fields and a mark/index price source (needed by the risk price band and position views).
- **[DATA_MODEL-014]** `InstrumentInfoAPI`/`InstrumentInfo` shall carry the linear-category fields: leverageFilter (minLeverage, maxLeverage, leverageStep), fundingInterval, contractType, settleCoin. *Accept:* a linear instruments-info fixture round-trips these fields into the flat `InstrumentInfo` and its SQLite row. *Test:* Catch2 unit: parse fixture + DAO round-trip.
- **[DATA_MODEL-015]** Subscribing a derivatives instrument shall additionally subscribe `tickers.<symbol>` and expose markPrice, indexPrice, fundingRate, and nextFundingTime through a datahub feed/accessor. *Accept:* a mocked ticker delta updates markPrice retrievable by callers within one dispatch cycle. *Test:* Catch2 vs mock WS: push ticker frames, assert feed values.
- **[DATA_MODEL-073]** `IDataController` shall expose a per-symbol ticker feed (lastPrice, price24hPcnt, volume24h, turnover24h) sourced from the `tickers.<symbol>` topic on the symbol's category stream — spot included — subscribable per symbol with the established RAII semantics (consumed by [TRADER_HUD-036]/[TRADER_HUD-038]). *Accept:* a mocked spot ticker frame updates the symbol's cached values within one dispatch; dropping the subscription shared_ptr stops further callbacks. *Test:* Catch2 vs mock WS: push ticker frames, assert feed values and RAII unsubscribe.

### 2.3 Private data lifecycle & versioned persistence
Honor the src/data/README.md private-data contract — every private update creates a new versioned record — and fix private stream fidelity defects.

#### DATA_MODEL: Case 3.1 — Versioned append-only persistence (defect)
Orders/Trades currently go through `insert_or_replace` keyed by orderId/execId, so updates overwrite history (README contract violated).
- **[DATA_MODEL-016]** Private entities Order, Trade, and Position shall be persisted append-only: each accepted update inserts a new row under a composite key including a monotonically increasing version (updatedTime + local sequence tie-breaker); `insert_or_replace` is forbidden on these tables. WalletBalance shall persist latest-state per coin (insert_or_replace) and is excluded from the versioning contract; src/data/README.md is updated in the same change to name the covered entities. *Accept:* two WS order updates for one orderId produce two distinct rows with both payloads retained; two wallet ticks for one coin leave exactly one row holding the latest values. *Test:* Catch2 vs temp SQLite: push updates per entity kind, assert row counts.
- **[DATA_MODEL-017]** A latest-state query helper shall return, for each business key (orderId/execId/symbol+positionIdx/coin), exactly the highest-version row. *Accept:* over a 3-version fixture for two orderIds the helper returns 2 rows, each the newest version. *Test:* Catch2 DAO unit with multi-version fixture.
- **[DATA_MODEL-018]** Live feeds shall continue to expose latest-state-per-key (deduped by business key) while persistence retains full history. *Accept:* after replaying 3 updates of one order, a subscriber snapshot contains a single entry with the final `orderStatus` while SQLite holds 3 rows. *Test:* Catch2 unit: feed snapshot vs DAO row count.

#### DATA_MODEL: Case 3.2 — Private stream fidelity (defect)
Fix string-timestamp sort keys and pin wire-format fidelity for order/execution topics.
- **[DATA_MODEL-019]** `Order::createdTime/updatedTime` and `Trade::execTime` shall be converted from `std::string` to the project `time_point` ms codec (as already done for `PublicTrade::time`), so feed ordering is chronological, not lexicographic. *Accept:* entities with timestamps 999999999999 and 1000000000000 sort chronologically in the feed and persist as INTEGER ms. *Test:* Catch2 unit: parse fixtures, assert feed order + DAO column type.
- **[DATA_MODEL-020]** WS `execution` payloads shall deserialize with fee, feeCurrency, isMaker, and execType populated and flow through the versioned trade sink. *Accept:* an official-shape execution frame yields a persisted Trade row with all four fields matching the fixture. *Test:* Catch2 vs mock WS: push frame, assert row.
- **[DATA_MODEL-021]** WS `order` topic payloads shall deserialize the full `bybit::Order` entity from an official-shape fixture with unknown keys tolerated and `""` currency fields mapped to zero. *Accept:* fixture parse succeeds with orderStatus, avgPrice, cumExecQty, and rejectReason populated as in the fixture. *Test:* Catch2 unit: Glaze parse of captured topic payload.

### 2.4 Auth profiles & credential storage
Named exchange session profiles with encrypted credentials and permission-aware capability gating.

#### DATA_MODEL: Case 4.1 — Connection profiles
Multiple named endpoint/credential profiles replace the single flat host/key config.
- **[DATA_MODEL-022]** Configuration shall support named connection profiles {name, REST host, WS host, credential reference, environment: mainnet|testnet}, with the active profile selected via config/CLI. *Accept:* switching the active profile name changes all constructed REST/WS URLs to that profile's hosts without code change. *Test:* Catch2 unit: config parse + manager URL construction for two profiles.
- **[DATA_MODEL-023]** `IDataController` shall expose the active profile's environment so upper layers can render testnet/mainnet distinction. *Accept:* accessor returns `testnet` for a testnet profile and is stable for the manager's lifetime. *Test:* Catch2 unit.

#### DATA_MODEL: Case 4.2 — Secure credential storage (defect)
API secrets currently live in ad hoc plaintext (key.txt in the working tree, CLI/config options) with no formal backend abstraction. Three `credential_backend` variants replace it, phased from MVP through production hardening; the app must support each independently rather than one either/or leaf.
- **[DATA_MODEL-024]** In the MVP/dev profile, API key/secret shall be persisted via `credential_backend: plaintext` — a single unencrypted credential file replacing the ad hoc key.txt/CLI/config paths — with a one-time startup warning naming the file as unprotected. *Accept:* a stored credential round-trips unencrypted through the file; a fresh dev-profile startup emits exactly one plaintext-storage warning. *Test:* Catch2 unit: store/load round-trip + warning-emission assertion.
- **[DATA_MODEL-092]** In the production profile, API key/secret shall be persisted via `credential_backend: encrypted_file` (passphrase-encrypted credential file) by default, superseding plaintext; no file under the data/config directories shall contain the secret substring. *Accept:* after storing a credential under a production profile, a recursive plaintext scan of the data dir finds no secret substring; the secret round-trips only through the passphrase-unlock path. *Test:* Catch2 unit: store/load round-trip + recursive plaintext scan of the data dir.
- **[DATA_MODEL-093]** (defer) `credential_backend: os_keyring` shall offer the platform OS keyring (Secret Service) as an opt-in alternative to the encrypted file where available. *Accept:* not applicable until scheduled; leaf registers the conscious deferral for the coverage map. *Test:* none (defer).
- **[DATA_MODEL-025]** No DATA_MODEL-owned log statement shall emit key, secret, signature, or auth payload material; signing helpers shall log at most a redacted key prefix. *Accept:* captured log output of a full auth + PlaceOrder cycle contains neither the API key, secret, nor any computed signature. *Test:* Catch2 vs mock server with clog/cerr capture and substring assertions.

#### DATA_MODEL: Case 4.3 — Key permission introspection
Capability discovery on connect drives read-only gating and expiry warnings.
- **[DATA_MODEL-026]** At session start the manager shall query signed `/v5/user/query-api` and derive capability flags (read-only vs trade-enabled) that feed the risk gateway's read-only mode. *Accept:* a mocked response lacking trade permission puts the command gateway into read-only (DATA_MODEL-064) automatically. *Test:* Catch2 vs mock HTTP fixture, assert gateway state.
- **[DATA_MODEL-027]** Key expiry (`expiredAt`) shall be checked on connect; expiry within a configurable warning window emits a session warning event. *Accept:* fixture expiring in 5 days with a 7-day window emits exactly one warning event carrying the expiry date. *Test:* Catch2 unit on introspection handler with clock injection.

### 2.5 Session resync & stream lifecycle
Session-level recovery discipline: correct auth handshake, full resubscribe/re-bootstrap after transport reconnect, and observable session state (transport-level reconnect itself is the CORE branch's contract).

#### DATA_MODEL: Case 5.1 — Private auth handshake (defect)
Auth, subscribe frames are currently fired blind with acks only logged.
- **[DATA_MODEL-028]** Private topic subscribe frames shall be sent only after a successful auth ack (`op=auth`, success=true) is received and matched. *Accept:* with a mock server delaying the auth ack, no subscribe frame is observed before the ack; after it, all private subscribes follow. *Test:* Catch2 vs scripted mock WS: frame-order assertion.
- **[DATA_MODEL-029]** Auth failure shall surface a typed session error state and stop after a bounded retry count instead of silently proceeding. *Accept:* an auth-fail fixture yields session state `auth_failed` after N (configurable, default 3) attempts and zero subscribe frames. *Test:* Catch2 vs mock WS returning auth failure.

#### DATA_MODEL: Case 5.2 — Reconnect orchestration
On transport-reconnected notification (provided by connect/CORE), the manager restores the full session.
- **[DATA_MODEL-030]** Upon a transport reconnect notification (consumes [CORE-033]) the manager shall re-authenticate the private stream and resubscribe every previously active topic (all per-symbol public topics from `symbol_streams` plus private topics). *Accept:* mock drop+reaccept: the server receives auth then a subscribe set identical to the pre-drop set (order-insensitive comparison). *Test:* Catch2 vs restartable mock WS: compare topic sets.
- **[DATA_MODEL-031]** After private reconnect the manager shall REST-resync open orders (`/v5/order/realtime`), positions, and wallet through the versioned sinks before the session state becomes `synced`; updates missed during the outage shall appear in feeds. *Accept:* scripted scenario where an order fills during the outage: after resync the order feed shows `Filled` and session state transitions degraded→authenticating→syncing→synced ([DATA_MODEL-034] vocabulary) are observed in order. *Test:* Catch2 vs mock HTTP+WS scenario with session-state subscriber.

#### DATA_MODEL: Case 5.3 — Orderbook gap detection (defect)
No `u`/`seq` continuity check exists; a missed delta silently corrupts the book.
- **[DATA_MODEL-032]** The orderbook stream shall run through the datahub gap-detecting stage parameterized with the topic's per-symbol update-id extractor (consumes [CORE-013]/[CORE-014]); on a detected gap the manager shall discard the book, force a fresh snapshot via unsubscribe+resubscribe of that topic, and reset the gap baseline ([CORE-014]). *Accept:* a scripted payload sequence skipping one update id triggers exactly one resubscribe and the resulting book equals the post-snapshot fixture state. *Test:* Catch2 unit: scripted payloads through the wired stage, assert frames + book.
- **[DATA_MODEL-033]** Each gap reported by the CORE stage (consumes [CORE-013]) shall emit a session event carrying topic, expected and received update ids. *Accept:* the gap scenario above yields exactly one event with both ids populated. *Test:* Catch2 unit: event subscriber assertion.

#### DATA_MODEL: Case 5.4 — Session state surface
One observable session state machine gates trading and feeds UI indicators (rendering is APP's).
- **[DATA_MODEL-034]** Session state (connecting, authenticating, syncing, synced, degraded, auth_failed) shall be exposed through a datahub feed on `IDataController` so any consumer can subscribe with snapshot-on-subscribe semantics. *Accept:* a late subscriber immediately receives the current state; all transitions in DATA_MODEL-028..DATA_MODEL-031 scenarios are delivered in order. *Test:* Catch2: state-sequence assertions inside the mock scenarios.
- **[DATA_MODEL-035]** Mutating commands (place/amend/cancel, leverage) shall be rejected locally with a typed `session_not_synced` error while the session is not `synced`; the kill-switch path ([DATA_MODEL-066]) is exempt. *Accept:* `PlaceOrder` during `syncing` produces a typed local reject and zero HTTP requests; `PanicFlatten` still reaches the mock server. *Test:* Catch2 vs mock HTTP: request-count assertions.

#### DATA_MODEL: Case 5.5 — Per-symbol topic teardown
Reference-counted unsubscribe of public topics when the last consumer releases a symbol (documented codebase gap).
- **[DATA_MODEL-074]** When the last registered interest in a symbol is released, the manager shall send WS unsubscribe frames for all of that symbol's public topics on the matching category stream and erase its `symbol_streams` entry, without reconnecting the stream. *Accept:* subscribe then release: the mock server receives unsubscribe frames for exactly that symbol's topics, later frames for it are not dispatched, and other symbols' flows are untouched. *Test:* Catch2 vs mock WS: frame capture + post-teardown dispatch assertion.
- **[DATA_MODEL-075]** Topic subscriptions shall be reference-counted per symbol+topic so overlapping consumer interests produce one subscribe frame and teardown fires only when the count reaches zero (cockpit-side interest counting is [TRADER_HUD-064]/[TRADER_HUD-065]). *Accept:* two subscriptions then one release produce zero unsubscribe frames; the second release produces exactly one. *Test:* Catch2 unit vs mock WS frame counts.

#### DATA_MODEL: Case 5.6 — Manager thread confinement (defect)
Strand-confine ByBitDataManager state against the documented UI-thread vs data-thread race on SubscribeInstrument.
- **[DATA_MODEL-076]** All mutable `ByBitDataManager` state (`m_pubdata_accept`, `symbol_streams`, SQLite handles) shall be strand-confined: `IDataController` entry points callable from the UI thread shall marshal onto the manager strand instead of touching state directly. *Accept:* 4 threads churning SubscribeInstrument/release against live mock dispatch (≥10 000 ops) complete TSan-clean, offline via the INFRA mock harness (consumes [INFRA-013]/[INFRA-026]). *Test:* Catch2 TSan stress regression under the tsan preset.

### 2.6 Clock sync & rate limiting
Time discipline for signed requests and client-side budgeting against ByBit per-UID/IP limits.

#### DATA_MODEL: Case 6.1 — Server clock sync
Signed requests shall tolerate local clock drift within recv_window.
- **[DATA_MODEL-036]** The manager shall fetch server time (`/v5/market/time`) at session start and periodically, estimate the offset as server_time − (send+recv)/2, and apply it to `X-BAPI-TIMESTAMP` and WS auth `expires`. *Accept:* with the mock server clock skewed +3 s, signed request timestamps land within ±500 ms of the mock server clock. *Test:* Catch2 vs mock HTTP validating received timestamps.
- **[DATA_MODEL-037]** `recv_window` shall be configurable (default 5000 ms) and measured drift exceeding recv_window/2 shall emit a session warning event. *Accept:* a simulated 3 s drift with recv_window 5000 emits exactly one warning event carrying the drift value. *Test:* Catch2 unit on offset estimator with injected clocks.

#### DATA_MODEL: Case 6.2 — Rate-limit budgeting
Client-side throttle across REST endpoints with priority for order actions.
- **[DATA_MODEL-038]** All signed REST calls shall pass through the shared connect token-bucket limiter (consumes [CORE-045]/[CORE-046]) with one bucket per endpoint group, seeded from config and refreshed from `X-Bapi-Limit`/`X-Bapi-Limit-Status`/`X-Bapi-Limit-Reset-Timestamp` response headers. *Accept:* after a header reporting a reduced remaining budget the group's bucket reflects it, and a burst of 20 calls against a 10/s budget dispatches at most 10 in any 1 s window (virtual clock). *Test:* Catch2 unit with injected clock and header fixtures.
- **[DATA_MODEL-039]** Order-mutation commands shall be assigned the limiter's high-priority lane (consumes [CORE-047]); polling, backfill, and history fetches use the low-priority lane. *Accept:* with a saturated low-priority queue, an interleaved `PlaceOrder` dispatches before all still-pending polls. *Test:* Catch2 unit: queue-order assertion through the CORE lane primitive.
- **[DATA_MODEL-040]** retCode 10006 or HTTP 403 shall trigger backoff for the affected group until the reset timestamp and emit a rate-limit session event. *Accept:* after a mocked 10006, no request in that group is sent before the reset timestamp and one event is emitted. *Test:* Catch2 vs mock HTTP with virtual clock.
- **[DATA_MODEL-087]** A process-wide IP-level budget shall cap aggregate dispatch across all endpoint groups at 600 REST requests per 5 s and 500 WS connection attempts per 5 min, layered on top of the per-endpoint-group buckets (consumes [CORE-045]). *Accept:* a burst of 700 REST calls dispatches at most 600 in any 5 s window (virtual clock); the 501st WS connect attempt within 5 min is delayed to the window boundary. *Test:* Catch2 unit with virtual clock.

### 2.7 Order command surface
A complete, typed order-command API on `IDataController` covering the ByBit V5 order matrix (UI composition is APP's; this is the gateway).

#### DATA_MODEL: Case 7.1 — Typed command results (defect)
PlaceOrder failures go to stderr and CancelOrder only logs the response.
- **[DATA_MODEL-041]** `PlaceOrder`/`AmendOrder`/`CancelOrder` shall complete via a typed result callback (`std::expected<ResultT, OrderError{retCode, retMsg, kind}>` or equivalent) — no log-only outcomes; every exchange reject reaches the caller. *Accept:* mocked retCode 170131 delivers an `OrderError` with that code and the exchange retMsg to the caller. *Test:* Catch2 vs mock HTTP: success and reject fixtures.
- **[DATA_MODEL-042]** Transport failures (timeout, 5xx, connection error) shall map into the same result taxonomy with `kind=transport`, distinguishable from exchange rejects. *Accept:* a mocked connection drop mid-request yields `kind=transport` with no retCode, not an exchange reject. *Test:* Catch2 vs mock HTTP inducing failure.

#### DATA_MODEL: Case 7.2 — Order-type matrix
Full v5/order/create parameter coverage for the supported order types.
- **[DATA_MODEL-043]** `OrderRequest` shall support conditional orders: triggerPrice, triggerDirection (1 rise / 2 fall), and triggerBy (LastPrice/MarkPrice/IndexPrice) serialized per V5 docs. *Accept:* a stop-market fixture serializes to exactly the documented field set (no extras, correct names/values). *Test:* Catch2 unit: Glaze serialization vs golden JSON.
- **[DATA_MODEL-044]** Time-in-force GTC/IOC/FOK/PostOnly and the reduceOnly/closeOnTrigger flags shall serialize correctly; locally invalid combinations (e.g. Market+PostOnly) are rejected by the gateway before submit. *Accept:* each TIF value round-trips to its wire string; Market+PostOnly returns a typed local reject with zero HTTP requests. *Test:* Catch2 unit: serialization table + gateway check.
- **[DATA_MODEL-045]** TP/SL attached at entry (takeProfit, stopLoss, tpLimitPrice, slLimitPrice, tpslMode, tpTriggerBy/slTriggerBy) shall serialize per V5 docs. *Accept:* a TP/SL-at-entry fixture matches the documented body field-for-field. *Test:* Catch2 unit: golden JSON comparison.

#### DATA_MODEL: Case 7.3 — Amend & cancel-all
Lifecycle management beyond single cancel.
- **[DATA_MODEL-046]** `AmendOrder` shall POST signed `/v5/order/amend` supporting price, qty, triggerPrice, and TP/SL changes, completing with a typed result. *Accept:* mock capture shows the amend body with orderId plus only the changed fields; retCode 0 surfaces the orderId to the caller. *Test:* Catch2 vs mock HTTP: body capture + result assertion.
- **[DATA_MODEL-047]** `CancelAllOrders(scope)` shall POST signed `/v5/order/cancel-all` with scopes per-symbol, per-category (per-settleCoin and per-side deferred), returning the list of cancelled orderIds. *Accept:* mocked response with 3 ids invokes the callback with exactly those 3 ids. *Test:* Catch2 vs mock HTTP fixture.

#### DATA_MODEL: Case 7.4 — Batch operations (defer)
Grouped place/amend/cancel per V5 batch endpoints.
- **[DATA_MODEL-048]** (defer) Batch place/amend/cancel commands shall use `/v5/order/create-batch`, `/v5/order/amend-batch`, `/v5/order/cancel-batch` with up to the per-category documented item caps. *Accept:* a 3-item batch produces one HTTP request containing all 3 items. *Test:* Catch2 vs mock HTTP: request-count + body assertion.
- **[DATA_MODEL-049]** (defer) Batch results shall surface per-item outcomes (each item's retCode/orderId) so one failed item does not mask the others. *Accept:* mocked mixed response (2 ok, 1 reject) delivers 3 per-item results with the reject's code attached to the right orderLinkId. *Test:* Catch2 vs mock HTTP fixture.

#### DATA_MODEL: Case 7.5 — Trailing stop & position TP/SL (defer)
Position-scoped trading-stop commands for derivatives.
- **[DATA_MODEL-050]** (defer) A `SetTradingStop` command shall POST signed `/v5/position/trading-stop` (takeProfit/stopLoss/trailingStop/activePrice) with a typed result. *Accept:* mocked retCode 0 surfaces success; body carries symbol, category, positionIdx and only the provided fields. *Test:* Catch2 vs mock HTTP body capture.
- **[DATA_MODEL-051]** (defer) Trailing parameters shall be gateway-validated: trailingStop distance > 0 and tick-aligned, activePrice tick-aligned. *Accept:* a non-tick-aligned trailing distance returns a typed local reject with zero HTTP requests. *Test:* Catch2 unit with InstrumentInfo fixture.

#### DATA_MODEL: Case 7.6 — orderLinkId & idempotency
Client order ids for correlation and duplicate protection.
- **[DATA_MODEL-052]** Every `PlaceOrder` shall carry an orderLinkId — auto-generated (UUID-v4-strength uniqueness) when the caller supplies none — and the typed result shall include it for correlation with the private order topic. *Accept:* 1000 generated ids are pairwise distinct; the submitted body contains the id returned to the caller. *Test:* Catch2 unit: uniqueness + body capture.
- **[DATA_MODEL-053]** A resubmission carrying an orderLinkId already in flight shall be rejected locally until the first attempt completes. *Accept:* second submit with the same id while the first is pending returns a typed `duplicate_submit` reject and produces no HTTP request. *Test:* Catch2 vs mock HTTP with delayed response.

### 2.8 REST bootstrap & historical backfill
Feeds start from persisted+fetched history instead of empty caches (the `API_RECENT_TRADE` constant is currently dead; no kline or order/execution history fetch exists).

#### DATA_MODEL: Case 8.1 — Recent public trades backfill
Seed the trade feed so charts are not blank until the first live print.
- **[DATA_MODEL-054]** `SubscribeInstrument` shall fetch `/v5/market/recent-trade` for the symbol and route the result through the existing public-trade sink, completing the backfill into the cache before live WS trade frames for that symbol are dispatched. *Accept:* against a mock serving 60 REST trades, the feed cache holds 60 sorted trades before any WS frame arrives. *Test:* Catch2 vs mock HTTP+WS: subscribe, assert cache.
- **[DATA_MODEL-055]** REST backfill and live WS trades shall merge without duplicates (dedup by execId) and preserve time ordering. *Accept:* overlapping REST and WS fixtures (10 shared execIds) yield a feed containing the union with each execId exactly once, time-sorted. *Test:* Catch2 vs mock scenario: overlap fixture.

#### DATA_MODEL: Case 8.2 — Kline history
Historical candles for chart backfill (chart consumption is TRADER_HUD's).
- **[DATA_MODEL-056]** A `bybit::Kline` entity and a paginated `/v5/market/kline` fetch (symbol, interval, cursor over start/end) shall persist candles per symbol+interval and expose them via an `IDataController` accessor or feed. *Accept:* a mocked 2-page fetch (1000+500 rows) persists 1500 unique, chronologically ordered candles. *Test:* Catch2 vs mock HTTP paging fixture.
- **[DATA_MODEL-057]** After a public-stream reconnect, kline and trade history shall be re-fetched from the last persisted timestamp to now so the persisted series has no hole. *Accept:* a simulated 5-minute outage results in a fetch whose start equals the last persisted candle time and a gap-free stored series. *Test:* Catch2 vs mock scenario with virtual clock.

#### DATA_MODEL: Case 8.3 — Order & execution history backfill
Private history acquisition feeding the versioned store (viewing is TRADER_HUD's).
- **[DATA_MODEL-058]** At session sync the manager shall fetch `/v5/order/history` and `/v5/execution/list` for a configurable lookback and route rows through the versioned private sinks, deduplicating against streamed rows. *Accept:* an overlap fixture (same execId via WS and REST) yields exactly one feed entry and no duplicate version row. *Test:* Catch2 vs mock HTTP+WS overlap scenario.
- **[DATA_MODEL-059]** History fetches shall follow the V5 `nextPageCursor` pagination until the lookback window is covered. *Accept:* a 3-page mocked cursor chain issues exactly 3 requests and persists all pages' rows. *Test:* Catch2 vs mock HTTP cursor fixture.

### 2.9 Pre-trade risk gateway
A mandatory client-side check pipeline in front of every mutating command — the enforcement point for limits, read-only mode, and the kill switch (configuration/UI live in APP).

#### DATA_MODEL: Case 9.1 — Instrument-rule validation
Orders shall conform to exchange instrument filters before leaving the client.
- **[DATA_MODEL-060]** The gateway shall validate every `OrderRequest` against the instrument's cached filters — price tick alignment, qty step alignment, min/max order qty — and reject violations locally with a typed error naming the violated rule. *Accept:* qty below `minOrderQty` returns reject `qty_below_min` carrying the limit value, with zero HTTP requests. *Test:* Catch2 unit: violation table over InstrumentInfo fixtures.
- **[DATA_MODEL-061]** Minimum order notional (`minOrderAmt`/minNotionalValue) shall be validated using exact `currency` arithmetic (price × qty), never floating point. *Accept:* an order 1 tick below the notional floor is rejected; at the floor it passes. *Test:* Catch2 unit: boundary fixtures.

#### DATA_MODEL: Case 9.2 — Configured limits & price band
User-configured hard limits stricter than exchange caps.
- **[DATA_MODEL-062]** Per-instrument configured caps — max order qty, max order notional, max total position size — shall be enforced pre-submit with typed rejects carrying the configured cap. *Accept:* an order whose notional exceeds the configured cap is rejected locally with the cap value in the error; one at the cap passes. *Test:* Catch2 unit: boundary tests around each cap.
- **[DATA_MODEL-063]** A fat-finger price band shall reject limit/conditional prices deviating more than a configured percentage from the reference price (markPrice for derivatives, last trade for spot). *Accept:* with a 5% band, a limit 6% away is rejected with the computed deviation; 4% away passes. *Test:* Catch2 unit with injected reference prices.
- **[DATA_MODEL-081]** A rapid-fire throttle shall reject order submissions exceeding a configured maximum count per rolling interval with a typed `rapid_fire_limit` reject carrying the limit and window. *Accept:* with limit 5 per 10 s, the 6th submit inside the window is rejected locally with zero HTTP requests; after the window slides it passes. *Test:* Catch2 unit with injected clock.
- **[DATA_MODEL-082]** A daily realized-loss limit shall lock new-entry orders once the session's realized PnL (from the executions feed) breaches the configured loss cap — reduce-only orders remain allowed — until an explicit unlock through `IDataController`; the counter resets at UTC day rollover. *Accept:* after a fixture loss beyond the cap, a non-reduce-only order is rejected `daily_loss_limit` while a reduce-only order passes; unlock restores submission. *Test:* Catch2 unit with executions fixture and injected clock.
- **[DATA_MODEL-083]** A configured per-instrument max-leverage cap shall bound `SetLeverage` below the exchange `leverageFilter` maximum. *Accept:* a SetLeverage above the configured cap returns a typed local reject carrying the cap with zero HTTP requests; at the cap it passes. *Test:* Catch2 unit boundary fixtures.

#### DATA_MODEL: Case 9.3 — Read-only enforcement
Non-mutating operation modes enforced at the gateway (toggles/banners are APP's).
- **[DATA_MODEL-064]** When the API key lacks trade permission (consumes [DATA_MODEL-026]) the gateway shall block every mutating command with a typed `read_only` reject while all data flows continue. *Accept:* in read-only, `PlaceOrder`/`AmendOrder`/`CancelAllOrders` produce zero HTTP requests and typed rejects; feeds keep updating. *Test:* Catch2 vs mock server: request counter stays 0.
- **[DATA_MODEL-065]** A user-toggleable observation mode shall force the same gateway block regardless of key permissions, switchable at runtime through `IDataController`. *Accept:* enabling the toggle makes the next command reject locally; disabling restores submission. *Test:* Catch2 unit: toggle then command, assert both behaviors.

#### DATA_MODEL: Case 9.4 — Kill-switch primitive
The flatten-everything enforcement point (hotkey/UI trigger is APP's).
- **[DATA_MODEL-066]** A `PanicFlatten` command shall cancel all working orders across all active categories (via cancel-all) and close all open positions with reduce-only market orders, bypassing the throttle queue, confirmation hooks, and session-sync gating. *Accept:* mock scenario with 2 working orders and 1 position: the server receives cancel-all then a reduce-only market close, using at most 1 request per category plus 1 per position. *Test:* Catch2 vs mock HTTP scenario: request sequence + flags.
- **[DATA_MODEL-067]** After panic execution a verification pass shall REST-fetch open orders and positions, retry residuals up to a configurable count, and emit a final report event stating flat/not-flat. *Accept:* a mock leaving 1 residual order after the first pass triggers a second cancel and a final report with flat=true; exhausted retries report flat=false. *Test:* Catch2 vs mock scenario with scripted residual.
- **[DATA_MODEL-088]** `PanicFlatten` shall accept an optional scope: current-symbol (cancel and flatten that symbol only) and cancel-orders-only (no position closes), alongside the global default (consumed by [APP-086]). *Accept:* a symbol-scoped panic touches only that symbol's orders and position in the mock scenario; cancel-orders-only issues cancel-all with zero close orders. *Test:* Catch2 vs mock HTTP scenario per scope.

### 2.10 Entity convergence & currency hardening
Resolve the dead provider-neutral tier and harden fixed-point arithmetic defects.

#### DATA_MODEL: Case 10.1 — Provider-neutral tier decision
scratcher::data common entities (src/data/entities/common.hpp, instrument.hpp, trade.hpp) are dead code referenced nowhere.
- **[DATA_MODEL-068]** The unused provider-neutral entities shall be removed (keeping the in-use `OrderBookLevel`), with src/data/README.md updated to state that `IDataController` is the current provider-neutrality seam and type convergence is deferred until a second venue. *Accept:* build and full test suite pass after removal; a repo grep for the removed type names returns zero hits outside git history. *Test:* build + grep check in CI script.

#### DATA_MODEL: Case 10.2 — Currency arithmetic hardening (defect)
currency<T>::parse rescales through floating `std::pow`; `*`, `/`, and narrowing conversions can silently overflow.
- **[DATA_MODEL-070]** `currency::parse` shall rescale with integer multiplication/division only — no floating-point path. *Accept:* an 18-decimal value (e.g. "0.000000000000000001" at scale 18) round-trips parse→to_string exactly for every digit position. *Test:* Catch2 unit: exhaustive last-digit round-trip at scales 0..18.
- **[DATA_MODEL-071]** `operator*`, `operator/`, and cross-storage conversions (including `currency<uint64_t>` → signed) shall detect overflow/narrowing loss and throw `std::overflow_error` instead of wrapping. *Accept:* a multiplication whose true product exceeds the storage max throws; converting a uint64 value above int64 max to `currency<int64_t>` throws. *Test:* Catch2 unit: boundary values around each storage limit.
- **[DATA_MODEL-072]** `raw_at` upscaling shall detect multiplicative overflow and throw rather than return a wrapped value. *Accept:* `raw_at(19)` on a near-max raw value throws `std::overflow_error`; in-range rescales are unchanged. *Test:* Catch2 unit: boundary matrix over scales.

### 2.11 Audit & transaction history

#### DATA_MODEL: Case 11.1 — Mutating-action audit trail
Append-only local audit of every mutating command and session/connectivity event.
- **[DATA_MODEL-077]** Every mutating command passing the gateway shall append an audit record — timestamp, action kind, request payload with secrets redacted, typed result or exchange retCode/retMsg, orderLinkId, round-trip latency — to an append-only local audit store through a datahub sink. *Accept:* one PlaceOrder round trip appends exactly one record carrying its orderLinkId, retCode, and a nonzero latency; existing rows are never mutated (rowid count strictly grows). *Test:* Catch2 vs mock HTTP: submit, assert audit row content and append-only growth.
- **[DATA_MODEL-078]** Session and connectivity events (connect, disconnect, re-auth, resync completion, rate-limit hits, clock-drift warnings) shall append to the same audit/event store with their event payloads. *Accept:* a scripted drop/reconnect scenario appends disconnect, re-auth, and resync-complete records in occurrence order. *Test:* Catch2 vs mock WS/HTTP scenario: assert record sequence.

#### DATA_MODEL: Case 11.2 — Transaction log & closed PnL
Cash-flow-level account records fetched and persisted for history views.
- **[DATA_MODEL-079]** A paginated signed `/v5/account/transaction-log` fetch shall persist entries (type, currency, amount, fee, timestamp, related symbol/orderId) for a configurable lookback through the private sinks, following `nextPageCursor` (viewing is [TRADER_HUD-080]). *Accept:* a 2-page mocked cursor chain issues exactly 2 requests and persists all rows exactly once. *Test:* Catch2 vs mock HTTP cursor fixture.
- **[DATA_MODEL-080]** (defer) A `/v5/position/closed-pnl` fetch shall persist per-round-trip closed-PnL records (entry/exit average price, qty, closed PnL, fee/funding breakdown) exposed via an `IDataController` accessor for TRADER_HUD stats views. *Accept:* an official-shape fixture round-trips to persisted rows queryable by symbol and time range. *Test:* Catch2 vs mock HTTP fixture.
## 3. Trader cockpit — visualization & panel content logic (TRADER_HUD)
The cockpit (src/cockpit) is the controller+view-model layer: TradeCockpit routes datahub feeds to registered ContentPanels, and InstrumentPanel renders a live ThorVG buoy-candle chart via pluggable scratchers. Today only MarketGraph/Empty panels have content, the chart is live-follow only (reverse projections TimeOfHudX/PriceOfHudY exist unused, src/cockpit/instrument_panel.hpp:127-137), the orderbook feed is materialised but unconsumed, and IDataController::SubscribeOrders/SubscribeTrades/PlaceOrder/CancelOrder are never called from this layer. This branch specifies chart interaction completeness, view-model content for the seven stub PanelTypes, chart/DOM order-entry business logic, and cockpit infrastructure fixes (unregistration, venue abstraction, error surfacing, event-driven invalidation). All panel content is specified as UI-toolkit-agnostic view models in the cockpit tier so Catch2 can drive them with synthetic datahub feeds; widget rendering chrome stays in APP.

### 3.1 Chart interaction & completeness
Turn the live-follow-only chart into a fully navigable instrument view: pan/zoom, crosshair, timeframe switching, history on demand, bounded memory, and trade-context overlays.

#### TRADER_HUD: Case 1.1 — Pan & zoom
Mouse-driven navigation of the time/price viewport using the existing reverse projections and view-pinning machinery.
- **[TRADER_HUD-001]** InstrumentPanel shall expose a drag-pan input method that pins the view (SetViewLeftTimeMs) and shifts view-left time by the drag distance converted through TimeOfHudX, disengaging live-follow. *Accept:* dragging 100 px left at 10 px/candle moves ViewLeftTimeMs by exactly 10 candle periods and live-edge follow stops. *Test:* Catch2 unit on TestPanel: invoke drag, assert ViewLeftTimeMs delta and pin state.
- **[TRADER_HUD-002]** Wheel-zoom shall rescale px_per_ms around the cursor anchor so the timestamp under the cursor is invariant across the zoom step. *Accept:* after any zoom step, TimeOfHudX(cursor_x) differs from pre-zoom value by <1% of the visible time span. *Test:* Catch2 unit: pinned view, zoom at fixed x, assert anchor invariance.
- **[TRADER_HUD-003]** Zoom shall be clamped to configured bounds so the logical transform never degenerates. *Accept:* candle pixel width stays within [1, 200] px under repeated zoom; e11 > 0 always holds. *Test:* Catch2 unit: 100 zoom steps each direction, assert clamps.
- **[TRADER_HUD-004]** A jump-to-live action shall clear the view pin and re-engage wall-clock live-edge anchoring. *Accept:* after the action, ViewLeftTimeMs returns the wall-clock-derived value and subsequent ticks advance the view. *Test:* Catch2 unit: pin, jump-to-live, assert unpinned anchor behavior.

#### TRADER_HUD: Case 1.2 — Crosshair with time/price readout
Cursor-tracking hairlines with formatted time and price labels at the ruler strips.
- **[TRADER_HUD-005]** A crosshair scratcher shall render hairlines through the cursor plus a time label (TimeOfHudX) and a price label (PriceOfHudY, formatted with PriceDecimals) in the ruler strips, drawn in HudScene per the two-layer non-scaling pattern. *Accept:* rendered label strings equal the reverse projections of the cursor position; hairlines pass through the cursor pixel ±1 px in the snapshot. *Test:* Catch2 render-to-PNG (test/render pattern) + string assertions on labels.
- **[TRADER_HUD-006]** Crosshair movement shall repaint via the MarkDirty damage path, not a full-canvas redraw. *Accept:* Render() after a cursor move returns a damage rect covering only old+new crosshair bounds, strictly smaller than the canvas. *Test:* Catch2 unit: move cursor, assert returned PixelRect area.

#### TRADER_HUD: Case 1.3 — Timeframe switching & re-aggregation
Runtime candle-period change without panel reconstruction (period is currently a const constructor parameter).
- **[TRADER_HUD-007]** InstrumentPanel shall support SetCandlePeriod at runtime, rebuilding BuoyCandleQuotes by replaying the retained trade series. *Accept:* switching 1m→5m over 25 min of synthetic trades yields exactly 5 closed buoys whose volume totals equal the 1m aggregate. *Test:* Catch2 unit with clock-injected ingest, switch period, assert buoy series.
- **[TRADER_HUD-008]** TradeCockpit shall expose the configured set of available candle periods for the app-layer selector. *Accept:* GetAvailableCandlePeriods() returns the config-defined list containing the default period. *Test:* Catch2 unit vs CLI11 config fixture.
- **[TRADER_HUD-009]** A timeframe switch shall preserve the view anchor time. *Accept:* |ViewLeftTimeMs after − before| < one new candle period. *Test:* Catch2 unit: pin view, switch period, assert anchor.

#### TRADER_HUD: Case 1.4 — Historical backfill on scroll-back
Cockpit-side trigger and consumption of older data when the user pans before the loaded series (fetch/replay mechanism is DATA_MODEL/CORE).
- **[TRADER_HUD-010]** When the view-left time crosses the earliest retained trade, the panel shall issue one history request per uncovered gap through the IDataController history API, with in-flight deduplication. *Accept:* panning 10 times into the same gap issues exactly 1 request to a counting mock controller. *Test:* Catch2 unit vs mock IDataController counting requests.
- **[TRADER_HUD-011]** Backfilled trades shall merge into the chart through the existing snapshot/increment ingestion path without duplicating existing buoys. *Accept:* mock delivery of 100 older trades extends the closed pool left; re-delivering the same batch changes nothing. *Test:* Catch2 unit: ingest, re-ingest, assert buoy count and values.
- **[TRADER_HUD-012]** The panel shall expose a backfill-pending state and render a loading cue while a request is in flight. *Accept:* BackfillPending() is true between request and delivery, false after; cue visible in snapshot only while pending. *Test:* Catch2 unit + render snapshot with mock delayed delivery.
- **[TRADER_HUD-087]** When backfill demand predates retained trade history, the chart shall source closed buoys from the persisted kline series (consumes [DATA_MODEL-056]), merging kline-derived buoys with trade-built buoys at the seam without duplicating the boundary period. *Accept:* a gap served by 100 fixture klines renders 100 closed buoys left of the trade-built series with the boundary period appearing exactly once. *Test:* Catch2 unit vs mock kline accessor.

#### TRADER_HUD: Case 1.5 — Bounded chart memory
Eviction of the append-only closed buoy pool (currently unbounded over long sessions).
- **[TRADER_HUD-013]** The closed buoy pool shall evict buoys outside a configured retention window while preserving the closed-pool append-only re-emit invariant on rebuild. *Accept:* with retention 5000 buoys, ingesting 20000 periods keeps the pool ≤5000 and rendering shows no stale shapes. *Test:* Catch2 unit: long synthetic ingest, assert pool size + snapshot.
- **[TRADER_HUD-014]** Scrolling back into an evicted range shall re-materialise it through the backfill path with values identical to pre-eviction. *Accept:* evict, scroll back, backfill replay reproduces a buoy series equal field-by-field (currency and time_point model values) to the pre-eviction series for the range. *Test:* Catch2 unit: capture series, evict, replay, compare.

#### TRADER_HUD: Case 1.6 — Own orders & executions on chart
Trade-context overlays from the private feeds (feeds exist; cockpit never subscribes today). Position entry and liquidation price line overlays are a product-level deferred requirement — no further decomposition until trading-core delivery schedules them.
- **[TRADER_HUD-015]** Working-order lines shall render as horizontal HudScene overlays at HudYOfPrice(order price), labeled with side and qty, sourced from the private orders feed. *Accept:* 3 mock working orders render 3 lines at correct y ±1 px; a fill/cancel update removes the line on next frame. *Test:* Catch2 render snapshot vs synthetic orders feed.
- **[TRADER_HUD-016]** Execution markers shall render at (execTime, execPrice) for own fills within the visible window. *Accept:* mock executions render markers at projected x,y ±1 px; off-window executions render nothing. *Test:* Catch2 render snapshot vs synthetic trades feed.
- **[TRADER_HUD-017]** Order-line rendering shall be capped with nearest-to-view priority to avoid clutter. *Accept:* 150 working orders render exactly the 100 whose prices are closest to the visible band. *Test:* Catch2 unit: assert selected subset from synthetic order set.

#### TRADER_HUD: Case 1.7 — Core indicators (defer)
Minimal pluggable indicator layer computed from closed buoys.
- **[TRADER_HUD-018]** (defer) An indicator scratcher API shall compute derived series from the closed buoy pool, with MA and session VWAP as the first implementations rendered as LogicalScene polylines. *Accept:* MA(9) values match reference computation within 1e-6 on a fixture series. *Test:* Catch2 unit vs hand-computed fixture + render snapshot.
- **[TRADER_HUD-019]** (defer) Indicator instances shall carry per-instance parameters and render independently. *Accept:* two MA instances (9, 21) on one panel produce two distinct polylines with correct values. *Test:* Catch2 unit: two instances, assert both series.

#### TRADER_HUD: Case 1.8 — Drawing tools (defer)
User-drawn levels anchored in model coordinates.
- **[TRADER_HUD-020]** (defer) Horizontal levels and trendlines shall be anchored in (time_ms, price_points) model coordinates and re-project correctly under pan/zoom/refloor. *Accept:* after pan+zoom+SceneFloor change, drawn endpoints re-project to the same model coordinates (round-trip error ≤1 px). *Test:* Catch2 unit: draw, transform view, assert re-projection.

#### TRADER_HUD: Case 1.9 — Advanced chart tiers (defer)
Consciously deferred advanced charting: analytics tier, crosshair sync, price-series source toggle.
- **[TRADER_HUD-094]** (defer) An advanced chart analytics tier — footprint/cluster rendering, volume profile, cumulative delta — shall build on the closed-buoy and trade series when scheduled. *Accept:* not applicable until scheduled; leaf registers the conscious deferral for the coverage map. *Test:* none (defer).
- **[TRADER_HUD-096]** (defer) Multi-chart crosshair synchronization across panels sharing a symbol-link group ([APP-036]) shall mirror the crosshair time across group members. *Accept:* not applicable until scheduled; leaf registers the conscious deferral. *Test:* none (defer).
- **[TRADER_HUD-097]** (defer) A price-series source toggle for derivatives charts (last / mark / index, consumes [DATA_MODEL-015]) shall switch the series feeding buoy aggregation. *Accept:* toggling to mark rebuilds buoys from the mark-price series on the next update. *Test:* Catch2 unit vs synthetic mark/index feeds.

### 3.2 Order book / DOM panel
Give PanelType::OrderBook real content by consuming the per-symbol orderbook feed that ByBitDataManager already materialises but nothing reads.

#### TRADER_HUD: Case 2.1 — Depth ladder view model
A price ladder maintained live from the orderbook snapshot feed.
- **[TRADER_HUD-021]** An OrderBookPanel (ContentPanel, PanelType::OrderBook) shall subscribe to the per-symbol orderbook_feed_type through TradeCockpit registration and maintain a bid/ask ladder view model. *Accept:* after a 50-level snapshot plus 3 increments, ladder rows equal the expected merged book with strictly ordered prices and both sides separated. *Test:* Catch2 unit vs synthetic orderbook feed dispatches.
- **[TRADER_HUD-022]** The ladder shall expose a configurable visible depth of N levels per side plus a spread row. *Accept:* view model yields exactly N bid + N ask rows and spread = best_ask − best_bid for the fixture book. *Test:* Catch2 unit: set N=10, assert row count and spread.

#### TRADER_HUD: Case 2.2 — Price grouping & cumulative volume
Tick aggregation and running totals, the minimum DOM analytics.
- **[TRADER_HUD-023]** The ladder shall support price grouping at tick-size multiples (1x/10x/100x) summing level sizes into buckets. *Accept:* at 10x grouping, bucket sizes equal the sum of member levels and total size per side is preserved. *Test:* Catch2 unit: group fixture book, assert bucket sums.
- **[TRADER_HUD-024]** Each ladder row shall carry cumulative volume from the top of its side. *Accept:* cumulative at row k equals the sum of sizes of rows 1..k on that side for the fixture book. *Test:* Catch2 unit: assert running sums.

#### TRADER_HUD: Case 2.3 — Ladder centering modes
Auto-centering dynamic mode versus manually scrolled static ladder.
- **[TRADER_HUD-025]** The ladder shall auto-center on the mid-price by default, and a manual scroll shall pin the price window until recentered. *Accept:* while pinned, a fixed price stays at a fixed row index across 100 updates; unpinned, mid-price stays within center ±1 row. *Test:* Catch2 unit: scroll, dispatch updates, assert row stability.
- **[TRADER_HUD-026]** A Recenter action shall re-engage auto-centering immediately. *Accept:* after Recenter(), the next update places mid-price within center ±1 row. *Test:* Catch2 unit: pin, recenter, assert.

#### TRADER_HUD: Case 2.4 — Own orders in the ladder
Working orders marked inline at their price rows.
- **[TRADER_HUD-027]** Ladder rows whose price matches a working order shall be flagged with the order's side and quantity, sourced from the private orders feed. *Accept:* 2 mock working orders flag exactly their 2 rows with correct qty; a cancel update clears the flag. *Test:* Catch2 unit vs synthetic orders + orderbook feeds.
- **[TRADER_HUD-028]** Order flags shall track price grouping so a grouped bucket aggregates the working-order qty of its members. *Accept:* two orders inside one 10x bucket flag that bucket with the summed qty. *Test:* Catch2 unit: group, assert aggregated flag.
- **[TRADER_HUD-084]** The ladder shall mark the current position inline: the row (or grouped bucket) containing the position's entry price is flagged with the position's side and size from the positions feed. *Accept:* a fixture position with entry 100.5 flags exactly that row with (long, 2.0); a position-closed update clears the flag. *Test:* Catch2 unit vs synthetic positions + orderbook feeds.

#### TRADER_HUD: Case 2.5 — Order-flow analytics tier (defer)
Large-print highlighting on the ladder from the public trade stream.
- **[TRADER_HUD-029]** (defer) The ladder shall optionally highlight rows where recent public prints exceed a configurable size threshold, decaying after a configurable interval. *Accept:* a 5.0-size print at price p highlights row p when threshold is 1.0 and clears after the decay interval. *Test:* Catch2 unit with injected clock: print, advance, assert highlight lifecycle.
- **[TRADER_HUD-095]** (defer) Order-flow analytics beyond TRADER_HUD-029 — historical liquidity heatmap, large-lot/iceberg detection highlighting, trade bubbles — shall remain deferred until scheduled. *Accept:* not applicable until scheduled; leaf registers the deferral for the coverage map. *Test:* none (defer).

### 3.3 Time & Sales tape
Give PanelType::TradeHistory-adjacent tape content: streaming public prints with filtering and pause, fed by the existing public_trades_feed.

#### TRADER_HUD: Case 3.1 — Streaming tape rows
Newest-first print rows with aggressor-side data.
- **[TRADER_HUD-030]** A TimeAndSalesPanel shall consume the per-symbol public_trades_feed and maintain newest-first rows of (time, price, size, side) with side carried for coloring. *Accept:* dispatching 10 synthetic trades yields 10 rows in reverse time order with correct side flags. *Test:* Catch2 unit vs synthetic public trades feed.
- **[TRADER_HUD-031]** The tape shall bound its row cache with a configurable ring buffer. *Accept:* with cap 1000, ingesting 1500 trades retains exactly the newest 1000. *Test:* Catch2 unit: overflow ingest, assert retained window.

#### TRADER_HUD: Case 3.2 — Size filter & aggregation
Isolating meaningful prints.
- **[TRADER_HUD-032]** A minimum-size filter and size-tier highlighting shall be applied in the view model. *Accept:* filter 0.5 hides all prints <0.5; prints ≥ tier thresholds carry the matching tier flag. *Test:* Catch2 unit: mixed-size fixture, assert visible rows and tiers.
- **[TRADER_HUD-033]** An aggregation toggle shall collapse consecutive same-price same-side prints into one row with summed size and latest timestamp. *Accept:* 3 consecutive matching prints produce 1 row with the summed size; a side change breaks the run. *Test:* Catch2 unit: fixture sequences, assert collapsed rows.

#### TRADER_HUD: Case 3.3 — Pause & resume-to-live
Reading the tape without losing the live stream.
- **[TRADER_HUD-034]** Pausing shall freeze the visible row set while ingestion continues into the ring buffer; resume shall jump to live. *Accept:* during pause, visible rows are unchanged while 100 trades arrive; resume shows them; the cap is never exceeded. *Test:* Catch2 unit: pause, ingest, resume, assert row sets.
- **[TRADER_HUD-035]** While paused, the view model shall support scrolling over the buffered window. *Accept:* scroll offset exposes any contiguous slice of the retained rows; offsets beyond the buffer clamp. *Test:* Catch2 unit: paused scroll through fixture buffer.

### 3.4 Watchlist / market watch
Give PanelType::Watchlist content: the instrument navigation hub with live per-symbol stats and selection retargeting.

#### TRADER_HUD: Case 4.1 — Live quote table
Symbol rows with live stats columns.
- **[TRADER_HUD-036]** A WatchlistPanel shall list member symbols with columns (last price, 24h change %, 24h volume) maintained from a per-venue ticker feed on IDataController. *Accept:* a mock ticker update changes the matching row's values on the next dispatch. *Test:* Catch2 unit vs mock ticker feed dispatches.
- **[TRADER_HUD-037]** Rows shall be sortable by any column with stable order for ties. *Accept:* sorting by 24h change yields descending values; equal values keep insertion order. *Test:* Catch2 unit: sort fixture rows, assert order.
- **[TRADER_HUD-038]** A row shall be flagged stale when no update arrives within a configured interval and unflagged on the next update. *Accept:* with a 10 s threshold and injected clock, the flag appears at 10 s and clears on update. *Test:* Catch2 unit with injected clock.
- **[TRADER_HUD-089]** (defer) Mark/index price columns and inline per-row mini-stats (bid/ask, spread, sparkline over the last N ticks) shall extend the watchlist row model (consumes [DATA_MODEL-015]/[DATA_MODEL-073]). *Accept:* fixture ticker data populates the extra columns; the sparkline series equals the last N fixture ticks. *Test:* Catch2 unit vs ticker fixture.

#### TRADER_HUD: Case 4.2 — Search & membership
Building the list from the known instrument universe.
- **[TRADER_HUD-039]** Instrument search shall filter the instruments feed cache by case-insensitive substring. *Accept:* searching "btc" returns every symbol containing "BTC" from a fixture instrument list and nothing else. *Test:* Catch2 unit vs instruments feed fixture.
- **[TRADER_HUD-040]** Symbols shall be addable and removable from the watchlist, with removal dropping the row and its ticker interest. *Accept:* add shows the row within one dispatch; remove deletes it and releases the per-symbol subscription (subscriber count returns to baseline). *Test:* Catch2 unit vs mock controller counting subscriptions.
- **[TRADER_HUD-088]** (defer) Multiple named watchlists, pinned favorites, and a ByBit category filter (spot / linear / inverse / option) shall extend the watchlist model. *Accept:* switching the active named list swaps the row set; pinned rows sort first; the category filter hides non-matching symbols. *Test:* Catch2 unit fixtures.

#### TRADER_HUD: Case 4.3 — Selection retargeting
The watchlist drives which instrument linked panels show.
- **[TRADER_HUD-041]** Row selection shall publish an instrument-selected event through a TradeCockpit channel carrying the symbol. *Accept:* selecting a row fires the callback exactly once with that symbol. *Test:* Catch2 unit: select, assert callback payload.
- **[TRADER_HUD-042]** Panels registered as symbol-linked shall rebind to the selected symbol, resolving InstrumentInfo and swapping the per-symbol data subscription. *Accept:* after selection, a linked chart panel's Symbol() equals the new symbol and its old trade subscription is released. *Test:* Catch2 integration: cockpit + mock controller, assert rebind and teardown.

### 3.5 Orders table
Give PanelType::Orders content: the live working-orders view over the private orders feed (SubscribeOrders currently has no caller).

#### TRADER_HUD: Case 5.1 — Working orders view model
Live table of open orders with lifecycle-driven updates.
- **[TRADER_HUD-043]** An OrdersPanel shall subscribe to the private orders feed via TradeCockpit and expose working orders (non-terminal statuses) with columns (symbol, side, type, price, qty, filled qty, status, updated time). *Accept:* dispatching 3 synthetic working orders yields 3 rows with matching field values. *Test:* Catch2 unit vs synthetic orders feed.
- **[TRADER_HUD-044]** Order updates shall transition rows in place and remove rows on terminal status (Filled/Cancelled/Rejected). *Accept:* New→PartiallyFilled updates the row's filled qty; a Filled update removes the row from the working set. *Test:* Catch2 unit: lifecycle sequence, assert row set at each step.

#### TRADER_HUD: Case 5.2 — Row actions
Cancel and amend intents routed from the table.
- **[TRADER_HUD-045]** A per-row cancel action shall call IDataController::CancelOrder once with the row's orderId and symbol and mark the row pending-cancel until the feed confirms. *Accept:* invoking cancel triggers exactly one CancelOrder on a mock controller; the pending flag clears when a Cancelled update arrives. *Test:* Catch2 unit vs mock controller + synthetic confirmation.
- **[TRADER_HUD-046]** A per-row amend action shall emit an amend-intent event carrying orderId, current price, and qty for the APP order ticket to prefill. *Accept:* the intent payload equals the row's current values. *Test:* Catch2 unit: trigger amend, assert intent payload.

### 3.6 Positions table
Give PanelType::Positions content: live position rows with PnL and close intents (requires a positions feed from the data layer — DATA_MODEL dependency).

#### TRADER_HUD: Case 6.1 — Positions view model
Live table over a positions feed. ADL rank indicator and selectable unrealized-PnL basis (mark vs last) are a product-level deferred requirement — no further decomposition until scheduled; TRADER_HUD-048 remains mark-price basis until then.
- **[TRADER_HUD-047]** A PositionsPanel shall consume a per-venue positions feed on IDataController with columns (symbol, side, size, entry price, mark price, liquidation price, unrealized PnL). *Accept:* a mock position update changes the matching row within one dispatch. *Test:* Catch2 unit vs mock positions feed.
- **[TRADER_HUD-048]** Unrealized PnL shall be recomputed on every mark-price update as (mark − entry) · size · side-sign in the settle currency using fixed-point currency arithmetic. *Accept:* a fixture position (entry 100, mark 105, size 2, long) shows uPnL exactly 10 with correct decimals. *Test:* Catch2 unit: fixture positions, assert exact currency values.
- **[TRADER_HUD-085]** (defer) Hedge-mode rendering shall present separate long/short legs per symbol keyed by positionIdx when the account is in hedge mode. *Accept:* a two-leg fixture yields two rows for one symbol with correct sides and sizes. *Test:* Catch2 unit fixture.

#### TRADER_HUD: Case 6.2 — Position row actions
Close and focus actions from the row.
- **[TRADER_HUD-049]** Close-at-market and close-at-limit-prefill actions shall emit order intents; close-at-market carries opposite side, full size, and reduce-only. *Accept:* close-at-market on a long 2.0 position emits an intent (Sell, 2.0, reduce-only). *Test:* Catch2 unit: trigger action, assert intent payload.
- **[TRADER_HUD-050]** Row click shall publish the instrument-selected event (TRADER_HUD-041 channel) with the position's symbol. *Accept:* clicking a row fires the retarget callback once with that symbol. *Test:* Catch2 unit: click, assert callback.
- **[TRADER_HUD-082]** Position row actions shall additionally emit reverse and partial-close intents: reverse emits a reduce-only market close of the full size followed by an opposite-side open of the same size; partial close emits a reduce-only close for the chosen qty or percentage, exact in currency units. *Accept:* reverse on a long 2.0 emits (Sell 2.0 reduce-only) then (Sell 2.0 open); partial close 50% emits (Sell 1.0 reduce-only). *Test:* Catch2 unit: trigger actions, assert intent payload sequence.
- **[TRADER_HUD-083]** (defer) A move-stop-to-breakeven action shall emit a TP/SL amend intent placing the stop at the position's entry price, tick-snapped (consumes [DATA_MODEL-050]). *Accept:* the emitted intent carries triggerPrice equal to entry snapped to tick. *Test:* Catch2 unit intent assertion.

### 3.7 History & stats views
Give PanelType::TradeHistory (and TradeStats) content: execution and order history from the private feeds plus the SQLite store already populated by datahub persistence.

#### TRADER_HUD: Case 7.1 — Execution history table
Fill-level records merged from live feed and persisted rows.
- **[TRADER_HUD-051]** A TradeHistoryPanel shall show executions (time, price, qty, fee, fee currency, maker/taker) merging the live private trades feed with a SQLite-backed query for older ranges, deduplicated by execId. *Accept:* 25 stored + 5 live executions display as 30 time-ordered rows with no duplicates. *Test:* Catch2 integration: in-memory SQLite fixture + synthetic feed, assert merged rows.
- **[TRADER_HUD-052]** Executions shall be groupable by orderId with a computed average fill price. *Accept:* 3 fills of one order group under a parent row with avg = Σ(p·q)/Σq exact in currency arithmetic. *Test:* Catch2 unit: fixture fills, assert group aggregate.

#### TRADER_HUD: Case 7.2 — Order history view
Closed/cancelled order records with filters.
- **[TRADER_HUD-053]** An order-history view shall query persisted orders with terminal statuses filtered by symbol, status, and time range through datahub query conditions, paginated. *Accept:* filtering symbol=BTCUSDT, status=Filled returns only matching rows; page size 50 returns ≤50 rows per page with a working next-page cursor. *Test:* Catch2 unit vs in-memory SQLite order fixture.
- **[TRADER_HUD-054]** Each history row shall expose lifecycle detail (created/updated timestamps, orderLinkId, reject reason when present). *Accept:* a rejected fixture order's row carries its reject reason string and both timestamps. *Test:* Catch2 unit: fixture rows, assert fields.
- **[TRADER_HUD-092]** (defer) Order-history rows for conditional/TP-SL orders shall expose trigger history detail (trigger price vs actual execution price). *Accept:* a triggered-stop fixture row carries both values. *Test:* Catch2 unit fixture.
- **[TRADER_HUD-093]** (defer) A deep-link action on a history row shall pin the linked chart's view to the row's timestamp (consumes the [TRADER_HUD-001] view-pinning path). *Accept:* invoking the action sets ViewLeftTimeMs so the row time lies inside the visible window. *Test:* Catch2 unit assertion on view state.

#### TRADER_HUD: Case 7.3 — Trade stats panel (defer)
Aggregated session statistics.
- **[TRADER_HUD-055]** (defer) A TradeStatsPanel shall compute session realized PnL, total fees, volume, and trade count per instrument from the execution history. *Accept:* values match a hand-computed fixture of 10 executions exactly (currency arithmetic, no float drift). *Test:* Catch2 unit vs execution fixture.

#### TRADER_HUD: Case 7.4 — Transaction log & balance explanation
Account cash-flow records viewable and explainable from the persisted transaction log.
- **[TRADER_HUD-080]** A transaction-log view shall query persisted transaction-log entries (consumes [DATA_MODEL-079]) filtered by type and time range through datahub query conditions, paginated like TRADER_HUD-053. *Accept:* filtering type=funding returns only funding rows; page size 50 returns ≤50 rows per page with a working cursor. *Test:* Catch2 unit vs in-memory SQLite fixture.
- **[TRADER_HUD-081]** (defer) A balance-change explanation view shall link the equity delta between two points in time to the transaction-log entries that explain it. *Accept:* a fixture delta decomposes into entries summing exactly to the delta in currency arithmetic. *Test:* Catch2 unit vs transaction-log fixture.

### 3.8 Instrument info display
Expose the already-persisted InstrumentInfo reference data as display content and order-entry validation input.

#### TRADER_HUD: Case 8.1 — Contract specs display data
Formatted instrument rules for panels and the order ticket.
- **[TRADER_HUD-056]** The cockpit shall assemble an instrument-info view model (tick size, qty step, min/max order qty, min order amount, status) from InstrumentInfo with decimals derived exactly as RegisterInstrumentPanel does today. *Accept:* fixture instrument fields format to the exact expected strings (e.g. tickSize "0.01" → 2 price decimals). *Test:* Catch2 unit vs InstrumentInfo fixture.
- **[TRADER_HUD-057]** Non-Trading instrument status shall gate order intents: intents for such symbols are blocked with a reason string surfaced to the emitting panel. *Accept:* status=Settling blocks intent emission and exposes "instrument not trading" reason; status=Trading passes. *Test:* Catch2 unit: fixture statuses, assert gating.

#### TRADER_HUD: Case 8.2 — Fee & funding display (defer)
Derivative/fee context beside order entry (needs DATA_MODEL fee-rate/funding acquisition).
- **[TRADER_HUD-058]** (defer) The instrument-info view model shall include maker/taker fee rates (and funding rate/countdown for derivatives) when the data layer provides them, marked unavailable otherwise. *Accept:* mock fee rates display for the symbol; absent data shows an explicit unavailable state, never zeros. *Test:* Catch2 unit vs mock controller with and without fee data.
- **[TRADER_HUD-098]** (defer) A derivative stats display (mark price, index price, open interest, basis, 24h turnover) shall extend the instrument-info view model, tied to the DATA_MODEL derivatives milestone (consumes [DATA_MODEL-015]). *Accept:* fixture ticker data populates all five stats fields. *Test:* Catch2 unit vs fixture.

### 3.9 Chart & DOM order-entry interactions
Business-logic side of direct-manipulation trading: price picking and order dragging on the chart/ladder; the ticket UI itself is APP.

#### TRADER_HUD: Case 9.1 — Click-price-to-prefill
Clicking a chart level or DOM cell yields a tick-snapped order intent.
- **[TRADER_HUD-059]** A click on the chart inner rect or a DOM price cell shall emit an order-prefill intent (symbol, side by bid/ask context, price) with the chart price obtained via PriceOfHudY and snapped to tick size. *Accept:* clicking at hud_y emits price = PriceOfHudY(hud_y) rounded to the nearest tick, exact in currency units. *Test:* Catch2 unit: pinned view, click coordinates, assert intent payload.
- **[TRADER_HUD-060]** Click-trading shall pass intents only in the armed state, owned by the cockpit and default disarmed. *Accept:* disarmed clicks emit nothing; after Arm(), the same click emits one intent; Disarm() suppresses again. *Test:* Catch2 unit: state machine transitions with click injections.

#### TRADER_HUD: Case 9.2 — Drag working-order lines to amend
Direct price amendment by dragging the on-chart order line.
- **[TRADER_HUD-061]** Dragging a working-order line (hit-test within ±3 px of the line) shall emit on release an amend intent with the new tick-snapped price. *Accept:* dragging 5 ticks up emits one amend intent with price = old + 5·tick; a drag released at the original price emits nothing. *Test:* Catch2 unit: synthetic order line, drag sequence, assert intent.
- **[TRADER_HUD-062]** The dragged order shall render pending-amend at the new price until the private orders feed confirms, then reconcile to the exchange-reported price. *Accept:* line shows pending state at the drag price; a feed update at a different price snaps the line to the feed value. *Test:* Catch2 unit + render snapshot vs synthetic confirmation sequence.

#### TRADER_HUD: Case 9.3 — Drag TP/SL handles (defer)
TP/SL level manipulation on chart and position rows (needs TP/SL order support end-to-end).
- **[TRADER_HUD-063]** (defer) TP and SL levels attached to a position or order shall be draggable with the same hit-test/snap/pending semantics as TRADER_HUD-061/062, each emitting a TP-SL amend intent. *Accept:* dragging the TP handle emits one intent with the snapped trigger price and pending state until confirmation. *Test:* Catch2 unit: synthetic TP/SL levels, drag, assert intent.

### 3.10 Cockpit infrastructure
Controller-layer fixes required for the above to be correct and cheap: lifecycle teardown, venue abstraction, error surfacing, and render-pipeline efficiency.

#### TRADER_HUD: Case 10.1 — Panel unregistration & subscription teardown
Registrations and per-registration subscriptions currently accumulate for process lifetime.
- **[TRADER_HUD-064]** Panel registration shall return an RAII handle whose destruction removes the registry entry and releases every subscription created for that registration. *Accept:* after 100 register/destroy cycles, the panel registry is empty and the trade feed's subscriber count equals its pre-registration baseline. *Test:* Catch2 unit: cycle registrations vs mock controller, assert counts.
- **[TRADER_HUD-065]** Instrument rebind (TRADER_HUD-042) and panel close shall route through the same teardown so no per-symbol subscription outlives its consumer. *Accept:* rebinding a chart 50 times across 2 symbols leaves exactly 1 live trade subscription. *Test:* Catch2 unit: repeated rebinds, assert subscription census.

#### TRADER_HUD: Case 10.2 — Venue abstraction
TradeCockpit hardwires bybit::ByBitDataManager in its constructor; the IDataController seam exists but is not injected.
- **[TRADER_HUD-066]** TradeCockpit::Create shall accept std::shared_ptr<IDataController> so the venue is injected by the application, removing bybit includes from cockpit headers. *Accept:* trade_cockpit.hpp compiles without including bybit/data_manager.hpp; grep confirms no bybit:: manager reference in src/cockpit. *Test:* build assertion + grep check in CI script.
- **[TRADER_HUD-067]** The full cockpit (registration, routing, panels) shall run against a mock IDataController with no network or SQLite. *Accept:* a Catch2 integration suite drives register→feed→panel view model end-to-end offline in <1 s. *Test:* Catch2 integration vs in-process mock controller.

#### TRADER_HUD: Case 10.3 — Error & connectivity state surfaced to panels
Replace swallow-and-continue (silent coUpdate catch, empty IngestAndScale catch) with visible state.
- **[TRADER_HUD-068]** TradeCockpit shall expose a per-venue connection-state channel using the presentation vocabulary connected | reconnecting | stale, derived from the transport states (consumes [CORE-033]) and the session states (consumes [DATA_MODEL-034]) per the shared mapping — connected ⇔ transport connected ∧ session synced; reconnecting ⇔ transport reconnecting ∨ session ∈ {connecting, authenticating, syncing}; stale ⇔ transport disconnected ∨ session ∈ {degraded, auth_failed} — with synchronous current-state delivery on subscribe. *Accept:* each mock transport/session pair maps to the specified presentation state and reaches a subscribed panel callback within one dispatch; late subscribers get the current state immediately. *Test:* Catch2 unit vs mock controller state injections covering the full mapping table.
- **[TRADER_HUD-069]** Panels shall render an explicit stale/disconnected visual state instead of silently freezing on last data. *Accept:* setting state=stale renders the stale badge in the chart snapshot; state=connected removes it. *Test:* Catch2 render snapshot per state.
- **[TRADER_HUD-070]** Exceptions in panel Update and trade ingestion shall be logged with context and counted on the error channel, never silently swallowed, and the update loop shall continue. *Accept:* a panel whose Update throws produces one log line + error-counter increment per tick while other panels keep updating. *Test:* Catch2 unit: throwing mock panel, assert counter and sibling updates.

#### TRADER_HUD: Case 10.4 — Event-driven invalidation
Replace the 25 ms full CalculateSize/OnLayout recompute per tick with dirty-driven updates.
- **[TRADER_HUD-071]** Panels shall be updated only when flagged dirty by data arrival, view-transform change, resize, or time-tick crossing a candle boundary; the heartbeat shall skip clean panels. *Accept:* with a pinned view and no data, the scratcher relayout counter stays 0 over 1 s; a single trade dispatch increments it exactly once. *Test:* Catch2 unit: instrumented relayout counter under scripted events.
- **[TRADER_HUD-072]** Live-follow view advancement shall mark only the view transform dirty, reusing scratcher geometry where the layout is unaffected. *Accept:* in live mode with no data, per-tick work is transform update + damage render only (relayout counter 0). *Test:* Catch2 unit: live ticks with counter assertions.

#### TRADER_HUD: Case 10.5 — TimeRuler incremental pan path
TimeRuler currently RebuildAll's every layout; the architecture supports an e13-only translate.
- **[TRADER_HUD-073]** A pure pan within the same tick step shall update the ruler by translation (e13) plus MarkDirty, emitting no new label paints. *Accept:* pan of <1 tick step performs 0 label re-emissions (counter) and the snapshot matches a full-rebuild reference pixel-exact. *Test:* Catch2 render: pan, compare against rebuilt reference PNG.
- **[TRADER_HUD-074]** Full rebuild shall occur only when the nice-step level, visible label set, or canvas size changes. *Accept:* zooming across a step boundary rebuilds exactly once; repeated same-step pans rebuild zero times. *Test:* Catch2 unit: rebuild counter across scripted pan/zoom script.

#### TRADER_HUD: Case 10.6 — Real text metrics
Char-count · font-size heuristics drive strip widths and collision suppression; replace with measured glyph bounds.
- **[TRADER_HUD-075]** Label width/height used for ruler strip sizing and collision logic shall come from ThorVG text bounds measurement of the actual string and font. *Accept:* PriceRuler strip width equals measured widest visible label width + configured padding ±1 px across 3 zoom fixtures. *Test:* Catch2 render: measure labels, assert strip width.
- **[TRADER_HUD-076]** Label collision suppression shall use measured bounding boxes so no two rendered labels overlap at any zoom. *Accept:* at a dense-zoom fixture, rendered label boxes are pairwise disjoint (snapshot scan finds no overlap). *Test:* Catch2 render snapshot: extract label boxes, assert disjoint.

#### TRADER_HUD: Case 10.7 — Cockpit ports for the app shell
Cockpit-owned surface the app layer consumes: order-submission port and clock-offset accessor.
- **[TRADER_HUD-090]** TradeCockpit shall expose an order-submission port forwarding order, cancel-all, and panic intents to `IDataController` and delivering the typed results back to the registering callback with intent correlation (consumed by [APP-004]/[APP-014]). *Accept:* one submitted intent reaches a mock controller exactly once and a fake result returns to the callback carrying the intent's correlation id. *Test:* Catch2 unit vs mock controller.
- **[TRADER_HUD-091]** TradeCockpit shall expose a clock-offset accessor surfacing the DATA_MODEL server-clock offset (consumes [DATA_MODEL-036]) for the app-layer drift banner (consumed by [APP-047]). *Accept:* a mock offset update of 2000 ms is readable through the accessor within one dispatch. *Test:* Catch2 unit vs mock controller.

### 3.11 Balances & account

#### TRADER_HUD: Case 11.1 — Wallet balances & account header
Per-coin balances panel view model and account-header aggregates.
- **[TRADER_HUD-077]** A WalletPanel view model shall consume the wallet feed (consumes [DATA_MODEL-001]) exposing per-coin rows (coin, equity, available, locked/in-order, USD valuation when provided) updated live. *Accept:* a 3-coin fixture yields 3 rows; a wallet delta updates the matching row within one dispatch. *Test:* Catch2 unit vs synthetic wallet feed.
- **[TRADER_HUD-078]** The wallet view model shall support small/zero-balance filtering with a configurable equity threshold. *Accept:* threshold 1.0 hides rows with equity below 1.0; disabling the filter restores all rows. *Test:* Catch2 unit fixture.
- **[TRADER_HUD-079]** An account-header view model shall expose total equity, available balance, and session realized PnL aggregated from the wallet and execution feeds using currency arithmetic (rendered by [APP-064]). *Accept:* fixture balances and executions produce exactly the hand-computed header values with no float drift. *Test:* Catch2 unit vs fixtures.
- **[TRADER_HUD-086]** (defer) A unified-account margin overview (account IM/MM rate, margin usage, borrow amounts) and a risk-limit tier table view model (consumes [DATA_MODEL-086]) shall present account margin state. *Accept:* fixture margin data renders the expected usage values; tier rows match the DATA_MODEL fixture. *Test:* Catch2 unit vs fixtures.
## 4. Application shell & UX — cycfi/elements app layer (APP)
The app layer (`src/app`, `src/main.cpp`, `src/config.*`, target `xcockpit`) is today a market-data viewer shell: tab/split/chart composition and live chart hosting work, but 7 of 9 PanelTypes are perpetual "Loading..." stubs, there is no order-entry UI, no persistence, no status/error feedback, and generic leaves are registered with hardcoded `panel_id 0`. This branch evolves the shell into the trading-terminal front end: order ticket with safety policies, interactive workspace with session persistence, connection/error surfacing, shell hygiene, and history/export UI. Binding testability constraint: cycfi/elements has no headless test harness, so every feature is split into an elements-free view-model/controller (Catch2-testable, see APP-058) and thin widget wiring verified visually via the snap-app Xvfb screenshot loop. Order execution/enforcement stays in the data-manager branch (DATA_MODEL); chart/scratcher content stays in cockpit rendering (TRADER_HUD) — this branch consumes their ports through TradeCockpit only.

### 4.1 Order ticket & trading actions
Give the user a safe, keyboard-capable write path: a real NewOrder panel with validation, layered confirmation, hotkeys, panic control, and an explicit read-only mode.

#### APP: Case 1.1 — Order ticket panel
Replace the NewOrder "Loading..." stub with a working buy/sell ticket bound to the leaf's instrument.
- **[APP-001]** `MainWindow::MakeLeaf` shall route `PanelType::NewOrder` to a dedicated order-ticket leaf offering side toggle (Buy/Sell), order type (Market/Limit), price field, quantity field, TIF selector (GTC/IOC/FOK/PostOnly) and a submit control instead of `MakeGenericLeaf`. *Accept:* a NewOrder panel shows all six controls and no "Loading..." indicator. *Test:* snap-app visual: NewOrder tab screenshot shows ticket controls.
- **[APP-002]** All ticket state and logic shall live in an elements-free `OrderTicketModel` that produces a venue-neutral order intent (the TRADER_HUD 3.9 intent shape: symbol, side, type, price, qty, TIF, flags) submitted through the cockpit order port (consumes [TRADER_HUD-090]); `bybit::OrderRequest` assembly is DATA_MODEL's ([DATA_MODEL-043]..[DATA_MODEL-045]). *Accept:* the model's TU includes no cycfi and no bybit header; for a populated Limit GTC buy every intent field equals the corresponding model field. *Test:* Catch2 unit: populate model, assert intent field-by-field mapping + include audit.
- **[APP-003]** The ticket shall bind to its leaf's selected symbol and `InstrumentInfo`, retargeting constraints when the symbol changes. *Accept:* after retarget, validation uses the new symbol's tickSize/qtyStep on the next check. *Test:* Catch2: retarget model between two fixture instruments, assert constraint swap.
- **[APP-004]** Submit shall forward through the single cockpit order-submission port ([TRADER_HUD-090]) and track pending → accepted/rejected from the result callback; while pending the submit control renders the in-flight state, and a duplicate attempt surfaces the gateway's `duplicate_submit` reject (enforcement is [DATA_MODEL-053]). *Accept:* with a fake port, one submit yields exactly one port call; the control shows in-flight until the callback; a fake duplicate reject renders as a rejection state. *Test:* Catch2 vs fake order port: call count + state machine.

#### APP: Case 1.2 — Client-side order validation
Validate against instrument trading rules before anything leaves the client.
- **[APP-005]** The ticket shall surface, as per-field inline state, the results of the shared DATA_MODEL instrument-rule validator (consumes [DATA_MODEL-060]/[DATA_MODEL-061]) evaluated on each edit, disabling submit while any check fails; the ticket re-specifies no rules. *Accept:* price 100.003 with tickSize 0.01 disables submit and names the validator's violated constraint; a corrected value enables submit. *Test:* Catch2 validation surface vs fake validator results.
- **[APP-006]** Field visibility and validation surface shall follow the order type: Market hides the price field and skips its validation; Limit requires a positive price whose validity comes from the shared validator ([DATA_MODEL-060]). *Accept:* toggling Limit→Market clears price errors; Market→Limit with empty price disables submit. *Test:* Catch2 type-toggle transition cases.
- **[APP-007]** Validation failures shall render as per-field inline messages sourced verbatim from model error strings, never as modal dialogs. *Accept:* model emits exactly one error per failing field; rendered text equals model text. *Test:* Catch2 error-string assertions; snap-app visual for inline placement.

#### APP: Case 1.3 — Confirmation policies
Configurable, immediately-effective confirmation layers between click and wire.
- **[APP-008]** A `ConfirmationPolicy` (always / above-notional-threshold / never) shall be evaluated on every submit; when triggered, a modal summary (symbol, side, type, price, qty, notional) precedes the port call and decline sends nothing. *Accept:* policy=always: decline → zero port calls, confirm → one. *Test:* Catch2 policy decision + fake port; snap-app dialog visual.
- **[APP-009]** The double-click-to-arm affordance shall drive the cockpit-owned armed state (consumes [TRADER_HUD-060]): an arming activation calls Arm() with a visually distinct armed indication, a confirming activation within a configurable window (default 3 s) proceeds, and expiry calls Disarm(); APP holds no separate armed flag. *Accept:* a single activation never submits; expiry invokes Disarm() at 3 s ± 100 ms under an injected clock; the indication tracks the cockpit state. *Test:* Catch2 affordance state machine with fake clock and fake cockpit.
- **[APP-010]** Confirmation and arming settings changes shall take effect on the next submit without restart. *Accept:* switching never→always mid-session routes the very next submit through the dialog. *Test:* Catch2: mutate policy object, assert next-submit path.
- **[APP-083]** One-click/armed mode shall auto-disarm after a configurable inactivity interval by invoking the cockpit Disarm() (consumes [TRADER_HUD-060]). *Accept:* with 30 s configured and an injected clock, 30 s without intent activity triggers exactly one Disarm; activity resets the timer. *Test:* Catch2 fake-clock state machine.
- **[APP-084]** Confirmation policy shall support per-order-type "don't ask again" toggles (Market / Limit / Conditional) with a visible settings summary of the active per-type policies. *Accept:* disabling Market confirmations routes the next market submit dialog-free while a limit submit still confirms; the summary lists all three states. *Test:* Catch2 policy matrix.
- **[APP-085]** Leverage change, margin-mode change, and position-mode change shall each raise a dedicated confirmation dialog that no one-click/armed or don't-ask setting can suppress. *Accept:* with confirmations globally off and armed mode on, a leverage change still raises its dialog; decline sends nothing. *Test:* Catch2 policy cases with fake port.

#### APP: Case 1.4 — Trading hotkeys
Keyboard-first execution with conflict-safe, mode-aware bindings.
- **[APP-011]** A `HotkeyRegistry` shall map configurable key chords to trading/shell actions (buy, sell, cancel-last, panic, safe-mode toggle) and reject duplicate chords naming the holding action. *Accept:* assigning an occupied chord returns an error containing the conflicting action's name; dispatch invokes the mapped handler. *Test:* Catch2 registry assign/dispatch/conflict cases.
- **[APP-012]** Hotkey dispatch shall honor trading mode and confirm/arm policies identically to button paths. *Accept:* in safe mode a trading chord is a no-op that emits a status notice; with policy=always the chord still raises the confirm dialog. *Test:* Catch2 dispatch under mode/policy matrix with fake port.
- **[APP-013]** The elements window shall route key events into the registry so configured chords work application-wide. *Accept:* pressing a configured chord in the running app fires its handler, observable as a status-bar notice. *Test:* manual/visual — no headless key injection exists. *Verify:* demo.
- **[APP-087]** (defer) Hotkey scoping rules (global vs focused panel vs active DOM) with cross-scope conflict detection shall extend the HotkeyRegistry. *Accept:* the same chord bound in two scopes resolves by focus; a true conflict is rejected naming both holders. *Test:* Catch2 registry scope cases.

#### APP: Case 1.5 — Kill-switch / panic control
One-action cancel-all (optional flatten) that is always reachable; enforcement lives in DATA_MODEL.
- **[APP-014]** An always-visible panic control (app/status bar) shall trigger exactly one cockpit cancel-all intent (with optional flatten flag per settings) and show panic-in-progress until the completion callback. *Accept:* activation → one CancelAll(scope=all, flatten per setting) on the fake port; UI blocked in progress state until callback. *Test:* Catch2 fake port call count + state assertions.
- **[APP-015]** Panic shall bypass confirmation and arming policies and remain enabled during user safe mode and resync; it is disabled only when the API key lacks trade permission, with the reason shown. *Accept:* with confirm=always and safe mode on, panic issues its intent with zero dialogs; read-only key renders it disabled with reason text. *Test:* Catch2 mode/policy matrix on panic dispatch.
- **[APP-016]** Panic outcome shall be surfaced: success toast with cancelled/residual counts from the DATA_MODEL result; failure raises a persistent banner until acknowledged. *Accept:* fake result {cancelled:3, residual:1} renders both counts; failure banner survives refresh until dismissed. *Test:* Catch2 view-model; snap-app banner visual.
- **[APP-086]** Panic shall offer scoped variants: current-symbol panic (consumes [DATA_MODEL-088]) and cancel-all-orders-only (consumes [DATA_MODEL-047]), alongside the global flatten. *Accept:* the symbol-scoped control emits one scoped intent naming the active symbol; cancel-orders-only emits no close intents. *Test:* Catch2 fake-port payload assertions.

#### APP: Case 1.6 — Read-only / safe mode
Explicit non-mutating operation with visible cause.
- **[APP-017]** A global safe-mode toggle (menu entry + status indicator) shall drive the DATA_MODEL observation-mode gateway block (consumes [DATA_MODEL-065]) through the cockpit port; `TradingModeModel` mirrors the controller state on every mutating affordance with the reason displayed — APP holds no independent mode flag. *Accept:* toggling on invokes the DATA_MODEL toggle exactly once and disables submit/hotkey dispatch within one refresh of the state echo; reason text rendered. *Test:* Catch2 `TradingModeModel` vs fake controller; snap-app disabled-state visual.
- **[APP-018]** The app shall start in safe mode when no API credentials are configured, displaying reason "no API key". *Accept:* launch without key/env credentials → read-only badge shown, ticket submit disabled. *Test:* Catch2 mode derivation from Config fixture; snap-app badge.
- **[APP-019]** The user-toggled safe-mode state shall persist across sessions via settings write-back. *Accept:* enable → restart → still enabled. *Test:* Catch2 settings struct round trip.

#### APP: Case 1.7 — Order sizing & price helpers
Preset quantities, price pickers, quantity modes, and later sizing tiers for the ticket and click-trading.
- **[APP-071]** A preset-quantity selector (configurable buttons) shall govern click-trading quantity: the active preset value is attached to every DOM/chart prefill intent (consumed by [TRADER_HUD-059]) and shown on the armed indicator. *Accept:* selecting preset 0.5 makes the next chart-click intent carry qty 0.5; presets are editable in settings and persisted. *Test:* Catch2 model + fake intent sink.
- **[APP-072]** The ticket price field shall offer bid/ask/last/mid pickers and tick-size-aware nudge steppers using the instrument decimals (consumes [TRADER_HUD-056]). *Accept:* picking mid fills (bid+ask)/2 snapped to tick; one stepper increment changes the price by exactly one tick. *Test:* Catch2 unit with quote fixture.
- **[APP-073]** Quantity entry shall support three modes — base-coin amount, quote-currency order value, and percent of available balance (consumes [DATA_MODEL-001]) — converted with currency arithmetic. *Accept:* 50% of 1000 USDT available at price 100 yields qty 5 exactly; mode switches preserve the underlying value where representable. *Test:* Catch2 conversion fixtures.
- **[APP-074]** (defer) Risk-based sizing shall derive quantity from stop distance and a fixed money or percent-of-balance risk amount. *Accept:* fixture (entry, stop, risk) triples produce the hand-computed qty exactly. *Test:* Catch2 fixtures.
- **[APP-075]** (defer) A pre-trade preview (order cost / initial margin, estimated fees, estimated liquidation price) and leverage-aware max buy/sell size shall render in the ticket once DATA_MODEL exposes account-margin data. *Accept:* fixture inputs produce the documented formulas' values. *Test:* Catch2 fixtures.

### 4.2 Workspace composition & persistence
Make the panel workspace interactive, self-describing, and durable across restarts.

#### APP: Case 2.1 — Interactive draggable splitters
Turn the static 50/50 divider into a draggable, ratio-preserving splitter.
- **[APP-020]** The split divider shall be draggable, updating the split ratio live with a minimum pane size clamp of 80 px. *Accept:* dragging toward an edge stops at 80 px; neither child ever reaches zero size. *Test:* Catch2 ratio-clamp math; actual drag manual/visual.
- **[APP-021]** The split ratio shall be stored on `SplitPanelNode`, preserved across `ReplaceChild`/rebuild, and included in workspace persistence. *Accept:* after ReplaceChild the ratio is unchanged (not reset to 50/50); ratio round-trips through the workspace file. *Test:* Catch2 layout-model retention + serialization round trip.
- **[APP-022]** Split layout construction shall have a single implementation: `SplitPanelNode::BuildLayout` delegates to the `UiBuilder::Make*Split`/`MakeDivider` helpers (currently dead code) or the helpers are removed. *Accept:* exactly one split-construction path remains; rendered layout unchanged. *Test:* manual code inspection; snap-app render regression. *Verify:* inspection.

#### APP: Case 2.2 — Tab labels, rename, reorder
Tabs that describe and follow their content.
- **[APP-023]** Tab labels shall derive from content ("<PanelType> — <symbol>" for instrument leaves) and update on symbol selection, change-type, and split. *Accept:* selecting BTCUSDT retitles the tab on the next refresh. *Test:* Catch2 label-derivation function; snap-app label visual.
- **[APP-024]** Tabs shall be renameable (context menu or double-click); a user-set name pins the tab and stops auto-derivation. *Accept:* a renamed tab keeps its custom name after symbol change and after restart. *Test:* Catch2 TabModel pinning + persistence; rename gesture manual/visual.
- **[APP-025]** Tabs shall be reorderable and the order persisted. *Accept:* a changed tab order survives restart. *Test:* Catch2 tab-order model + workspace round trip.
- **[APP-026]** `TabBar::onTabSelected` shall be wired to MainWindow so the active tab is tracked for persistence and keyboard shortcuts. *Accept:* the active tab id is recorded in workspace state and restored as active on restart. *Test:* Catch2 active-tab field round trip; snap-app selection visual.

#### APP: Case 2.3 — Layout & session persistence
Everything the user arranged survives restart (gap: nothing persists today).
- **[APP-027]** Full workspace state — tab list (order, names, active), per-tab split trees (direction, ratio), leaf panel types, selected symbols, and chart preferences (candle period), watchlist contents and sort order, and per-panel settings (DOM grouping level and centering mode, tape filter and aggregation settings) — shall serialize via Glaze to `<data-dir>/workspace.json` on exit and debounced on layout mutation. *Accept:* a 2-tab/3-panel session produces a file that deserializes to an identical `WorkspaceState`. *Test:* Catch2 WorkspaceState serialize/deserialize round trip.
- **[APP-028]** On startup the workspace file shall be restored (tabs, splits, panel types, symbols recreated); a missing or corrupt file falls back to the default single tab without crashing. *Accept:* restart reproduces tab count, split structure, and symbols; a truncated file yields the default layout plus a logged warning. *Test:* Catch2 restore-plan builder incl. corrupt fixtures; snap-app end-to-end.
- **[APP-029]** Workspace and settings writes shall be atomic (temp file + rename) and carry a schema version; an unknown newer version leaves the file untouched and starts with defaults. *Accept:* interrupting between temp write and rename leaves the previous file intact and parseable. *Test:* Catch2 injected-failure atomicity test.
- **[APP-079]** (defer) Persistence shall extend to chart drawings and indicator instance settings once [TRADER_HUD-018]..[TRADER_HUD-020] land. *Accept:* a drawn level and an MA(9) instance survive restart. *Test:* Catch2 round trip.

#### APP: Case 2.4 — Settings dialog
One place to configure profiles, safety, display, and risk limits.
- **[APP-030]** A settings dialog reachable from the menu shall expose sections for API profile (hosts, key/secret), trading safety (confirmation policy, arm window, safe mode), display (theme, density), and risk limits (values forwarded to DATA_MODEL enforcement). *Accept:* every listed control is bidirectionally bound to a `SettingsModel` field. *Test:* Catch2 SettingsModel binding; snap-app dialog visual.
- **[APP-031]** Settings shall persist to a Glaze-serialized `settings.json` (distinct from the read-only CLI11 launch config) and apply without restart where feasible (theme, confirmations, safety); connectivity changes are flagged restart-required. *Accept:* a confirm-policy edit affects the next submit in the same session; the file is updated atomically. *Test:* Catch2 round trip + live-apply of policy change.
- **[APP-032]** The API secret shall be masked in the dialog and never written to any app-layer log or export. *Accept:* after a session including auth and a settings edit, captured log output contains no secret substring. *Test:* Catch2 log-sanitizer helper; dialog masking manual/visual.

#### APP: Case 2.5 — Themes & display density
Replace scattered hardcoded rgba constants with configurable theming.
- **[APP-033]** A central `Theme` struct shall supply the full palette consumed by UiBuilder, TabBar, and PanelNode (removing duplicated rgba constants) with at least dark and light themes selectable in settings. *Accept:* switching theme recolors app bar, tabs, and panel chrome on next redraw; no literal rgba constants remain in those three files. *Test:* Catch2 theme lookup table; snap-app per-theme screenshots.
- **[APP-034]** Density modes (normal/compact) shall scale fonts and paddings from theme metrics. *Accept:* compact scales header heights and font sizes by the documented factor (0.8) with no clipped labels in the snapshot. *Test:* Catch2 metric computation; snap-app visual.
- **[APP-035]** The theme shall expose semantic buy/sell (up/down) colors consumed by app-layer widgets (ticket side toggle, submit tint). *Accept:* changing the up-color in settings retints the Buy control on redraw. *Test:* Catch2 semantic-color lookup; snap-app visual.
- **[APP-076]** Settings shall establish a display timezone (default local) and locale-aware number formatting (decimal separator, thousands grouping) applied by every timestamp/number formatter and by exports (consumed by [APP-062]). *Accept:* switching the timezone shifts rendered history timestamps accordingly on redraw; the chosen number format applies to prices and totals. *Test:* Catch2 formatting-function fixtures.
- **[APP-077]** (defer) Per-table column show/hide and reorder shall be configurable and persisted per panel. *Accept:* a hidden column stays hidden after restart; a reordered column order round-trips. *Test:* Catch2 settings round trip.
- **[APP-078]** (defer) A reduced-motion option shall damp flash/animation intensity for high-frequency updates. *Accept:* with the option on, update flashes are disabled while values still change. *Test:* Catch2 model flag; snap-app visual.

#### APP: Case 2.6 — Advanced workspace (defer)
Symbol-link groups, named shareable layouts, detachable windows — later maturity stage.
- **[APP-036]** (defer) Panels shall join color-coded symbol-link groups so instrument selection propagates within a group. *Accept:* changing the symbol in one linked panel retargets all group members and no unlinked panel. *Test:* Catch2 link-group controller with fake leaves.
- **[APP-037]** (defer) Named layouts shall be savable/loadable and exportable/importable as files. *Accept:* an exported layout imported on a fresh profile reproduces an equal `WorkspaceState`. *Test:* Catch2 export/import round trip.
- **[APP-038]** (defer) A panel subtree shall be detachable into a separate OS window and re-attachable on close. *Accept:* detach opens a second elements window hosting the subtree; closing it restores the subtree to the main window. *Test:* manual/visual — multi-window is not headless-verifiable. *Verify:* demo.

### 4.3 Status & feedback
Make connectivity, errors, and pending states visible instead of silent.

#### APP: Case 3.1 — Status bar & connection state
Persistent per-stream connection visibility fed through the datahub pipeline.
- **[APP-039]** A persistent status bar shall show per-stream badges (public WS, private WS, REST) with connected/reconnecting/stale states, driven by a subscription to a cockpit-exposed connectivity-status feed, using the TRADER_HUD presentation vocabulary connected | reconnecting | stale as the single source (consumes [TRADER_HUD-068]); APP defines no independent states. *Accept:* a fake feed event flips the corresponding badge within 500 ms of the callback. *Test:* Catch2 `StatusBarModel` vs fake feed; snap-app badge visual.
- **[APP-040]** While reconnecting, the status bar shall show the attempt count and elapsed time. *Accept:* three fake reconnect attempts render "#3" with a nonzero elapsed value. *Test:* Catch2 model formatting with fake clock.
- **[APP-041]** A stale/desynced private stream shall force mutation lockout via `TradingModeModel` with the cause named in the status bar. *Accept:* injected stale event disables submit; recovery re-enables within one refresh. *Test:* Catch2 mode-transition cases.
- **[APP-066]** The status bar shall show a rate-limit pressure indicator driven by the DATA_MODEL budget state (consumes [DATA_MODEL-038]/[DATA_MODEL-040]) with a distinct throttled state while a 10006/403 backoff is active. *Accept:* a fake budget event at ≥80% utilization renders the pressure state; a backoff event renders the throttled state until its reset timestamp. *Test:* Catch2 `StatusBarModel` cases; snap-app visual.
- **[APP-067]** A persistent, distinctly-accented environment banner shall render whenever the active profile environment is testnet (consumes [DATA_MODEL-023]); mainnet renders none. *Accept:* a testnet profile shows the banner regardless of the active tab; a mainnet profile shows none. *Test:* Catch2 model derivation; snap-app visual per environment.
- **[APP-080]** Per-stream status badges shall include the measured transport latency (consumes [CORE-055]) refreshed per heartbeat. *Accept:* a fake latency value of 42 ms renders "42 ms" on the matching badge and updates on the next event. *Test:* Catch2 model formatting vs fake feed.

#### APP: Case 3.2 — Error surfacing
Transient toasts and blocking dialogs replacing stderr-only reporting.
- **[APP-042]** A toast queue shall present transient errors/notifications with auto-dismiss (default 5 s), at most 3 visible, and an overflow counter. *Accept:* 5 injected errors show 3 toasts plus "+2"; each dismisses at 5 s ± 0.5 s under injected clock. *Test:* Catch2 `ToastQueueModel` with fake clock; snap-app visual.
- **[APP-043]** Fatal startup errors (invalid config, DB open failure) shall show a modal dialog with the error detail before exiting (today stderr-only). *Accept:* an invalid `--data-dir` produces a visible dialog; exit code remains -1. *Test:* manual/visual — startup path precedes any harness. *Verify:* demo.
- **[APP-044]** Every surfaced error shall carry a source tag and human-readable text produced by a mapping table over exceptions and exchange retCodes (codes supplied by DATA_MODEL). *Accept:* retCode 10006 maps to a rate-limit message tagged source "REST". *Test:* Catch2 mapping-table cases.

#### APP: Case 3.3 — Loading timeout & retry
Kill the perpetual "Loading..." state (gap: no timeout or retry today).
- **[APP-045]** Waiting indicators shall transition to an error state with a Retry control after a timeout (default 10 s), and Retry shall re-fire the pending operation (instrument-list subscribe / chart install). *Accept:* with instruments never arriving, the retry state appears at 10 ± 1 s; Retry issues a new subscribe call. *Test:* Catch2 `LoadingStateModel` with fake clock + fake cockpit.
- **[APP-046]** Retries shall be repeatable without leaking cockpit registrations or feed subscriptions. *Accept:* three retries produce three subscribe calls but at most one live subscription in the cockpit registry. *Test:* Catch2 counting fake cockpit registry across retries.

#### APP: Case 3.4 — Clock-skew warning
Surface clock drift that endangers signed requests.
- **[APP-047]** A warning banner shall render whenever the DATA_MODEL clock-drift session warning event (consumes [DATA_MODEL-037], surfaced via [TRADER_HUD-091]) is active, naming the measured offset and its signing consequence; APP applies no independent threshold. *Accept:* an injected drift warning event carrying 3000 ms shows a banner containing "3000"; with no active event no banner shows. *Test:* Catch2 banner-model event cases.
- **[APP-048]** The banner shall be dismissible; once dismissed it stays hidden for the session unless the drift warning clears and a new warning event fires. *Accept:* dismissed during a continuing warning it stays hidden; after the event clears and re-fires, the banner re-shows. *Test:* Catch2 dismissal state machine.

#### APP: Case 3.5 — Alerts & notification center
UI for the CORE-side alert engine plus a browsable notification log.
- **[APP-049]** A notification-center PanelType shall list order-lifecycle, connectivity, and alert events newest-first with a severity filter over a bounded ring buffer (500 entries). *Accept:* the 501st entry evicts the oldest; the severity filter hides lower-severity rows. *Test:* Catch2 `NotificationLogModel` eviction + filter.
- **[APP-050]** Alert firings from the CORE engine feed shall produce a toast and a center entry; a global mute toggle suppresses toasts but not entries. *Accept:* while muted, a fired alert yields one center entry and zero toasts. *Test:* Catch2 with fake alert feed.
- **[APP-068]** An alert manager panel shall create, edit, enable/disable, and delete price-level alerts (cross above/below) backed by the CORE edge-trigger engine (consumes [CORE-027]/[CORE-028]) and persisted in settings. *Accept:* creating an alert registers exactly one trigger; disabling stops firings without deleting; alerts are restored after restart. *Test:* Catch2 `AlertManagerModel` vs fake engine; snap-app visual.
- **[APP-069]** (defer) Percent-move-over-window, funding-rate-threshold, and big-trade/volume-spike alert types shall extend the alert manager (big-trade events sourced from the tape hooks, consumes [TRADER_HUD-029]/[CORE-027]). *Accept:* fixture feeds fire each type exactly at its threshold. *Test:* Catch2 vs fake feeds.
- **[APP-070]** (defer) Alert sounds with per-category sound selection shall extend notifications; the global mute ([APP-050]) suppresses them. *Accept:* each category triggers its configured sound id; mute silences all. *Test:* Catch2 model dispatch (sound playback manual/visual). *Verify:* demo.

#### APP: Case 3.6 — Log viewer panel
In-app viewer over the CORE logging facility with severity filter and search.
- **[APP-065]** An in-app log viewer panel shall present the CORE logging facility's sink output (consumes [CORE-048]) with severity filter and substring search over a bounded buffer. *Accept:* with injected mixed-severity lines, filter=warn shows only warn+error rows; a search string reduces rows to exactly the matches. *Test:* Catch2 `LogViewerModel`; snap-app visual.

### 4.4 Shell hygiene & keyboard access
Close known lifecycle defects and make the shell testable and keyboard-usable.

#### APP: Case 4.1 — Panel registration hygiene
Fix panel_id 0 hardcoding and missing unregistration (documented gaps).
- **[APP-051]** Generic leaves shall register a ContentPanel with `TradeCockpit::RegisterPanel` and receive a unique nonzero panel_id, replacing the hardcoded `0` at `main_window.cpp:174`. *Accept:* two generic leaves report distinct nonzero ids visible in the cockpit registry. *Test:* Catch2 vs real TradeCockpit with stub ContentPanels.
- **[APP-052]** Leaves shall hold the RAII registration handle returned by TradeCockpit registration (consumes [TRADER_HUD-064]); `HandleClose`, `HandleChangeType`, and leaf destruction drop the handle so registry entries and per-registration feed subscriptions are released — no explicit unregister call exists on the cockpit surface. *Accept:* a register→close cycle returns registry size and feed subscriber count to the pre-registration baseline. *Test:* Catch2 lifecycle vs TradeCockpit with fake data controller.
- **[APP-053]** Instrument-leaf close/change-type shall destroy the chart element deterministically via the deferred-destruction path as part of dropping the leaf's registration handle ([APP-052]); no separate manual uninstall call remains for callers. *Accept:* a weak_ptr to the chart element expires within one event-loop turn of close. *Test:* Catch2 weak_ptr expiry where linkable; snap-app regression otherwise.

#### APP: Case 4.2 — Menu completeness
A menu that reaches every shell function (today: Exit + no-op About).
- **[APP-054]** The About entry shall open a dialog showing application name, version (identical to `--version` output), and license. *Accept:* the dialog version string equals `--version` stdout. *Test:* manual/visual — dialog content inspection. *Verify:* demo.
- **[APP-055]** The menu shall gain Settings…, Save layout, Safe-mode toggle, and Panic entries, and the hamburger deck shall restore to the tab strip after any action fires (fixes unused `mMenuVisible`). *Accept:* each entry invokes its handler and the app bar returns to the tab strip. *Test:* Catch2 menu-action dispatch table; snap-app app-bar state.

#### APP: Case 4.3 — Keyboard navigation basics
Minimal keyboard control of the shell.
- **[APP-056]** Shell shortcuts Ctrl+1..9 (select tab N), Ctrl+W (close active tab), and Ctrl+T (new tab) shall route through the HotkeyRegistry. *Accept:* each chord resolves to its action in the registry and the running app responds equivalently to the mouse path. *Test:* Catch2 chord→action mapping; end-to-end manual/visual.
- **[APP-057]** Escape shall close the topmost dialog/menu, and the focused control shall show a visible focus indication. *Accept:* Esc dismisses the settings dialog; a focus ring is visible in a screenshot. *Test:* manual/visual — focus traversal needs real input. *Verify:* demo.
- **[APP-088]** (defer) A keyboard-only order-ticket flow (focus traversal through every ticket control, submit via keyboard) shall be supported. *Accept:* a scripted key sequence composes and submits a valid order with no pointer event. *Test:* Catch2 model-level traversal; end-to-end manual/visual. *Verify:* demo.

#### APP: Case 4.4 — Testable shell decomposition
The structural enabler for every Catch2 test in this branch.
- **[APP-058]** All view-models/controllers introduced by this branch (OrderTicketModel, ConfirmationPolicy, HotkeyRegistry, TradingModeModel, StatusBarModel, ToastQueueModel, LoadingStateModel, TabModel, WorkspaceState, Theme, CsvExporter, NotificationLogModel, AlertManagerModel, LogViewerModel) shall live in elements-free translation units compiled into a static library linked by both `xcockpit` and Catch2 executables under `test/app/`. *Accept:* the `test_app_models` executable builds and passes without linking cycfi/elements. *Test:* Catch2 build gate: test target compiles and runs green.
- **[APP-059]** The duplicated `findParent` DFS in `ReplaceNode` and `HandleClose` shall be extracted into one shared tree-search utility over the PanelNode composite. *Accept:* a single implementation remains; close/collapse-to-sibling scenarios behave identically. *Test:* Catch2 on extracted utility with stub nodes; snap-app regression.

### 4.5 History & export UI
Give persisted order/execution history a viewer and a way out of the app.

#### APP: Case 5.1 — History table panel shell
Table shell for Orders/TradeHistory PanelTypes over cockpit history feeds (data from DATA_MODEL).
- **[APP-060]** `PanelType::Orders` and `PanelType::TradeHistory` shall route to a thin elements grid rendering the TRADER_HUD view models (consumes [TRADER_HUD-043]/[TRADER_HUD-044], [TRADER_HUD-051]/[TRADER_HUD-053]) — scroll, column layout, and row virtualization only; sorting, filtering, and row content are cockpit-owned. *Accept:* 100 fake view-model rows render in a scrollable table; a view-model filter change re-renders exactly the model's row set. *Test:* Catch2 grid-adapter binding; snap-app table rendering.
- **[APP-061]** View-model row changes driven by feed increments ([TRADER_HUD-044]) shall update grid rows in place (newest-first default) without a full table rebuild. *Accept:* one increment adds exactly one row at the top; existing row widgets are untouched. *Test:* Catch2 grid-adapter increment handling vs fake view model.

#### APP: Case 5.2 — CSV export
Export the visible history slice for journaling and taxes.
- **[APP-062]** An Export CSV action on history panels shall write the currently filtered rows honoring configured timezone and number format, with a header row recording the export parameters (symbol, range, generated-at). *Accept:* the file parses as CSV, data row count equals the filtered count, and the header contains symbol and range. *Test:* Catch2 `CsvExporter` golden-file comparison.
- **[APP-063]** Exports shall write atomically (temp + rename) to a user-chosen path defaulting to `<data-dir>/exports`, surfacing failures as an error toast with no partial file left behind. *Accept:* an unwritable directory produces a toast and no file. *Test:* Catch2 atomicity/failure injection; file dialog manual/visual.
- **[APP-081]** History grids shall support copy-row and copy-table-selection to the clipboard as TSV in displayed column order, honoring the [APP-076] formats. *Accept:* copying 3 selected rows yields 3 TSV lines whose cells equal the rendered values. *Test:* Catch2 clipboard-string builder; paste manual/visual.
- **[APP-082]** (defer) A JSON export variant of [APP-062] shall write the same filtered rows with the same parameter header. *Accept:* the JSON parses and its row count equals the filtered count. *Test:* Catch2 golden-file.

### 4.6 Account & balances display

#### APP: Case 6.1 — Balances panel & account header
Rendering of the TRADER_HUD wallet and account-header view models in the shell.
- **[APP-064]** A Balances panel (ContentPanel, `PanelType::Balances` — new enum value added alongside Empty/MarketGraph/OrderBook/Orders/TradeHistory/NewOrder/TradeStats/Positions/Watchlist) shall render the TRADER_HUD wallet view model (consumes [TRADER_HUD-077]/[TRADER_HUD-078]) through the thin table shell, and the status/app bar shall render the account-header values (consumes [TRADER_HUD-079]). *Accept:* fake wallet rows render with the small-balance filter toggle working; the header shows the fixture equity/available/session-PnL values. *Test:* Catch2 model-to-widget binding; snap-app visual.
## 5. Verification & requirements infrastructure (INFRA)
The suite today is 16 manually-run Catch2 v3 binaries with no CTest registration (a custom CMake function `add_test` at CMakeLists.txt:229 shadows the built-in), roughly seven live-exchange network cases mixed into the offline set, five `sleep_for(100ms)` synchronization points, file-static order-dependent fixtures, SQLiteCpp assertion boilerplate duplicated in four files, and no requirements-to-test traceability. This branch turns verification into an enforceable process: a one-command offline CTest run, deterministic async helpers in a shared test-support library, an offline mock ByBit V5 harness, render snapshot regression, sanitizer and parser-robustness jobs, and a Doorstop tree in `req/` whose every normative leaf is provably covered by a tagged Catch2 TEST_CASE and frozen by a user-approved review workflow. It is cross-cutting: every other branch's requirements become mechanically verifiable through this infrastructure.

### 5.1 Test runner & CTest integration
One command builds and runs the whole offline suite through CTest with per-TEST_CASE granularity.

#### INFRA: Case 1.1 — CTest registration
Un-shadow CMake's built-in `add_test` and register every Catch2 case with CTest.
- **[INFRA-001]** The custom test-factory function in root CMakeLists.txt (currently named `add_test`) shall be renamed (e.g. `xc_add_test`) and the project shall call `include(CTest)`/`enable_testing()` so CMake's built-in test registration works again. *Accept:* `ctest -N` in the build dir lists >0 tests; no in-project CMake function named `add_test` remains. *Test:* CI step: `ctest -N` exits 0 with non-zero count.
- **[INFRA-002]** Every Catch2 test executable shall be registered via Catch2's `catch_discover_tests` so each TEST_CASE is an individual CTest entry. *Accept:* `ctest -N` entry count equals the number of TEST_CASE occurrences found by a source scan of test/ (scripts/count_test_cases.py) — asserting per-case discovery, not a fixed number. *Test:* CI compares the `ctest -N` count against the scripted TEST_CASE count.
- **[INFRA-003]** An aggregate target (`cmake --build <dir> --target check`) shall build all test executables and run `ctest --output-on-failure` for the offline profile, documented in test/README.md as the canonical invocation. *Accept:* one command from a clean configure exits 0 having executed every offline test. *Test:* CI runs the target on clean checkout.

#### INFRA: Case 1.2 — Offline/online tag discipline
Deterministic offline tests are separable from live-exchange tests by tags and CTest labels.
- **[INFRA-004]** Tag convention (written into test/README.md): every TEST_CASE contacting a live exchange endpoint shall carry `[live]`; every multi-component in-process test shall carry `[integration]`. *Accept:* `scripts/check_tags.py` finds zero test files referencing live hosts (`api.bybit.com`, `stream.bybit.com`) whose cases lack `[live]`. *Test:* scripts/check_tags.py static scan in CI.
- **[INFRA-005]** `[live]`-tagged cases shall be excluded from the default offline CTest profile (via discovery `TEST_SPEC "~[live]"` plus a labeled `live` discovery set). *Accept:* the documented offline invocation passes inside a no-network sandbox (`unshare -n` or equivalent) and executes no `[live]` case. *Test:* CI offline job runs suite under network-namespace isolation.
- **[INFRA-006]** (defect fix) Existing untagged/undertagged files shall be retro-tagged: all of test_currency, test_buoy_candle, test_http_query, plus `[live]` on the network cases in test_http_query, test_websocket, test_data_manager and the orderbook capture case. *Accept:* `ctest -L live -N` lists exactly the network-dependent cases (~7); `ctest -LE live` passes offline. *Test:* CI offline job + live-label listing snapshot.

### 5.2 Deterministic async test support
A shared test-support library removes duplicated fixture boilerplate and sleep-based flakiness.

#### INFRA: Case 2.1 — Shared test-support library
One linkable test/support library replaces per-file fixture boilerplate.
- **[INFRA-007]** A `test_support` static library under test/support/ shall own the SQLiteCpp `assertion_failed` handler, deleting the four verbatim copies in test_dao.cpp, test_data_sink.cpp, test_data_model.cpp, test_data_manager.cpp. *Accept:* `grep -rl assertion_failed test --include=*.cpp` matches only test/support/. *Test:* CI grep gate + build of all test targets.
- **[INFRA-008]** test_support shall provide the standard RAII database fixture (in-memory SQLite + scheduler + connect context) constructed per TEST_CASE. *Accept:* test_dao, test_data_sink, test_data_model define no local scheduler/DB setup structs of their own and stay green. *Test:* build + existing suites pass via ctest.

#### INFRA: Case 2.2 — Deterministic completion waiting
Replace sleep-based synchronization with predicate waits.
- **[INFRA-009]** test_support shall provide `wait_for_condition(predicate, timeout, description)` (condition-variable or polled latch), and the five existing `std::this_thread::sleep_for(100ms)` sync points (test_data_sink ×3, test_data_model, bybit test_orderbook) shall be replaced with it. *Accept:* `grep -rn sleep_for test/` excluding test/support returns zero hits; offline suite passes `ctest --repeat until-fail:20`. *Test:* CI grep gate + repeat-until-fail stress job.
- **[INFRA-010]** On timeout the helper shall FAIL the test reporting the description and elapsed time, never hang or silently pass. *Accept:* a never-true predicate with 1 s timeout fails in <2 s with the description in the message. *Test:* Catch2 self-test of the helper.

#### INFRA: Case 2.3 — Order-independent fixtures
Kill file-static global fixtures so every case passes alone and in any order.
- **[INFRA-011]** (defect fix) File-static global fixtures in test_data_sink, test_data_model, test_data_manager shall become per-TEST_CASE fixtures; test_data_sink's accumulated row-count assertions (60→120→126) shall assert only against state each section itself seeded. *Accept:* every TEST_CASE passes when executed alone via name filter. *Test:* per-case CTest entries from INFRA-002 run each case in its own process.
- **[INFRA-012]** The offline CI job shall run each test binary with Catch2 randomized case order (`--order rand --rng-seed <logged>`). *Accept:* 10 consecutive randomized runs green; seed printed for reproduction. *Test:* CI stress job with randomized order.

### 5.3 Mock exchange harness
An offline mock ByBit V5 server enables connect/datahub integration tests without the live exchange.

#### INFRA: Case 3.1 — In-process mock ByBit server
Loopback WS+HTTP server scriptable with canned V5 responses.
- **[INFRA-013]** test_support shall provide an in-process mock ByBit V5 server on a loopback ephemeral port exposing a WebSocket stream endpoint and HTTP REST endpoints, scriptable with canned responses per topic/path. *Accept:* connect's `websock_connection` and `http_query` complete a subscribe→snapshot→delta round-trip against it with zero external network. *Test:* Catch2 integration in test/connect, offline.
- **[INFRA-014]** connect/data-manager configuration shall accept endpoint and TLS overrides (host, port, plaintext or test-CA) so mock tests exercise production code paths unmodified. *Accept:* no test-only conditional compilation in src/ (`grep -rn "ifdef.*TEST" src/` empty); mock tests link the same connect code as xcockpit. *Test:* grep gate + mock integration test green.
- **[INFRA-015]** Mock server lifetime shall be RAII per fixture with OS-assigned ports so mock-using tests run concurrently. *Accept:* `ctest -j8` over all mock tests passes 10 consecutive runs with no port-conflict failures. *Test:* parallel ctest CI run.

#### INFRA: Case 3.2 — Recorded payload replay
Captured V5 wire data becomes replayable scenario scripts.
- **[INFRA-016]** The mock shall replay recorded scenario files (JSONL: direction, relative time, payload) seeded from the existing 16 WS + 3 HTTP captured ByBit samples. *Accept:* replaying the captured publicTrade scenario through dispatcher→adapter→sink yields the same DB row counts as current test_data_sink expectations. *Test:* Catch2 integration reusing existing captures, offline.
- **[INFRA-017]** A `[live]`-tagged capture utility test shall record fresh scenarios from the real exchange into the scenario format. *Accept:* record→replay round-trip delivers a byte-identical frame sequence. *Test:* round-trip check on a locally recorded fixture.

#### INFRA: Case 3.3 — Fault-injection scenarios
Scripted transport and protocol faults for resilience testing by other branches.
- **[INFRA-018]** The mock shall inject scripted faults at a chosen frame index: hard connection drop, malformed JSON frame, WebSocket close with a given code, delayed or failed auth response. *Accept:* each fault type triggerable and observed by a raw beast client in a harness self-test. *Test:* per-fault harness self-tests, offline.
- **[INFRA-019]** The mock orderbook scenario script shall control update-id sequencing to express a gap (skipped seq) and resync (fresh snapshot served on re-request or resubscribe). *Accept:* self-test receives the gap frame then a snapshot on subsequent snapshot request. *Test:* harness self-test, offline.

### 5.4 Render regression
ThorVG output is guarded by golden-image comparison instead of human-only inspection.

#### INFRA: Case 4.1 — ThorVG snapshot comparison
Golden PNG comparison with tolerance, diff artifacts, and regeneration mode.
- **[INFRA-020]** test_support shall provide golden-image comparison of ThorVG/Cairo buffers against PNGs under test/render/golden/ with tolerance: fail if >0.5% of pixels differ by >2/255 in any channel. *Accept:* a 1-pixel stroke shift fails; an identical re-render passes on the reference (CI Linux) platform. *Test:* comparator self-test with synthetic renders.
- **[INFRA-021]** On mismatch the comparator shall write actual and diff heat-map images under `${CMAKE_BINARY_DIR}/test_output/` and name all three paths in the failure message. *Accept:* a forced failure produces the golden/actual/diff triple at the reported paths. *Test:* Catch2 self-test asserts artifact files exist.
- **[INFRA-022]** A regeneration mode (`SNAPSHOT_UPDATE=1` env) shall rewrite goldens instead of failing. *Accept:* failing test rerun with the flag updates the PNG; the next plain run passes. *Test:* self-test against a temp golden dir.
- **[INFRA-023]** test_time_ruler's nine zoom-level PNG series shall gain snapshot assertions (today they are emitted for human inspection only). *Accept:* an injected tick-label offset regression turns `ctest` red; vision-loop PNGs still emitted. *Test:* snapshot comparison per zoom level; one-time mutation check.

#### INFRA: Case 4.2 — Availability skip reporting
Missing ThorVG/Cairo degrades render tests to visible skips, never silent absence.
- **[INFRA-024]** (defect fix) When `THORVG_AVAILABLE` or `CAIRO_FOUND` is false, render tests shall still register with CTest and report as Skipped (DISABLED property or skip return code), instead of being silently unbuilt. *Accept:* ctest summary on a ThorVG-less configure shows ≥2 skipped render tests. *Test:* CI configure without ThorVG asserts skip count.
- **[INFRA-025]** Configure shall emit one status line naming the missing dependency whenever render tests are degraded. *Accept:* CMake configure log contains `render tests skipped: <dep>` exactly when degraded, absent otherwise. *Test:* CI log grep in both configurations.

### 5.5 Sanitizers & parser robustness
Memory/thread safety and external-JSON robustness are continuously enforced.

#### INFRA: Case 5.1 — Sanitizer jobs
ASan/UBSan and TSan configurations run the offline suite in CI.
- **[INFRA-026]** CMake presets shall provide `asan` (Address+UB) and `tsan` configurations building the full test suite with clang sanitizers. *Accept:* `cmake --preset asan` then offline ctest completes; same for tsan, which shall cover the async pipeline and mock-server tests. *Test:* two CI jobs run the offline suite under each sanitizer.
- **[INFRA-027]** Sanitizer jobs shall fail on any report (`halt_on_error=1`, non-zero exit propagated through ctest). *Accept:* a deliberately injected use-after-free in a scratch test turns the asan job red; its removal restores green. *Test:* one-time seeded-defect verification, then permanent CI gate.

#### INFRA: Case 5.2 — Parser fuzz & property tests
External-exchange JSON parsing never crashes on hostile input.
- **[INFRA-028]** libFuzzer targets under test/fuzz/ shall cover the Glaze deserialization entry points for external JSON (WS envelope, publicTrade batch, orderbook delta, instrument-info REST), seeded from the captured-sample corpus. *Accept:* each target runs 60 s in CI (ASan-instrumented) with zero crashes or reports. *Test:* CI fuzz smoke job with fixed time budget.
- **[INFRA-029]** Catch2 property tests shall assert parsers return error results — never throw uncaught or corrupt state — for all truncation lengths and ≥1000 random single-byte mutations of every captured valid frame. *Accept:* zero crashes; every invalid input yields an error result, every valid input still parses. *Test:* Catch2 GENERATE-driven property tests, offline.

### 5.6 Requirements infrastructure (Doorstop)
Requirements live in-repo as a Doorstop tree with mechanical test traceability.

#### INFRA: Case 6.1 — Doorstop document tree
req/ holds the requirement documents with test-file references.
- **[INFRA-030]** The repo shall contain `req/` with a Doorstop tree: a root PRODUCT document and one child document per requirements branch prefix (CORE, DATA_MODEL, TRADER_HUD, APP, INFRA), items holding the normative leaf texts of this plan. *Accept:* `doorstop` validation passes on fresh checkout; every branch prefix resolves to a document. *Test:* CI doorstop validation step.
- **[INFRA-031]** Every normative leaf item shall carry ≥1 Doorstop `references:` entry of keyword type pointing at a test source file with keyword `[<PREFIX>-<nnn>]`. *Accept:* `doorstop --error-all` passes; deleting a referenced keyword from the test file makes validation fail. *Test:* CI doorstop step + one seeded-violation check.

#### INFRA: Case 6.2 — Test coverage gate
Every normative leaf is provably covered by a tagged Catch2 test.
- **[INFRA-032]** `scripts/check_req_coverage.py` shall assert every normative leaf item is referenced by ≥1 Catch2 TEST_CASE carrying its `[<PREFIX>-<nnn>]` tag (static scan of test/ cross-checked against the Doorstop tree). *Accept:* exits non-zero listing uncovered item IDs; exits 0 when coverage is complete. *Test:* script unit tests against a fixture tree with one uncovered item.
- **[INFRA-033]** Items not verifiable by automated test shall carry an explicit `verify: inspection` (or `demo`) attribute exempting them from the coverage gate; the script shall print the exemption list. *Accept:* only attributed items are exempt; exemptions enumerated in script output. *Test:* script unit test with an attributed fixture item.

#### INFRA: Case 6.3 — CI gate pipeline
One entrypoint chains build, offline tests, and all requirement checks.
- **[INFRA-034]** A single gate entrypoint (`ci/gate.sh`, used verbatim by CI and locally) shall run in order: build, offline ctest, `doorstop --error-all`, `scripts/check_req_coverage.py`, `scripts/check_frozen_tests.py` ([INFRA-035]), and the layering include-audit ([INFRA-040]), failing fast on the first red step. *Accept:* seeding one violation of each active kind produces a red gate naming the failing step. *Test:* one-time seeded verification per step, then permanent CI use.
- **[INFRA-035]** `scripts/check_frozen_tests.py` shall re-hash every reviewed item's referenced test files and fail when content differs from the hash recorded at review time (closing the verified gap: doorstop does not re-check stored SHAs). Phase 0 tooling, live in the gate from the start — not deferred: the script already ships and every subsequent phase's freeze workflow depends on it catching silent edits to reviewed tests. *Accept:* editing a referenced test after review stamping exits non-zero naming item and file; unreviewed items are ignored. *Test:* script unit test against a tmp fixture tree.
- **[INFRA-040]** The gate pipeline ([INFRA-034]) shall include a layering include-audit step failing when any `src/datahub` or `src/connect` source includes a `data/bybit` path (venue confinement per CONTRIBUTING layering; ownership moved here from the retired DATA_MODEL-069). *Accept:* seeding a bybit include into src/datahub turns the gate red naming the offending file; the current tree passes. *Test:* script unit test + one-time seeded verification.

### 5.7 Freeze workflow
Approved requirements and their tests are frozen; changes require explicit user re-approval.

#### INFRA: Case 7.1 — Review stamping on approval
User approval stamps the item and freezes its test references.
- **[INFRA-036]** On explicit user approval, `doorstop review <ID>` shall be run and committed, stamping the referenced-test-file hash checked by [INFRA-035]. *Accept:* after review, the full gate is green; the item YAML shows the reviewed fingerprint. *Test:* scripted round-trip in a tmp fixture repo.
- **[INFRA-037]** `req/README.md` shall document the lifecycle draft → approved (review+stamp) → modified (suspect) → re-approved (clear+review), stating that only the user authorizes review and clear. *Accept:* the doc enumerates all states with the exact commands. *Test:* manual/inspection — doc review. *Verify:* inspection

#### INFRA: Case 7.2 — Suspect and unreviewed states block CI
The gate stays red until user-approved clearing.
- **[INFRA-038]** The gate shall fail while any item is unreviewed or any link/reference is suspect, until `doorstop clear`/`doorstop review` (user-approved) lands. *Accept:* editing an approved item's text turns the gate red; after clear+review it is green again. *Test:* fixture-repo scripted state-transition test.
- **[INFRA-039]** (defer) `check_frozen_tests.py` shall reject self-approving changes: a single commit may not both modify a reviewed item or its referenced test and re-stamp/clear it. *Accept:* one commit containing both the edit and the re-stamp fails the gate; the same change split into two commits passes. *Test:* script unit test on a fixture git history.
## 6. Domain coverage map

Column 2 cites the owning branch section(s) (§1 CORE, §2 DATA_MODEL, §3 TRADER_HUD, §4 APP, §5 INFRA) and representative leaf IDs; it is not exhaustive of every leaf touching a feature. "Deferred" leaves are conscious (defer)-marked leaves that register the case for this map per `directives.frame.deferred_register`; they are not silent drops.

| Domain | Feature | Covering section(s) / representative IDs | Status |
|---|---|---|---|
| Market Data Views | Watchlist / Market Watch | §3 TRADER_HUD-036–042, §2 DATA_MODEL-073 | partial — named lists/pinned/category filter (TRADER_HUD-088) and mark-index/mini-stats/sparkline (TRADER_HUD-089) deferred |
| Market Data Views | Order Book / DOM | §3 TRADER_HUD-021–028, TRADER_HUD-084 | partial — order-flow analytics tier incl. big-print highlight, heatmap, iceberg, bubbles (TRADER_HUD-029, TRADER_HUD-095) wholly deferred |
| Market Data Views | Time & Sales (tape) | §3 TRADER_HUD-030–035 | partial — big-trade/volume-spike alert type consuming tape hooks deferred (APP-069) |
| Market Data Views | Charts with indicators | §3 TRADER_HUD-001–017, TRADER_HUD-087 | partial — indicators (TRADER_HUD-018/019), drawing tools (TRADER_HUD-020), advanced tier/crosshair-sync/price-source-toggle (TRADER_HUD-094/096/097) deferred; multi-chart grid itself covered via APP tab/split composition (APP-020–026), only crosshair sync deferred |
| Market Data Views | Instrument info & funding | §3 TRADER_HUD-056/057, §2 DATA_MODEL-004/005, DATA_MODEL-014/015 | partial — fee/funding display (TRADER_HUD-058), derivative stats (TRADER_HUD-098), risk-limit tier table (DATA_MODEL-086) deferred |
| Order Entry & Management | Order types & parameters | §2 DATA_MODEL-041–047, DATA_MODEL-052/053, DATA_MODEL-060/061 | partial — trailing stop + position TP/SL (DATA_MODEL-050/051, Case 7.5) wholly deferred |
| Order Entry & Management | Order entry panel | §4 APP-001–010, APP-071–073 | partial — risk-based sizing (APP-074), pre-trade preview/leverage-aware max size (APP-075) deferred |
| Order Entry & Management | Chart & DOM trading | §3 TRADER_HUD-059–062, TRADER_HUD-082, §4 APP-071 | partial — TP/SL handle dragging (TRADER_HUD-063), move-stop-to-breakeven (TRADER_HUD-083) deferred |
| Order Entry & Management | Amend, cancel & bulk operations | §2 DATA_MODEL-046/047, §3 TRADER_HUD-045/046 | partial — batch place/amend/cancel (DATA_MODEL-048/049, Case 7.4) wholly deferred; per-settleCoin cancel-all scope deferred inside DATA_MODEL-047 |
| Order Entry & Management | Order confirmation & hotkeys | §4 APP-008–013, APP-083–085 | partial — hotkey scoping rules (APP-087), keyboard-only ticket flow (APP-088) deferred; dedicated transfer confirmation deferred with Transfers (DATA_MODEL-089) |
| Positions & Account | Positions table | §3 TRADER_HUD-047–050, TRADER_HUD-082–084 | partial — hedge-mode (one-way vs hedge) rendering deferred (TRADER_HUD-085) |
| Positions & Account | PnL display | §3 TRADER_HUD-048, TRADER_HUD-079 | partial — closed-PnL records per round trip (DATA_MODEL-080) and balance-change explanation (TRADER_HUD-081) deferred |
| Positions & Account | Leverage & margin mode | §2 DATA_MODEL-009/010, DATA_MODEL-083 | partial — position-mode switch guard (DATA_MODEL-084), add/reduce isolated margin (DATA_MODEL-085), risk-limit tier selection+preview (DATA_MODEL-086), unified-account margin overview (TRADER_HUD-086) deferred |
| Positions & Account | Balances & wallet | §2 DATA_MODEL-001–003, DATA_MODEL-090/091, §3 TRADER_HUD-077–079, §4 APP-064 | partial — funding-account balance view (DATA_MODEL-090) and spot-margin collateral/liabilities/borrow-rate state (DATA_MODEL-091) deferred |
| Positions & Account | Transfers & asset operations | §2 DATA_MODEL-089 | deferred — all 4 trading.json cases consolidated into one leaf; not a first-milestone need for a manual single-account terminal |
| Connectivity & Session | API key auth & connection profiles | §2 DATA_MODEL-022–027, DATA_MODEL-092/093 | covered (DATA_MODEL-093 OS keyring deferred) |
| Connectivity & Session | Market & private streams | §2 DATA_MODEL-011–015, DATA_MODEL-028, DATA_MODEL-074/075 | covered |
| Connectivity & Session | Reconnect & resync | §1 CORE-031–034, CORE-041, CORE-055, §2 DATA_MODEL-030–034, DATA_MODEL-076 | covered |
| Connectivity & Session | Rate limit management | §1 CORE-045–047, §2 DATA_MODEL-038–040, DATA_MODEL-087, §4 APP-066 | covered |
| Connectivity & Session | Clock sync & timing | §2 DATA_MODEL-036/037, DATA_MODEL-019, DATA_MODEL-055/058, §4 APP-047/048, APP-076 | covered |
| Risk & Safety | Pre-trade limits | §2 DATA_MODEL-060–063, DATA_MODEL-081–083 | covered |
| Risk & Safety | Confirmation policies | §4 APP-008, APP-010, APP-083–085 | partial — transfer-confirmation-exempt-from-one-click deferred with Transfers (DATA_MODEL-089) |
| Risk & Safety | Kill switch / panic | §2 DATA_MODEL-066/067/088, §4 APP-014–016, APP-086 | covered |
| Risk & Safety | Read-only & safe modes | §2 DATA_MODEL-023, DATA_MODEL-035, DATA_MODEL-064/065, §4 APP-017–019, APP-067 | covered |
| Workspace & UX | Layouts, panels & tabs | §4 APP-020–026 | partial — symbol-link groups, named shareable layouts, detachable OS windows (APP-036/037/038, Case 2.6) wholly deferred as a maturity-stage case |
| Workspace & UX | Persistence | §4 APP-027–029 | partial — chart drawings and indicator-instance settings persistence deferred (APP-079), tied to TRADER_HUD-018–020 (defer) |
| Workspace & UX | Themes & display | §4 APP-033–035, APP-076 | partial — per-table column show/hide/reorder (APP-077), reduced-motion (APP-078) deferred |
| Workspace & UX | Alerts & notifications | §1 CORE-027/028, §4 APP-049/050, APP-068 | partial — percent-move/funding-rate/volume-spike alert types (APP-069), alert sounds+per-category selection (APP-070) deferred |
| History & Audit | Order history | §3 TRADER_HUD-053/054 | partial — conditional/TP-SL trigger history (TRADER_HUD-092), deep-link to chart (TRADER_HUD-093) deferred |
| History & Audit | Trade / execution history | §3 TRADER_HUD-051/052, §2 DATA_MODEL-058/059 | partial — closed-PnL records per round trip deferred with PnL display (DATA_MODEL-080) |
| History & Audit | Funding, fees & transaction log | §2 DATA_MODEL-079, §3 TRADER_HUD-080 | partial — balance-change explanation view deferred (TRADER_HUD-081) |
| History & Audit | Application logs & audit trail | §2 DATA_MODEL-077/078, §1 CORE-048–050, CORE-054, §4 APP-065 | covered |
| History & Audit | Export | §4 APP-062/063, APP-081 | partial — JSON export variant deferred (APP-082); CSV + clipboard covered |

Every domain and every feature in `trading.json` appears above exactly once. No feature is dropped without an explicit (defer) leaf backing its "deferred"/"partial" row.

## 7. Proposed phase roadmap

Rationale: safety and infrastructure before features, data before UI. Every phase below is a milestone-sized grouping of the leaves above; each phase is split into user-reviewed iterations at phase-planning time, not pre-decided here.

**Phase 0 — Requirements & test infrastructure**
- CTest registration, tag discipline, deterministic async test support: INFRA-001–012
- Mock ByBit V5 harness, recorded-payload replay, fault injection: INFRA-013–019
- Render regression, sanitizer jobs, parser fuzz/property tests: INFRA-020–029
- Doorstop tree, coverage gate, CI gate pipeline, freeze workflow: INFRA-030–040 (includes the CONTRIBUTING.md `update_kind` naming fix as a Phase 0 doc change, per merge coverage notes)

**Phase 1 — Connectivity & session robustness**
- CORE secure/resilient transport: TLS verification, reconnect with backoff, heartbeat staleness, error-path repairs, DNS re-resolution: CORE-029–041, CORE-055
- CORE HTTP robustness, rate-limiting primitive, structured logging, scheduler lifecycle: CORE-042–054, CORE-051–053
- DATA_MODEL connection profiles, phased credential storage (plaintext MVP/encrypted-file production/keyring deferred), key-permission introspection: DATA_MODEL-022–027, DATA_MODEL-092/093
- DATA_MODEL auth handshake, reconnect orchestration, session-state surface, per-symbol topic teardown, thread confinement, clock sync, rate-limit budgeting: DATA_MODEL-028–040, DATA_MODEL-074–076, DATA_MODEL-087

**Phase 2 — Market data completeness**
- CORE sequence-gap detection primitive and historical replay bridge (db_data_feed): CORE-013/014, CORE-021–024
- DATA_MODEL linear-category support, derivative instrument info & tickers, wallet flow, orderbook gap policy: DATA_MODEL-011–015, DATA_MODEL-032/033, DATA_MODEL-073, DATA_MODEL-001–003
- DATA_MODEL REST bootstrap & backfill: recent trades, klines, order/execution history: DATA_MODEL-054–059
- TRADER_HUD orderbook DOM view model and watchlist live quote table: TRADER_HUD-021–028, TRADER_HUD-084, TRADER_HUD-036–042

**Phase 3 — Trading core**
- DATA_MODEL typed order-command surface, order-type matrix, amend/cancel, orderLinkId idempotency: DATA_MODEL-041–053
- DATA_MODEL versioned private persistence, positions data flow, fee rates: DATA_MODEL-006–008, DATA_MODEL-016–021
- DATA_MODEL pre-trade risk gateway, read-only enforcement, kill-switch primitive: DATA_MODEL-060–067, DATA_MODEL-081–083, DATA_MODEL-088
- TRADER_HUD positions/orders view models and the cockpit order-submission port: TRADER_HUD-043–050, TRADER_HUD-082, TRADER_HUD-090/091

**Phase 4 — Trading UI**
- APP order ticket, client-side validation surfacing, confirmation policies, hotkeys, panic control, safe mode: APP-001–019, APP-083–086
- APP orders/positions/history table shells and balances panel: APP-060/061, APP-064
- TRADER_HUD wallet & account-header view models rendered by APP: TRADER_HUD-077–079
- APP status & error surfacing: connection state, toasts, clock-skew banner, rate-limit pressure, testnet banner: APP-039–048, APP-066/067, APP-080

**Phase 5 — Market data UX**
- TRADER_HUD chart interaction completeness: pan/zoom, crosshair, timeframe switching, scroll-back backfill, bounded memory, own-orders/executions overlay: TRADER_HUD-001–017, TRADER_HUD-087
- TRADER_HUD DOM ladder grouping/centering/own-position marking and Time & Sales tape: TRADER_HUD-021–035
- CORE condition-triggered alert primitive plus APP alert manager and notification center: CORE-027/028, APP-049/050, APP-068
- APP order sizing & price helpers feeding click-trading: APP-071–073

**Phase 6 — Workspace & polish**
- APP interactive splitters, tab lifecycle, layout/session persistence, settings dialog: APP-020–032
- APP themes, density modes, timezone/locale formatting: APP-033–035, APP-076
- DATA_MODEL audit trail & transaction log, CORE logging facility with rotation, APP log viewer: DATA_MODEL-077–079, CORE-048–050, CORE-054, APP-065
- APP history table shell, CSV export, clipboard copy: APP-060–063, APP-081

## 8. Review decisions (resolved 2026-07-03)

1. **INFRA-035 (hash stamper) — un-deferred.** The critic's "(defer)" tag contradicted reality: the re-hash script already ships as live Phase 0 tooling (`scripts/check_frozen_tests.py`, wired into `ci/gate.sh`), and INFRA-034's gate entrypoint and INFRA-036's review workflow both depend on it unconditionally. Leaving it deferred would have meant an active, in-use capability sitting in the "not doing this yet" bucket forever, with two other active requirements phrased as conditional on it someday un-deferring — a documentation/reality mismatch, not a real open question. INFRA-034/035/036 are rewritten to state the dependency as unconditional fact.
2. **`PanelType::Balances` — confirmed, added to the plan.** APP-064 now names the new enum value explicitly (alongside the nine existing values), the same way TRADER_HUD-021 names `PanelType::OrderBook`. The actual `enum class PanelType` edit in `src/cockpit/content_panel.hpp` is implementation work for whichever phase schedules the Balances panel, not a requirements-document change.
3. **DATA_MODEL-024 credential backend — split into three phased, independently-supported leaves**, per direction: MVP ships plaintext storage first (**DATA_MODEL-024**, `credential_backend: plaintext`, explicit startup warning, not silently insecure); the production default is a passphrase-encrypted file (**DATA_MODEL-092**, `credential_backend: encrypted_file`), promoted from a parenthetical fallback to its own committed requirement; OS keyring (Secret Service) integration is the true nice-to-have and is marked **DATA_MODEL-093 (defer)**. This also resolves the original "which is the default" question — the default is profile-dependent (dev vs production), not a single global choice.
4. **Defer-marker convention — per-leaf `(defer)` is the single authoritative marker, going forward.** A case-header `(defer)` on a wholly-deferred case may remain as a human-readable hint (e.g. "Batch operations (defer)") but is never load-bearing alone — every leaf underneath now repeats its own token: CORE-019/020, DATA_MODEL-048/049/050/051, APP-036/037/038 were missing theirs and now carry it, matching the convention TRADER_HUD and INFRA already followed and the DATA_MODEL Case 1.4 fix applied earlier during import (where a stale blanket header over a *mixed* case would have wrongly deferred active leaves DATA_MODEL-009/010).
5. **Modal verb — confirmed: every normative leaf uses exactly one "shall".** TRADER_HUD-095's "are consciously deferred" is corrected to "shall remain deferred until scheduled". A single, consistent modal verb per leaf is what makes `check_req_coverage.py`/import parsing and human scanning both reliable — worth enforcing precisely because it's easy to drift on placeholder/deferred leaves where the "shall" feels redundant.
6. **Cancel-all per-side scope — folded into DATA_MODEL-047's existing text**, not a new leaf: `(per settleCoin deferred)` becomes `(per-settleCoin and per-side deferred)`. The gap was real but is a one-clause refinement of an already-active, already-covered requirement, not a missing feature needing its own decomposition.
7. **Chart position-entry/liquidation-line overlays — resolved as a product-level deferred note, not new TRADER_HUD leaves.** Per the scope-discipline policy below, TRADER_HUD Case 1.6's description now states the deferral directly; no TRADER_HUD-0xx leaves were minted for it.
8. **Positions-table ADL rank / PnL-basis selection — same treatment as #7.** TRADER_HUD Case 6.1's description now states both are deferred without decomposition; TRADER_HUD-048 stays mark-price basis until scheduled.

### Requirement scope discipline (added on review)

The plan was flagged as over-decomposed for MVP acceptance review: cases like chart position/liquidation overlays and positions-table sub-fragments were getting broken into implementation-shaped sub-leaves (rendering, data-binding, column-by-column) for functionality nobody has scheduled yet. That's speculative decomposition — it inflates review burden now for design decisions that will most likely change by the time the feature is actually scheduled, which is the same waste YAGNI warns against in code. **Going forward: an advanced/complex feature not yet scheduled gets exactly one `(defer)` marker at the case (or leaf) it naturally attaches to, stating what's deferred — never a full per-component breakdown.** Decomposition into CORE/DATA_MODEL/TRADER_HUD/APP leaves happens when a feature is actually being scheduled into a phase, as part of that phase's iteration planning (which is user-reviewed before coding regardless). Items 7 and 8 above were the concrete instances; this paragraph is the standing policy so it doesn't recur leaf-by-leaf.

This also answers the tree-vs-cross-cutting-concern question directly: the deeper worry was whether a strict document tree can represent a requirement that legitimately touches several branches (cancel-all spans DATA_MODEL+APP; chart overlays span the trading-domain research and TRADER_HUD) without either duplicating it or forcing an awkward single home. Industry practice for exactly this (traceability-matrix / systems-engineering literature, e.g. requirements allocation guidance discussed in NASA and INCOSE-derived material) is *not* to flatten or restructure the hierarchy — it's to give each requirement exactly one owning parent for decomposition purposes, and use non-hierarchical trace links for every other relationship it participates in. Doorstop already supports this natively (an item's `links` can name any UID, not just its document's structural parent), and the plan has in fact been doing this informally all along via the "(consumes [ID])" / "(rendered by [ID])" prose scattered through nearly every branch (e.g. TRADER_HUD-079 "(rendered by [APP-064])", DATA_MODEL-035 "(the kill-switch path ([DATA_MODEL-066]) is exempt)"). No structural change is needed — the fix for #6 above is exactly this pattern applied correctly (single owner DATA_MODEL-047, cross-referenced by APP-014/APP-086), not a tree redesign. `req/README.md` now documents this as the standing cross-reference convention.
