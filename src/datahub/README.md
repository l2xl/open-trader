# Datahub Component

## Overview

Datahub is a composable data pipeline where each stage is optional. All stages share the same `data_acceptor` concept — a callable `(Range&&) -> void` — so any stage can be directly wired to the next, skipping intermediate layers when they are not needed.

Datahub is designed as a generic data pipeline where every class made as much generic as possible to do not couple any data types, containers, etc. Even if the previous element in the chain may have some restricted types due to its nature, the next must not rely on these types and restrictions and provide as generic implementation as possible.  

C++ Namespace: `datahub`

## Full Pipeline

```
Data source (HTTP / WebSocket / File)
    │
    ▼
data_dispatcher<Acceptors...>          — async SPSC queue + strand; tries each adapter in order
    │
    ▼
data_adapter<DataType, Handler>        — Glaze JSON → C++ struct; on failure falls through to next
    │
    ▼
data_sink<Model>                       — links persistence model to the feed layer
    └─ model->accept(cache)            — persists / merges (data_model or custom model)
    │
    │  handle_data(cache copy)         — fires feed acceptor (subscribers notified immediately)
    ▼
data_feed (sorted_data_feed / sorted_snapshot_data_feed / keyed_snapshot_data_feed)
    │  optional data_condition filter
    ▼
data_subscription<Range, UpdateIt...>  — leaf callbacks; snapshot delivered to new subscriber on attach
```

## Reduced Pipelines

All chainlinks expose `data_acceptor<Range>()` returning a callable. Any stage can be bypassed by wiring acceptors directly:

**No persistence — feed only:**
```
data_adapter → data_feed->data_acceptor<Range>()
```

**No feed — model only:**
```
data_adapter → data_sink<data_model>(model, [](cache&&){}, error_cb)
               └─ handle_data no-op; model persists
```

**No sink/model — adapter straight to custom handler:**
```
data_adapter → any callable(Range&&)
```

**No dispatcher — direct model feed (async, strand-posted):**
```
data_model::data_acceptor<Range, Container>()  — posts accept() on db_strand
``` 

**Outbound — entity straight to the connection the encoder owns:**
```
entity → json_body_encoder / url_query_encoder → connection(query, body)
```
No dispatcher, adapter, sink, or feed involved — outbound is inherently a single-shot, two-stage flow; there is nothing left to reduce.

## Outbound Flow

```
Client-side entity (by value)
    │
    ▼
json_body_encoder<Entity, Acceptor, Projection>   — glz::write_json(projection(entity)) → body
        or
url_query_encoder<Entity, Acceptor>               — per-field percent-encoded "k=v&k=v" → query
    │
    ▼
acceptor(std::string query, std::string body)     — the connection's own call operator
    │
    ▼
connection — connect::http_query<Policy> / connect::websock_connection<Policy> instance
    — owns verb + URL (or op name)
    — policy hook (RequestPolicy / SessionPolicy) lives in connect, NOT a datahub stage
```

The chain is built inline, exactly like the inbound one, with the connection constructed inside the encoder factory call:

```cpp
auto place_order = datahub::make_json_body_encoder<OrderRequest>(
    connect::http_query<rest_signer>::create(ctx, http::verb::post, url, rest_signer{creds}, response_pipeline, error_cb));
```

**Ownership rule:** the encoder holds the connection's `shared_ptr` and that ownership is what keeps the connection alive — mirroring the inbound rule where the connection owns its dispatcher, which owns its adapter. There is no separate long-lived "command object" member and no weak-captured acceptor: destroying the encoder destroys the chain.

The acceptor is transport-agnostic: either a callable `(std::string query, std::string body)` or a `std::shared_ptr<T>` for which `(*ptr)(query, body)` is valid; the encoder stores it by value and dereferences pointers before the call.

### Two-tier request

