# Universal Trading API & Bot Sandbox — Design & Plan (pending user review)

Status: **plan for review, nothing implemented**. Decomposes `req/engine/TRADE.yml`
(README point 3 "Universal trading API…", point 4 "Engine to test automated strategies…",
point 5 "Extensible set of plugins…") into six named branches under `req/engine/trade/`.
Written the way `req/refactor_plan_2026-07-20.md` was: prose design doc + terse `req/` stubs;
the stubs are the source of truth for scope, this file is the rationale and the spec.

# 1. Prior art survey

Six mature open-source platforms and two proprietary-but-documented ones were read at source
level (not just marketing docs) to ground this design. Table first, detail after.

| Platform | Indicator model | Trigger model | Connector split | Plugin isolation | Backtest/live parity |
|---|---|---|---|---|---|
| **Hummingbot** | none — inline `pandas_ta` calls on a DataFrame per tick | imperative Python in `on_tick`/`determine_executor_actions`, one declarative exception (`TripleBarrierConfig`) | `ExchangePyBase` (spot) / `PerpetualDerivativePyBase` (derivatives); execution class + separate `*_api_order_book_data_source.py` class per connector | none — single in-process asyncio program; non-Python (DEX) bridged via a separate **Gateway** REST+mTLS service | same `ControllerBase` object driven live and in `BacktestingEngineBase`; only *executors* are swapped for per-type simulators |
| **Freqtrade** | none — inline `talib`/`pandas_ta` columns | boolean `DataFrame.loc[...] = 1` masks (`enter_long`/`exit_long`) | one concrete `Exchange` class wrapping `ccxt`/`ccxt.pro` directly, no abstract interface | none — strategies are imported Python classes (`StrategyResolver`), FreqAI models are the only "plugin" boundary | `populate_indicators/entry/exit` run byte-identical in live and `Backtesting.backtest()` |
| **Backtrader** | **`Indicator`** is a first-class stateful object: `lines = (...)`, `params = (...)`, tracks its own bar index in lockstep with its data feed, composes (`CrossOver(a, b)`) | `Strategy.next()` reads indicator `.lines` values imperatively | `Store`/`Broker`/`Feed` triplet per venue; swapping backtest→live = swapping the triplet, `Strategy` code unchanged | none — in-process | identical `Strategy` object; only the `Store`/`Broker`/`Feed` triplet changes |
| **NautilusTrader** | **`Indicator`** trait/base (`handle_bar`/`handle_quote_tick`/`handle_trade_tick`, `.value`, `.initialized`), registered onto a bar/tick stream via `register_indicator_for_bars()` | imperative in `Actor`/`Strategy` callbacks | `InstrumentProvider` + `DataClient` + `ExecutionClient` per adapter | none in-process, but architecture is deliberately single-threaded-kernel + message bus so the *same* `Strategy` binary runs backtest/sandbox/live | same `Strategy` object across environment contexts; only injected clients change |
| **QuantConnect LEAN** | `IndicatorBase<T>` (`ComputeNextValue`, `Current`, `IsReady`), fed by `IDataConsolidator` (time or non-time bars) | **Algorithm Framework**: `IAlphaModel` → `IPortfolioConstructionModel` (some built-ins are literal QP: `MeanVarianceOptimizationPortfolioConstructionModel`) → `IExecutionModel` → `IRiskManagementModel` | `IBrokerageModel` (rules) + `IBrokerage` (connectivity) | Python hosted **inside** the C# engine via pythonnet (`AlgorithmPythonWrapper` wraps a Python object to satisfy the C# `IAlgorithm` interface) | same algorithm object; brokerage/fill/slippage models swapped |
| **CCXT** | n/a (data library only) | n/a | one unified method surface (`fetchOHLCV`, `fetchOrderBook`, `createOrder`, …) per exchange class, `params={}` escape hatch for exchange-specific fields | n/a | n/a |
| **TradingView Pine Script** | `series` type: one value per bar, `ta.*` built-ins carry their own recursive state | `strategy.entry(..., when=cond)`, `alertcondition(cond, ...)` | n/a (closed platform) | n/a | historical bars execute once each; the *realtime* bar re-executes per tick with automatic rollback of non-`var`/`varip` state between ticks — this is the "sync to candle close vs. live tick" boundary the user is describing |
| **MetaTrader MQL4/5** | compiled `.ex5` **Indicator**: `OnCalculate(prev_calculated, time[], …)` writes named buffers via `SetIndexBuffer`, incremental from `prev_calculated` | compiled `.ex5` **Expert Advisor**: `OnTick()` reads another indicator's buffer via `iCustom()` + `CopyBuffer()`, orders via `CTrade::Buy`/`OrderSend` | n/a | **Indicators and EAs are separate compiled binaries**; an EA loads a compiled indicator indirectly by name — exactly the "Python plugin / C++ library" split this project wants, already proven at scale | separate binaries per role, not per environment |

