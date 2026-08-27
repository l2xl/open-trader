# Connection Layer

**Namespace:** `scratcher::connect`

Abstract network communication supporting HTTP and WebSocket protocols implementation based on boost/asio and boost/beast libraries.

`connect` owns the transports and all their asynchronous lifecycle machinery (handshake, read loop, heartbeat, framing). Any content beyond verb+URL — headers, signing, extra outbound messages — is delegated to a policy type supplied by the caller; datahub and the ByBit layer never touch a transport's internals directly (see [src/datahub/README.md](../datahub/README.md)'s Outbound Flow).

**Key Classes:**
- `context` - Shared infrastructure: SSL context, DNS resolution caching
- `http_query<RequestPolicy = no_request_policy>` - One-shot HTTP request for REST API calls
- `websock_connection<SessionPolicy = no_session_policy>` - Persistent WebSocket with heartbeat, async read loop

Both are class templates with a defaulted policy parameter and no alias types — an unparameterised use is spelled `http_query<>` / `websock_connection<>`.

## Policy-parameterised transports

The policy is a template parameter, stored by value as a member, and granted `friend` access to the transport so it can reach machinery a plain caller can't (e.g. `co_write`).

### RequestPolicy — `http_query<RequestPolicy>`
- Invoked once per request, inside `co_request`, after the target/host/body are set and **before** `req.prepare_payload()`: `self->m_policy(*self, req)`
- Signature: `void operator()(Query&, http_request&) const` — `http_request` is connect's own record `{verb, target, body, headers}` for the pending request; the policy reads/alters it (typically appends to `headers`) and never touches the Boost.Beast request, which the query builds from the record afterwards; the policy value passed at `create()` is the one invoked, so it may carry state (e.g. credentials)
- `operator()(std::string query = {}, std::string body = {})` — no `headers` parameter: setting headers is the policy's job now. This call operator *is* the acceptor shape datahub's encoders target; the encoder holds the query's `shared_ptr` and calls it directly
- Default `no_request_policy` is a no-op — `http_query<>` behaves exactly as it did before policy injection existed

### SessionPolicy — `websock_connection<SessionPolicy>`
- `co_open` hook: `co_await SessionPolicy::co_open(weak_ptr<Conn>)` runs after the handshake succeeds and **before** `m_status = READY` — anything it writes precedes every message already queued for send, since `co_send_loop` waits for `READY`
- Per-message hook: `self->m_policy(*self, payload)` runs in `co_send_loop` immediately before each write — passive per-message alteration (no-op by default), applies to every outgoing frame including heartbeats
- `friend SessionPolicy` grants `co_open` access to the connection's private `co_write` primitive (single write primitive shared with `co_send_loop`)
- Default `no_session_policy` sends nothing extra and alters nothing — `websock_connection<>` behaves exactly as it did before

## Ordering guarantees
1. A `co_open` policy message is written before any application message queued prior to `READY`
2. The per-message policy hook runs immediately before every write, in submission order
3. Both policies are invoked with a reference to the live transport, never a detached copy of its state