| tier | encoded as | owner |
|---|---|---|
| command / address | the `http_query<Policy>` instance (verb + URL) or `websock_connection<Policy>` instance (+ op name) | the encoder that wraps it |
| data | a client-side entity struct (e.g. `bybit::OrderRequest`, `OrderFilter`) | caller, by value |

datahub hosts no request type — only the two generic encoders above, turning an entity into `(query, body)` strings handed to the connection. Authentication is not a pipeline stage: it is a policy handler injected into the transport by `connect` (see [src/connect/README.md](../connect/README.md)); the transport invokes it at the right lifecycle moment, and the encoders never see it.

## Component Details

### data_encoder.hpp — json_body_encoder, url_query_encoder (`src/datahub/data_encoder.hpp`)
- Acceptor contract for both: a callable `(std::string query, std::string body) -> void`, or a `std::shared_ptr` to one — the shape `connect::http_query<Policy>::operator()` has
- `json_body_encoder<Entity, Acceptor, Projection = std::identity>` — `operator()(Entity&&)` calls `acceptor("", glz::write_json(projection(entity)))`; throws `std::runtime_error` (wrapping `glz::format_error`) on write failure
  - Filtration: per-type via `glz::meta<Entity>` (field selection/renaming, nested shapes); Glaze's default `skip_null_members` drops empty `std::optional`; per-endpoint via the `Projection` callable (`Entity -> View`) applied before `write_json`
  - Factory: `make_json_body_encoder<Entity>(acceptor, projection = {})`
- `url_query_encoder<Entity, Acceptor>` — `operator()(Entity&&)` calls `acceptor(query_string, "")`; no JSON post-transform
  - Iterates `glz::reflect<Entity>::keys` + `glz::to_tie` (the idiom of `src/datahub/operations.hpp`); per field in declaration order: skip `std::nullopt`; serialise the scalar with `glz::write_json`; strip enclosing quotes (enum-by-name, currency codec, numeric formatting stay byte-identical to the JSON body path); percent-encode (RFC 3986 unreserved: `A-Z a-z 0-9 - _ . ~`); join as `key=value&key=value`
  - `static_assert`s every member is a scalar (arithmetic, bool, enum, `std::string`, `currency`) or `std::optional` of one — query entities must be flat
  - Empty entity / all-`nullopt` entity ⇒ empty query string
  - Factory: `make_url_query_encoder<Entity>(acceptor)`

### generic_handler<DATA, PARENT, DATA_CALLABLE, ERROR_CALLABLE, ARGS...> (`src/common/generic_handler.hpp`)
- CRTP mixin that implements `handle_data(DATA)` and `handle_error(exception_ptr)` as virtual overrides via stored callables
- Inherits from `PARENT` (the abstract base being implemented), forwarding `ARGS...` to its constructor
- `handle_data` wraps the callable in try/catch, routing exceptions to `handle_error`
- Used by `data_sink::create()` to produce a concrete subclass without a separate derived class per use case

### data_dispatcher<Acceptor...> (`src/datahub/data_sink.hpp`)
- `boost::lockfree::spsc_queue<std::string>` (1024-entry)
- Serialized via `boost::asio::strand`
- `operator()(std::string)` — pushes to queue, posts async dispatch
- Tries each acceptor in sequence via fold: `(try_accept<I>(data) || ...)` — first to return `true` wins

### data_adapter<DataType, Handler> (`src/datahub/data_sink.hpp`)
- Deserializes JSON string via `glz::read<opts{.error_on_unknown_keys = false}>`
- On success: calls handler, returns `true`
- On failure: returns `false`, dispatcher falls through to next adapter
- Factory: `make_data_adapter<DataType>(handler)`

### data_sink<Model> (`src/datahub/data_sink.hpp`)
- `accept<Range>(data)`:
  1. `handle_data(cache_type(cache))` — virtual; implemented by `generic_handler`, typically fires the feed acceptor
  2. `m_model->accept(std::move(cache))` — delegates to model