Plugin-isolation precedent outside trading: **pybind11** `py::scoped_interpreter` embeds CPython
in a C++ host (only one guard per process; GIL must be managed explicitly around callbacks;
Python 3.12+ `py::subinterpreter` gives each embedded strategy its own GIL but must be destroyed
on its creating thread). **LLVM pass plugins** show a clean versioned-C-ABI pattern:
`extern "C" llvmGetPassPluginInfo()` returns a struct carrying `LLVM_PLUGIN_API_VERSION`, checked
by the host before the struct's other fields are trusted. **HashiCorp go-plugin** and **Bitwig
Studio**'s plugin host show the out-of-process alternative: each plugin is its own OS process
talking RPC/gRPC over a socket, so "a plug-in crash will happen discreetly" instead of taking the
host down — at the cost of IPC latency. **Cap'n Proto**/FlatBuffers/Protobuf are the concrete
precedent for one schema generating matching C++ and Python bindings, so both plugin kinds see an
identical wire-level API.

Geometric/LP trigger precedent: MQL5's "Trendline Breakout with R² Goodness of Fit" formalises a
breakout as a **half-plane test** against a fitted regression line, gated by `R² > threshold`
computed from touch count; support/resistance zones are **interval-containment** tests
(`low ≤ price ≤ high`) over clustered pivots; TA-Lib's 61 `CDLxxx` candlestick-pattern functions are
purely geometric OHLC-ratio tests. `PyPortfolioOpt`'s `EfficientFrontier` and the Almgren-Chriss
optimal-execution model are concrete QP/LP formulations (`minimize ½xᵀPx + qᵀx s.t. Gx≤h, Ax=b`)
that turn indicator/state vectors into a sized order rather than a bare boolean.

# 2. Where this sits in the existing architecture

```
                         ┌───────────────────────────────────────────┐
                         │   Strategy / Bot  (Python plugin or        │
                         │   C++ shared library — STRATEGY_RUNTIME)   │
                         └───────────────┬─────────────────┬─────────┘
                                          │ reads            │ places/cancels
                                 ┌────────▼────────┐   ┌─────▼──────────┐
                                 │  Trigger graph   │   │ ExchangeConnector│
                                 │  (TRIGGER)       │   │ (EXECUTION)     │
                                 └────────▲────────┘   └─────┬──────────┘
                                          │ subscribes         │ REST/WS
                                 ┌────────┴────────┐   ┌─────▼──────────┐
                                 │ Indicator series  │   │ existing        │
                                 │ (MARKET_SERIES)   │   │ src/data/bybit  │
                                 └────────▲────────┘   │ private pipeline │
                                          │ subscribes  └────────────────┘
                                 ┌────────┴────────────────────┐
                                 │ existing datahub data_feed /  │
                                 │ BuoyCandleQuotes (src/engine)  │
                                 └───────────────────────────────┘
```

Everything below the dashed line already exists (`datahub`, `connect`, `src/data/bybit`,
`BuoyCandleQuotes`). The new work is the four boxes above it, plus `BACKTEST` (an alternate,
historical-data-fed implementation of the same `ExchangeConnector`/clock contract) and
`HB_COMPAT` (a Python-side shim so existing Hummingbot scripts/controllers port with minimal
edits). This keeps the project's layering rule intact: application/strategy code depends on the
new trading-library layer, which depends on `datahub`/`connect`/`engine`, never the reverse.

