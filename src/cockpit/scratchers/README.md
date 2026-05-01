# Scratchers Concept

The Trade Cockpit UI is built combining a dynamical set of content panels.
The content panels may use UI library components layout or a custom drawing canvas.

Scratcher is a class responsible for drawing on the content panel canvas. Multiple scratchers collaboratively draw on the canvas creating complex UI visualizations like a quote graph with visual indicators.
Everything on the canvas is controlled collaboratively by a number of scratchers. Examples are the quote graph candles, rulers, or even graph margins.

The scratcher interface contains just three methods:
- `CalculateSize(IChartPanel&)` — called every time the size of the content panel changes; scratchers adjust pixel geometry (e.g. reserve space for axis labels).
- `CalculatePaint(IChartPanel&)` — called before each render pass to recompute paint data (tick positions, data ranges, etc.); results may be read by other scratchers.
- `Paint(IChartPanel&) const` — performs the actual drawing using the values computed in the two preceding steps.

CalculateSize is called every time the size of the content panel changes.
The scratcher painting processing happens in two steps. First, CalculatePaint is called for every scratcher to calculate the painting data, which may be accounted by other scratchers as well.
This principle is used by the Margin scratcher to calculate margins used by all other scratchers. Thus, the order, how the scratchers are calculated, is important.
The second step is Paint itself.

# ThorVG Vector Graphics Scene

Scratchers do not draw directly on pixel-based canvas. Instead they create end enunerate vector graphics objects provided ThorVG. 
ContentPanel instance holds ThorVG Scene which may have several subscenes with ContentPanel controlled transform matrices which cfreates several coordinate systems: 
* Canvas (or screen) based coordinates (in pixels, creates bounding clip-window)
* Human viewable logical canvas coordinates (inverts screen Y-axis, borderless, controls pan and absolute zoom)
* Human viewable logical window (Synced with top canvas size, used to build UI related graphics like axes rulers, ruler ticks, labels, etc.)
* Logical coordinates (used to draw market graphs)

Scratchers have an access to all the subscenes and select appropriate one with relevant coordinates to build its graphics. Commonly scratchers would not care of exact transformations between subscenes, the transformations are controlled by ContenPanel since the ContentPanel knows about its size, keyboard/mouse events, etc.

# Layered Class Hierarchy

The scratcher hierarchy is split across two layers to keep business logic independent of a rendering backend.

**cockpit layer** — UI-agnostic data and geometry:
```
Scratcher  (pure interface: CalculateSize / CalculatePaint / Paint)
├── PriceIndicator    — tracks a price point; Paint() left to derived class
├── PriceRuler        — computes vertical price-axis geometry; Paint() left to derived class
├── QuoteScratcher    — ingests PublicTrade stream, builds BuoyCandleQuotes; Paint() left to derived class
└── TimeRuler         — computes horizontal time-axis geometry; Paint() left to derived class
```

**app/elements layer** — Skia rendering implementations (each `final`):
```
elements::PriceIndicator  : cockpit::PriceIndicator  — dashed price line + colored label box
elements::PriceRuler      : cockpit::PriceRuler      — vertical axis with tick marks and price labels
elements::QuoteScratcher  : cockpit::QuoteScratcher  — candlestick chart (green/red lines per direction)
elements::TimeRuler       : cockpit::TimeRuler       — horizontal axis with tick marks and timestamp labels
```

`ScratchPanel` (`app/elements`) implements `IChartPanel` and owns the ordered list of scratchers. It drives the three-phase pipeline:
1. On resize → `CalculateSize` for every scratcher
2. On data update → `CalculatePaint` for every scratcher
3. On draw → `Paint` for every scratcher into the shared `SkCanvas`

The standard scratcher registration order for a quote chart is: `QuoteScratcher` → `PriceRuler` → `TimeRuler` → `PriceIndicator`.
[gear.json](../../../../../Downloads/gear.json)