- `data_acceptor<Range>()` — returns lambda over `weak_ptr<data_sink>` calling `accept<Range>`
- `create(model, data_handler, error_handler)` — factory via `generic_handler` mixin
- Factory: `make_data_sink(model, data_handler, error_handler)`

**Model concept** (implicit): any type with `entity_type` alias, `cache_type` alias, and `accept(Range&&)` method.

### data_model<Entity, auto PrimaryKey> (`src/datahub/data_model.hpp`)
- SQLite DAO with Glaze reflection for automatic schema generation; RAII table creation
- `accept<Range>(entities)` — `insert_or_replace` per entity; returns only actually-changed rows (dedup via `sqlite3_changes()`)
- `data_acceptor<Range, Container>()` — async variant: posts `accept()` on `m_db_strand` via `weak_ptr`
- `query(condition, args...)` — SELECT with optional WHERE
- `strand_type = boost::asio::strand<boost::asio::any_io_executor>` — public alias; one strand shared across all models backed by the same DB
- Factory: `data_model<E, PK>::create(db, strand, table_suffix)`

### data_feed concept (`src/datahub/data_feed.hpp`)
- Abstract data feed concept. Every feed exposes:
  - `cache_type` — concrete container holding the cache (`CacheContainer<Entity>`)
  - `subscription_type` — the matching `datahub::subscription<cache_type, Extra...>` spec the feed dispatches into
  - `subscribe(weak_ptr<subscription_type>, condition_type = {})` — registers a subscriber paired with its own condition; fires current cache as snapshot synchronously if non-empty
  - `data_acceptor<InputRange>()` — returns a `(InputRange&&) -> void` callable that merges into the cache and notifies subscribers
  - `get_snapshot() -> const cache_type&` — direct read access to the live cache

Each feed takes a template-template `CacheContainer` parameter (default `std::deque`). Pick a stable-reference container (`std::list`, `boost::container::stable_vector`) when subscribers will hold the const-ref past the callback's return — default `std::deque` only keeps refs valid across `push_back`.

#### sorted_data_feed<Entity, SortField, KeyField, CacheContainer = std::deque>
- **Incremental feed.** `subscription_type = subscription<cache_type, const_iterator, const_iterator>`
- In-memory cache sorted ascending by `SortField`, deduplicated by `KeyField`
- `data_acceptor<InputRange>()` — filters by optional condition, skips known keys, inserts sorted, notifies subscribers via `push_snapshot()` (full reorder) or `push_increment(first, last)` (tail append). Subscribers see the new-tail window directly as `[first, last)` — no lookup.

#### sorted_snapshot_data_feed<Entity, SortField, KeyField, CacheContainer = std::deque>
- **Snapshot-only feed.** `subscription_type = subscription<cache_type>`
- Same as `sorted_data_feed` and optimized to deliver just full snapshot (no update bounds calculation)
- `data_acceptor<InputRange>()` — filters by optional condition, merge-inserts sorted by `SortField` deduplicated by `KeyField`, notifies with full-cache snapshot

#### keyed_snapshot_data_feed<Entity, KeyField, CacheContainer = std::deque>
- **Snapshot-only feed.** `subscription_type = subscription<cache_type>`
- In-memory keyed cache with upsert semantics: existing entries matched by `KeyField` are replaced, new entries are inserted
- `data_acceptor<InputRange>()` — filters by optional condition, upserts by `KeyField`, notifies with full-cache snapshot

#### db_data_feed<Entity>
- DB feed which translates query with condition into resulting range with DB cursor
TODO

Filtering is per subscriber, not per feed: `subscribe(sub, condition)` pairs the subscriber's `weak_ptr` with its own `data_condition` in the feed's subscriber list, and that subscriber is to see only matching records in the attach snapshot and in every later update (an empty condition matches every record). The subscription object carries no condition and the feed has no condition of its own.