Namespace: `scratcher::trading` (siblings of the existing `scratcher::data`, `scratcher::cex`
engine code), with a `scratcher::trading::compat::hummingbot` sub-namespace for §3.6. Naming
note: this is unrelated to the existing `PRICE_INDICATOR` HUD overlay
(`req/hud/market_data/PRICE_INDICATOR.yml`, a rendered last-price line) — the new type is called
`Indicator`/`IIndicator` throughout and never "price indicator".

# 3. Component design

## 3.1 `MARKET_SERIES` — market data & indicator series

**Goal**: indicator values as series synced 1:1 to candle close, matching the Pine
`series`/MQL5 buffer/Backtrader `lines`/NautilusTrader `Indicator` model, but as native C++
objects rather than ad hoc DataFrame columns (the one thing every Python framework surveyed
*doesn't* do — Hummingbot and Freqtrade both recompute inline `pandas_ta` calls per tick with no
reusable indicator object).

`BuoyCandleQuotes` (`src/engine/buoy_candle.hpp`) already produces a per-instrument, per-period
series of closed candles (`min/max/mean/close/volume/sigma±`) plus the live "active" candle,
advanced by `AppendTrades`/`AdvanceTo`. This is the OHLCV-equivalent backbone (`mean` standing in
for VWAP where a strategy would traditionally read `close`; `close` is also carried per candle).
Indicators are new `datahub`-shaped nodes wired onto it — not a new data pipeline:

```cpp
namespace scratcher::trading {

// Every built-in and plugin indicator implements this. Deliberately virtual (a genuine runtime
// polymorphism boundary per CONTRIBUTING.md — indicators are chosen at strategy-load time, not
// compile time) even though a *specific* built-in like SMA is itself a plain template class.
class IIndicator
{
public:
    virtual ~IIndicator() = default;
    // Called once per closed candle, in candle order — never per-tick. A strategy that needs
    // sub-candle reaction reads BuoyCandleQuotes::active_candle() directly, same as Pine's
    // realtime-bar path reads the unclosed bar.
    virtual void on_candle(const BuoyCandleQuotes::candle_t& closed) = 0;
    virtual bool ready() const = 0;               // Pine "na" / NautilusTrader "initialized"
    virtual double value(size_t output = 0) const = 0; // double: see §4 fixed-point boundary rule
    virtual void reset() = 0;
};

// Built-ins are compile-time parameterised (policy-over-virtual per CONTRIBUTING.md library-tier
// idiom) and only type-erased behind IIndicator at the point a strategy graph is assembled.
template <size_t Period>
class SimpleMovingAverage : public IIndicator { /* rolling sum over Period closed candles */ };

// A series-shaped feed, so an indicator subscribes exactly like any datahub consumer:
//   candle_feed->subscribe(make_subscription<candle_range>(
//       [indicator](update_kind, const auto& full) { indicator->on_candle(full.back()); }));
// Indicators taking other indicators as input (MACD-of-RSI, a spread of two instruments' closes)
// subscribe to each other's value the same way — a small DAG, not a monolithic DataFrame.
}
```

