# Trader Cockpit - the unique UI controller for The Exchange Scratchpad

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
  * Account balances