### data_condition<Entity> (`src/datahub/data_condition.hpp`)
- Composite AND filter dual-purpose: in-memory predicate and SQL `QueryCondition` generation
- `field_predicate<Entity, Field, Op>` — compile-time field pointer + operator (`QueryOperator` enum), runtime value
- `data_condition(Predicates...)` — variadic constructor composing predicates
- `matches(const Entity&)` — all predicates must pass
- `to_query_condition()` — produces SQL WHERE clause
- Static factory methods: `equal<Field>(v)`, `not_equal<Field>(v)`, `less<Field>(v)`, `less_or_equal<Field>(v)`, `greater<Field>(v)`, `greater_or_equal<Field>(v)`

### data_subscription<Range, UpdateIt...> (`src/datahub/data_subscription.hpp`)
- Single class template with two partial specialisations — one interface per feed shape; the feed picks the matching spec at compile time, so the dispatched call never carries arguments the feed didn't have.
  - **`subscription<Range>`** — used by snapshot-only feeds. One pure virtual: `handle_data(update_kind, const Range&)`.
  - **`subscription<Range, It>`** — used by incremental feeds. One pure virtual: `handle_data(update_kind, const Range&, It first, It last)` where `[first, last)` is the new tail.
- Implementation lives in `detail::subscription_impl<Range, Callable, UptateIt...>` — holds the user's Callable as a direct member (no `std::function` wrap, no type-erasure container). Virtual dispatch is the sole runtime indirection and exists only so the feed can hold heterogeneous subscribers in one `std::list<std::weak_ptr<subscription_type>>`.
- **Factory**: `make_subscription<Range>(callable)` — single entry point. Picks the matching spec by static `if constexpr` on the Callable's arity:
  - Callable invocable as `(update_kind, const Range&)` → `shared_ptr<subscription<Range>>` (snapshot-only)
  - Callable invocable as `(update_kind, const Range&, It, It)` → `shared_ptr<subscription<Range, It>>` (incremental)
  - Neither match → `static_assert` with a readable diagnostic
- A right-arity callable for the wrong feed kind fails at the feed's `subscribe()` call, where the `shared_ptr` conversion is rejected.

### update_kind (`src/datahub/data_update.hpp`)
- `enum class update_kind { snapshot, increment }`

## Key Design Rules

- **Weak ptr safety**: all async callbacks capture `weak_ptr` to avoid circular refs and handle object lifetime
- **One strand per DB**: all `data_model` instances backed by the same database share one `strand_type`
- **Thread safety boundary**: `data_dispatcher` serializes JSON dispatch; `data_model` serializes DB access via strand; feed mutation happens on whichever thread calls the acceptor (usually data_dispatcher)
- **Subscriptions are held by weak_ptr**: allowing automatic lazy subscription management (no explicit unsubscribe) — feeds prune expired weak_ptrs on the next push, so dropping the subscriber's `shared_ptr` is the only unsubscribe action needed.
- **Subscriber callback shape is dictated by the feed kind, statically**: snapshot-only feeds never pass iterators to subscribers; incremental feeds always do. There is no runtime branching inside the subscription layer to decide which signature applies — the partial specialisation of `subscription<Range, Extra...>` picked by the feed makes the call shape unambiguous at compile time.
- **Callable held as-is**: the user's lambda or function object is stored as a direct member of `detail::subscription_impl` — no `std::function` wrap, no per-call SBO/allocation overhead.
- **Auth is never a datahub stage**: encoders hand `(query, body)` straight to the connection they own; signing/authentication is a policy injected into the transport by `connect` (see [src/connect/README.md](../connect/README.md)), invoked at the transport's own lifecycle boundary. datahub has no knowledge of credentials or headers.
- **Encoders are entity/transport agnostic**: `json_body_encoder`/`url_query_encoder` depend on nothing but the acceptor shape `(std::string, std::string) -> void`; they compose with any connection (`http_query<Policy>`, `websock_connection<Policy>`, a test double) with zero coupling to a specific transport or to an entity's business meaning.