Multi-timeframe sync (Pine's `request.security(..., lookahead_off)`) is a resampling node that
folds N base-timeframe closed candles into one higher-timeframe candle and republishes on the
higher period's boundary — reusing `BuoyCandleQuotes`'s own period-bucketing algorithm
(`req/engine/buoy/BUOY-001.yml`), so both the base and derived series share the exact same
closed/active semantics and no indicator ever "sees the future" of an unclosed higher-timeframe
candle.

## 3.2 `TRIGGER` — trigger & signal evaluation (geometry + linear programming)

**Goal**: replace "boolean pandas expression with magic thresholds" (Freqtrade/Hummingbot) with
composable geometric and optimization objects, while keeping a plain boolean/crossover mode for
straightforward compatibility.

The existing `datahub::data_condition<Entity>` (`field_predicate<Entity, Field, Op>`: a
compile-time field pointer + operator, a runtime value, composed by AND, also emittable as a SQL
`QueryCondition`) is already exactly this pattern one level down — a first-class predicate object
instead of inline `if`. `Trigger` generalises it from entity fields to indicator values and from a
boolean-only result to a signed size:

```cpp
struct TriggerResult { bool fired = false; std::optional<currency<int64_t>> signal_size; };

// Strategy pattern: a Trigger holds one evaluator, swappable without touching the indicator graph.
class ITriggerEvaluator
{
public:
    virtual ~ITriggerEvaluator() = default;
    virtual TriggerResult evaluate(std::span<const double> indicator_values,
                                    scratcher::time_point candle_close) const = 0;
};

// (a) Boolean/crossover — the Freqtrade/Hummingbot-compatible mode, expressed as data_condition's
// own AND-of-predicates idiom generalised over indicator values instead of entity fields.
class BooleanEvaluator : public ITriggerEvaluator { /* AND/OR tree of value-vs-value predicates */ };

// (b) Geometry — half-plane test for a trendline/channel break (MQL5 R²-gated regression),
// interval containment for a support/resistance zone, polygon fitting for chart patterns.
class GeometricEvaluator : public ITriggerEvaluator
{
    // e.g. HalfPlaneBreak{slope, intercept, side} : fired = sign(price - (slope*t+intercept)) == side
    // e.g. ZoneContainment{low, high}             : fired = low <= price && price <= high
};

// (c) Linear/quadratic programming — turns a vector of indicator values + constraints into a
// sized decision, e.g. a PyPortfolioOpt-style EfficientFrontier re-weighting, an Almgren-Chriss
// execution slice, or a stat-arb z-score threshold with a sized hedge ratio.
class LinearProgramEvaluator : public ITriggerEvaluator
{
    // Delegates to a pluggable ILinearProgram (addVariable/addConstraint/setObjective/solve),
    // so the LP/QP backend (e.g. HiGHS) is swappable without touching the trigger graph.
};

class Trigger
{
    std::vector<std::shared_ptr<IIndicator>> m_inputs;
    std::shared_ptr<ITriggerEvaluator> m_evaluator;
public:
    // Subscribes to every input indicator's candle-close event; fires evaluate() once all
    // inputs for this candle are ready, mirroring data_condition::matches()'s all-predicates-pass
    // contract but yielding a TriggerResult instead of a bool.
};
```

This is also where the Hummingbot `TripleBarrierConfig` idea is generalised rather than discarded:
a `ManagedTrigger` wraps a fired entry with its own stop-loss/take-profit/time-limit sub-triggers
that self-monitor until closed, same lifecycle Hummingbot's `PositionExecutor` gives one order.

## 3.3 `EXECUTION` — unified order execution / connector abstraction

**Goal**: one connector interface across exchanges, generalising the ByBit-specific private
pipeline already implemented (`src/data/bybit/data_manager.hpp`: `OrderRequest` →
`json_body_encoder` → `connect::http_query<rest_signer>` → `OrderAck` feed) the same way CCXT
generalises per-exchange REST calls into one method surface.

```cpp
class IExchangeConnector
{
public:
    virtual ~IExchangeConnector() = default;
    // Mirrors ccxt's createOrder(symbol, type, side, amount, price, params) shape, but with
    // currency<int64_t> price/qty (never double) and an explicit params-escape-hatch map for
    // exchange-specific fields ccxt calls out as its own unified/native split.
    virtual std::string place_order(const OrderIntent&) = 0;
    virtual void cancel_order(const std::string& order_id) = 0;
    virtual const InstrumentInfo& instrument(const std::string& symbol) const = 0; // precision/limits, ccxt market[] equivalent
    virtual std::shared_ptr<BalanceFeed> balances() = 0;
    virtual std::shared_ptr<OrderFeed> orders() = 0;
};
```

Pre-trade validation reuses the CCXT `market.precision`/`market.limits` idea and Hummingbot's
`BudgetChecker`/`OrderCandidate` pattern: an `OrderIntent` is resized/rejected against the
instrument's tick size, lot size and available balance *before* `place_order` is called, using the
same `currency<T>` rescale machinery already in `src/engine/currency.hpp` (`raw_at`) rather than
float rounding.

Each real exchange (ByBit today, others later) implements `IExchangeConnector` by composing its
existing `data_manager` private pipeline — this is additive over `src/data/bybit`, not a rewrite.

## 3.4 `STRATEGY_RUNTIME` — strategy/bot runtime & plugin sandbox

**Goal**: run a bot as either a **Python plugin** or a **compiled C++ shared library**, matching
MQL5's proven "compiled Indicator vs. compiled EA, loaded by name" split, informed by the research
into pybind11/LLVM/go-plugin/Bitwig above. Two tiers, chosen per bot at load time:

- **Tier A — in-process C++ library plugin** (trusted, latency-sensitive). Loaded via `dlopen` +
  a versioned C ABI, LLVM-pass-plugin style:
  ```cpp
  extern "C" struct OpenTraderPluginInfo {
      uint32_t abi_version;                 // checked before any other field is trusted
      const char* name;
      IStrategy* (*create)(const StrategyContext&);
      void (*destroy)(IStrategy*);          // never `delete` a plugin object from the host
  };
  extern "C" OpenTraderPluginInfo* open_trader_plugin_entry();
  ```
  A version mismatch is rejected outright, exactly as LLVM's `LLVM_PLUGIN_API_VERSION` gate does.

- **Tier B — sandboxed bot process** (default for third-party and all Python bots). Each bot
  instance is its own OS process, talking to the engine over a local Unix-domain-socket using one
  Protobuf schema for the command/event contract (order intents, fills, indicator snapshots) —
  the go-plugin/Bitwig pattern: a misbehaving or crashed bot cannot take the host down, and
  resource limits are enforceable via cgroups. Because the schema is language-neutral, a **Python**
  bot process imports the generated Python stub around it and a **C++** bot process links the
  generated C++ stub — both see the identical logical API, which is what "compatible… as python
  plugins and C++ libraries" requires structurally, not just by convention. High-frequency
  market-data snapshots (the indicator/candle feed) prefer FlatBuffers/Cap'n Proto over the same
  socket to avoid a full-deserialize per tick; order/control messages stay plain Protobuf.
  A `py::subinterpreter`-per-bot in-process mode (Python 3.12+, own GIL per interpreter, must be
  destroyed on its creating thread) is a later opt-in for users who explicitly trade the crash
  isolation of Tier B for lower latency — not the default.

Strategy config is schema-validated on both sides: a Pydantic `BaseModel` for Python bots
(matching Hummingbot's `BaseClientModel`/`ClientConfigMap` shape so ported configs need minimal
changes) and a plain aggregate struct (Glaze-reflectable, per the project's C++23 convention) for
C++ bots — both generated from the same Protobuf/JSON-schema source so the two never drift.

## 3.5 `BACKTEST` — backtesting engine

**Goal**: strategy/indicator/trigger code must run **unchanged** in backtest and live — every
platform surveyed that does this well (Hummingbot's `BacktestingEngineBase`, Freqtrade's
`Backtesting`, NautilusTrader's environment contexts, Backtrader's `Store`/`Broker`/`Feed` swap)
achieves it by swapping only the data source and the execution/fill side, never the strategy code.

Concretely: `BACKTEST` provides an `IExchangeConnector` implementation fed from historical candles
via `BuoyCandleQuotes` replay instead of live `connect::websock_connection`, plus a fill/slippage
simulator standing in for Tier A/B's live order feed (mirroring Hummingbot's per-executor
`*ExecutorSimulator` classes rather than re-running a live async order-state machine against fake
time). The `Trigger`/`IIndicator` graph and the `STRATEGY_RUNTIME` plugin boundary are identical
in both modes — only which `IExchangeConnector` and clock get injected changes, matching
NautilusTrader's "environment context" framing exactly.

## 3.6 `HB_COMPAT` — Hummingbot compatibility layer

Full binary compatibility (loading an unmodified Hummingbot `.py` script) would require either
depending on Hummingbot's own package or reimplementing its entire class surface
(`hummingbot.core.data_type.common`, `hummingbot.strategy_v2.*`) byte-for-byte — expensive and
brittle across Hummingbot's own version churn (its `ScriptStrategyBase` was itself just merged
into `StrategyV2Base` upstream). This design instead targets **format compatibility**, ported in
stages of increasing fidelity:

1. **Concept/lifecycle compatibility** (do first): Open Trader's Python bot base class mirrors
   `StrategyV2Base`'s hook names (`on_tick`, `on_stop`, `format_status`) and `ControllerBase`'s
   split (`update_processed_data`/`determine_executor_actions`), so porting a controller is a
   mechanical rename, not a rewrite.
2. **Data-shape compatibility**: `MarketDataProvider.get_candles_df()`/`CandlesConfig`-equivalent
   accessors return the same column names Hummingbot's `CandlesBase` does
   (`timestamp, open, high, low, close, volume, …`) over Open Trader's `IIndicator`/candle series,
   so existing `candles_df.ta.rsi(...)`-style Hummingbot controller code needs no column renames.
3. **Order/connector compatibility**: `IExecutionModel`/`OrderCandidate` field names track
   Hummingbot's `OrderType{MARKET,LIMIT,LIMIT_MAKER}`/`TradeType{BUY,SELL}`/`PositionAction`
   enums and `BudgetChecker` semantics, so a ported controller's order-construction code is
   unchanged.
4. **Trigger compatibility**: `TriggerEvaluator`'s `ManagedTrigger` (§3.2) accepts a
   `TripleBarrierConfig`-shaped input directly.

Explicitly **not** attempted: Hummingbot's Cython connector base classes, its Gateway DEX bridge,
or `Decimal`-typed prices (Open Trader keeps `currency<int64_t>` end to end per
`CONTRIBUTING.md`; the compat layer converts at the Python/C++ boundary, not internally).

