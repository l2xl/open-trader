# Trader HUD 

**Namespace:** `scratcher::cockpit`

Contains of the main TradeCockpit class which implements dynamic orchestration of the set of ContentPanel instances.
It is also aware of set of traded instruments (currently it is spot symbols) and implements hi-level control for data provider instances management and control.
In terms of Data-View-Controller design pattern, the Trader Cockpit instance is the controller and every content panel is a view. Data is provided by the DataProvider instances through the datahub based datapipes.
The content panels placement is controlled separately be the app layer which is independent of the Trader Cockpit 

The content panels may be of two types:

* Graphical Panels which provides a graphical canvas for Scratchers. The scratchers are active self-drawing classes which are dynamically added and removed from the canvas.
  * Candlestick like graphs
  * Orderbook diagram
  * Indicator graphs

Read [scratchers/README.md](scratchers/README.md) for design details

* Table panels
  * Private orders
  * Private Trades
  * Account balances — `WalletPanel`, a form rendered from the SVG template `templates/wallet_panel.svg`

Both families share `VectorScenePanel`, the offscreen ThorVG host (runtime reference, ARGB pixel buffer bound as the
SwCanvas target, HUD root scene, worker/paint update circuits, damage-tracked `Render()`); a subclass attaches its
content under `HudScene()` and lays it out in `DoLayout()`. `InstrumentPanel` adds the logical scene and the scratchers;
`WalletPanel` binds a view-model to template elements located by their SVG `id`.

`svg_template` loads an SVG through ThorVG's loader and detaches the scene tree from the hosting `tvg::Picture`: a
loaded Picture skips `Canvas::update()` unless it is itself marked dirty, so paints mutated inside it would never
re-render. The detached duplicate keeps every `id` (`Paint::id`, djb2 hash) and drops the loader's viewBox clipping
layer; template rows are cloned with `Paint::duplicate()` (ids are not copied, `Shape::reset()` drops the CSS fill,
so both are re-applied by the owner).

