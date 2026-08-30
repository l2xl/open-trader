# Data Model Design

**Namespace:** `scratcher` (common entities, OrderBook), `scratcher::data` (common entity types), `scratcher::bybit` (ByBit-specific entities and data manager)

## Common principles

All the mutable data are managed by entity types declared under `entities` subfolder or `<data_provider>/entities` for provider specific where the <data-provider> examples are bybit, okx, binance, etc...

The data lifecycle is managed exclusively by `datahub` library facilities.

The data pipeline management implemented by the `IDataController` subclasses on a per data provider basis.

Most of the data are persisted using data_model template, parameterized by the data entity type (currently uses SQLite as the data store)

Some data, like the order book, does not need persistence and managed by an independent model type (`OrderBook`) which satisfies the `data_sink` model concept and can be used to parameterize `data_sink<OrderBook>`.

All the data can be assigned to the two categories with different lifecycles:

1. Public data with origin owned by the data provider. Updates received from the data provider are automatically overwrite the locally persisted data.
   Here are:
   * Instrument list
   * Order book
   * Public trades

2. Private data with origin owned by the user. These data updates may be originated locally or received from the data provider. These data must be never overwrited and always versioned so every update creates a new data record with a new version.
   Here are:
   * Account balance
   * Order history
   * Position history
   * Private Trade history

## Data Providers

Disclaimer: the data entities type model is still in prototyping stage and most of the entity types are implemented particularly for ByBit data provider.

Mature data model type structure will contain all the entity type definitions defined commonly for all data providers.
Particular per provider definitions must be convertable to the common ones and used mostly for data deserialization when received from the data provider and then converted by the data pipeline into common types for persistence and local operation. 

### Bybit

* ByBit specific data entity definitions: ./bybit/entities
* ByBit authentication policies: ./bybit/auth.hpp
* ByBit data controller implementation: ./bybit/data_manager.hpp, ./bybit/data_manager.cpp

#### Public pipeline

`ByBitDataManager` owns one `connect::http_query<>` per REST endpoint and one `connect::websock_connection<>` for the
`/v5/public/spot` stream. Inbound data flows `data_dispatcher → data_adapter → data_sink<data_model> → data_feed`;
the instrument list is fetched on every `SubscribeInstrumentList`, per-symbol order-book / public-trade streams are
materialised on the first `SubscribeInstrument(symbol, ...)`.

#### Private pipeline (requires `--api-keyfile`)

`--api-keyfile` names a plain text file holding the API key on the first line and the secret on the second; like
`--config`, a relative path that does not start with `.` resolves inside the data dir. Credentials are loaded once at
construction into `std::optional<credentials>`; without the option every private chain is skipped and `PlaceOrder` /
`CancelOrder` throw.

An outbound request has two tiers: the *command* (a `connect::http_query<rest_signer>` carrying verb + URL, or the
private `connect::websock_connection<ws_authenticator>` plus an op name) and the *data* (a caller-supplied entity such
as `OrderRequest`, `OrderFilter`, `ExecutionFilter`, `WalletFilter`). A datahub encoder binds the entity type to the
connection and owns it: `json_body_encoder` for POST bodies, `url_query_encoder` for GET query strings. The manager
holds the encoders, not the connections — the chain is built inline, and the encoder's `shared_ptr` is what keeps the
connection alive, mirroring the inbound connection-owns-dispatcher-owns-adapter rule:

```cpp
m_place_order.emplace(datahub::make_json_body_encoder<OrderRequest>(
    connect::http_query<rest_signer>::create(m_context, http::verb::post, api_base + API_ORDER_CREATE,
        rest_signer{*m_credentials}, response_pipeline, error_cb)));
```

| entity | command | response → |
|---|---|---|
| `OrderRequest` | POST `/v5/order/create` | `ApiResponse<PlaceOrderResult>` → `OrderAck` into the order-ack feed (keyed by `orderLinkId`) |
| `CancelOrderRequest` | POST `/v5/order/cancel` | logged |
| `OrderFilter` | GET `/v5/order/realtime` | `ListResult<Order>` → private order sink (same sink as the WS `order` topic) |
| `ExecutionFilter` | GET `/v5/execution/list` | `ListResult<Trade>` → private trade sink (same sink as the WS `execution` topic) |
| `WalletFilter` | GET `/v5/account/wallet-balance` | `ListResult<WalletBalance>` → wallet feed (feed-only, nested coin balances are not DAO-mappable) |

`SubscribeOrders`, `SubscribeTrades` and `SubscribeWallet` attach the subscription and fire the matching snapshot
query once; the `/v5/private` stream (`order`, `execution`, `wallet` topics, `WsPrivatePayload<T>` envelope) then keeps
the same sinks and feeds current.

Authentication is not a pipeline stage but a policy handed to the transport by type and value: `rest_signer` signs the
GET query string or the POST body and appends the `X-BAPI-*` headers to every pending `connect::http_request` record; `ws_authenticator`
writes the `auth` frame right after the websocket handshake, before any queued subscription, building it at that moment
so its `expires` stamp cannot be stale.