# 4. Cross-cutting design rules

- **Fixed-point boundary, extended.** `CONTRIBUTING.md` forbids floating point for price/time up
  to the render boundary. Trigger geometry/LP evaluation (§3.2) is a new, narrower exception:
  `GeometricEvaluator`/`LinearProgramEvaluator` may consume `double` copies of indicator series
  purely for the optimization/geometry math (an already-lossy statistical process, unlike
  accounting), but the resulting `OrderIntent` must be re-quantized into `currency<int64_t>` at
  the `EXECUTION` boundary against the instrument's tick/lot precision — the same discipline CCXT
  enforces via `market.precision`/`market.limits` before a unified order reaches the exchange.
- **Indicators/triggers are datahub-shaped, not a new pipeline.** §3.1/§3.2 deliberately reuse
  `subscribe`/`handle_data`/`data_acceptor()` and the `data_condition` predicate-composition idiom
  rather than inventing a parallel reactive framework, per `CONTRIBUTING.md`'s "new stages must
  follow the established contract."
- **Virtual dispatch only at genuine plugin/runtime boundaries** (`IIndicator`, `ITriggerEvaluator`,
  `IExchangeConnector`, `IStrategy`, the plugin ABI) — built-in indicators and evaluators stay
  compile-time templates, per the existing policy-over-virtual library-tier idiom.
- **No collision with `PRICE_INDICATOR`.** That item is a HUD render overlay; this design's
  `Indicator`/`IIndicator` is an unrelated `scratcher::trading` type.

# 5. Phased delivery (each phase user-reviewed per the TDD gate before implementation)

1. `MARKET_SERIES`: `IIndicator` + one built-in (SMA) wired onto `BuoyCandleQuotes`, unit-tested.
2. `TRIGGER`: `BooleanEvaluator` first (cheapest, covers crossover-style triggers), then
   `GeometricEvaluator`, then `LinearProgramEvaluator` behind a chosen LP/QP backend.
3. `EXECUTION`: `IExchangeConnector` extracted from the existing ByBit private pipeline (no new
   exchange required to land this phase).
4. `BACKTEST`: historical replay `IExchangeConnector` + fill simulator, reusing phases 1–3 unchanged.
5. `STRATEGY_RUNTIME`: Tier A (C++ ABI) first (stays entirely in-process, no IPC to design yet),
   then Tier B (sandboxed process + schema) once the wire contract is settled by phases 1–4.
6. `HB_COMPAT`: layered on top once phases 1–5 are stable, in the four sub-stages of §3.6.

## Open questions (blockers for implementation, not for this design's review)

- Which LP/QP backend to embed for `LinearProgramEvaluator` (HiGHS is the leading permissively
  licensed candidate; needs a licensing/build-footprint check against `CMakeLists.txt`'s CPM deps).
- Protobuf vs. FlatBuffers vs. Cap'n Proto for Tier B's schema — Cap'n Proto's Python binding is a
  wrapper over its C++ dynamic API (wire-identical semantics for both sides) but has the smallest
  ecosystem of the three; needs a spike before committing.
- Whether Tier B bots get their own `req/exchange`-style per-connector credential isolation or
  share the engine's existing `--api-keyfile` mechanism.

# 6. References

Hummingbot: `github.com/hummingbot/hummingbot` — `connector/{connector_base.pyx,exchange_base.pyx,exchange_py_base.py,perpetual_derivative_py_base.py,budget_checker.py}`, `strategy/strategy_v2_base.py`, `strategy_v2/controllers/controller_base.py`, `strategy_v2/executors/position_executor/data_types.py`, `data_feed/market_data_provider.py`, `data_feed/candles_feed/{candles_base.py,candles_factory.py}`, `core/data_type/{common.py,order_candidate.py}`, `core/gateway/gateway_http_client.py`, `strategy_v2/backtesting/backtesting_engine_base.py`.
Freqtrade: `github.com/freqtrade/freqtrade` — `strategy/interface.py`, `resolvers/strategy_resolver.py`, `persistence/trade_model.py`, `exchange/exchange.py`, `freqai/freqai_interface.py`; docs freqtrade.io/en/stable/strategy-customization/.
Backtrader: `github.com/mementum/backtrader` — `indicator.py`, `indicators/{sma,crossover}.py`, `cerebro.py`, `brokers/ibbroker.py`.
NautilusTrader: `github.com/nautechsystems/nautilus_trader` — `docs/concepts/architecture.md`, `docs/concepts/adapters.md`, `nautilus_trader/indicators/base.pyx` (v1), `crates/indicators/src/indicator.rs` (v2).
QuantConnect LEAN: `github.com/QuantConnect/Lean` — `Algorithm/QCAlgorithm.cs`, `Indicators/IndicatorBase.cs`, `Common/Data/Consolidators/IDataConsolidator.cs`, `Algorithm.Framework/`, `AlgorithmFactory/Python/Wrappers/AlgorithmPythonWrapper.cs`.
CCXT: `github.com/ccxt/ccxt` manual (`github.com/ccxt/ccxt/wiki/manual`), `ccxt.pro` manual.
Pine Script: tradingview.com/pine-script-docs (Execution model, `ta.*`, `request.security`, Alerts).
MQL5: mql5.com/en/docs (`customind/setindexbuffer`, `series/copybuffer`) and mql5.com/en/articles/19625 (R²-gated trendline breakout), mql5.com/en/articles/43.
Geometry/pattern precedent: `github.com/ednunezg/pytrendline`; `ta-lib.github.io/ta-lib-python` pattern-recognition group; tradingmathematically.com S/R detection.
LP/QP precedent: `github.com/robertmartin8/PyPortfolioOpt` (`GeneralEfficientFrontier.rst`), cvxpy.org QP example, Almgren-Chriss (`smallake.kr/optliq.pdf`).
Plugin sandboxing: pybind11.readthedocs.io (embedding, subinterpreters), `llvm.org/doxygen/structllvm_1_1PassPluginLibraryInfo.html`, `github.com/hashicorp/go-plugin` (`docs/internals.md`), bitwig.com crash-protection article, capnproto.org.
