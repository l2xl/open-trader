# Wallet layout survey & template drafts

Date: 2026-08-27. Research only — no code changes. Companion to `wallet_panel_research.md` (elements/ThorVG feasibility)
and `svg_template_research.md` (SVG-template + CSS-class + data-injection route); this document covers the *layout* question:
how CEX-oriented products arrange multi-account, multi-coin balances, and five reviewable SVG drafts for our panel.

Method note: two web-research passes (2026-08-27) over exchange help centers, official API docs, terminal manuals and
design literature. Most exchange help domains were egress-blocked for direct fetch, so several facts come from
search-engine renditions of the exact official pages; field lists are high-confidence, pixel-level placement claims are
marked *(uncertain)* where applicable.

## 0. Scope framing — what "CEX-oriented" changes

Our wallet is an **exchange account**, not an on-chain wallet. That removes the whole self-custody furniture (seed
phrase, receive addresses, address book, QR codes, chain selection) and replaces it with a different backbone:

1. **The account dimension is first-class.** One exchange login owns several wallet types (ByBit: Funding + Unified
   Trading Account; the industry is converging on exactly this two-item core — Bybit, OKX, Bitget, Gate, KuCoin all now
   push a unified margin account next to a funding wallet) plus **subaccounts**, each with its own wallets. A balance is
   addressed by *(member, accountType, coin)*, not by *(address, coin)*.
2. **Money moves by internal transfer**, instant and free, not by on-chain send — so the pivotal action verb on every
   surveyed assets page is *Transfer*, and deposit/withdraw are peripheral (out of scope for our display-only panel).
3. **Risk is part of the wallet.** Unified/derivatives accounts carry equity ≠ wallet balance (UPL), and margin-health
   ratios with hard 100 % boundaries. Every surveyed derivatives venue shows IM/MM-style ratios *inside* its assets
   surface; a wallet panel that hides them answers "what do I have" but not "am I OK".
4. **Freshness is visible state.** Balances change on fills on the exchange's schedule; surveyed terminals treat the
   panel as live (WS) with the wallet page as snapshot — our G7 staleness stamp ("updated hh:mm:ss") is the honest
   equivalent.

## 1. Survey

### 1.1 Product skeletons (compressed)

| Product | Top of the assets surface | Account switching | Coin table & filters |
|---|---|---|---|
| Binance | "Estimated Balance" in **BTC + ≈fiat (selectable, USD default)**, **eye mask**, Today's PnL | left-nav list of wallet types (Spot, Funding, Margin, Futures, Earn…) | Coin, Total, Available, **In Order**, BTC/fiat value, per-row actions; **default sort = holding value desc**; dust→BNB converter |
| ByBit | Assets Overview: fund distribution across accounts + holdings w/ avg cost; UTA page: **Total Equity, Margin Balance, Available, IM %, MM %** (100 % = block / liquidate) | **Funding + UTA** two-item core; subaccount manager page (standard/custom/custodial) with per-row Transfer | per-coin equity/walletBalance (v5 `accountIMRate`, `accountMMRate`, `totalEquity` map 1:1 to the UI); dust→MNT/USDT/USDC |
| OKX | "Est total value" + **eye icon**, Today's PnL, allocation & PnL charts (1W/1M/3M/custom) | Funding vs Trading (unified; 4 account modes) | equity / available / in-use rows; margin-ratio **risk bar**, warn < 300 %, liquidate ≤ 100 % (inverted semantics vs IM/MM-rate!) |
| Kraken Pro | Portfolio value + history graph **in selected display currency**; per-asset unrealized P&L, cost basis | spot + Multi-Collateral derivatives wallet; institutional subs via **top-right dropdown** | **dockable Balances widget** in the trade view; user-selectable columns (Allocation %, Available/Total Holdings, Price, Available/Total Value); **Hide Small Balances threshold toggle** |
| Coinbase Adv. | total + balance-history chart; perps get a **"liquidation buffer" %** | **portfolios** (isolated sub-portfolios, dropdown switcher) — subaccounts by another name | asset list w/ Available-to-trade; no hide-small documented |
| Deribit | wallet page is per-currency rows; **Account Summary in the terminal**: Equity, Available Funds, Initial/Maintenance Margin, Margin Balance (+ USD row under cross-collateral); **MM bar top-right** | subaccounts switched in the **top-right account menu** ("switch-into") | per-currency rows + Transfer; Account Summary **columns are user-editable** |
| MT4/5, cTrader | — | one account per connection | single **status strip**: Balance · Equity · Margin · Free margin · Margin level % (+ floating P/L), pinned to the positions table |
| TradingView | Account Manager bottom panel: pinned grey **summary line** above Positions/Orders tabs | **account selector in the panel header** | column-driven table framework; broker-defined summary fields |
| Hyperliquid / dYdX | right rail: Account Equity (Spot/Perps split), then Balance, Unrealized PnL, **Cross Margin Ratio**, Maint. Margin, Leverage as label:value rows; dYdX adds Low/Med/High risk tag | single account | bottom tab strip: Balances · Positions · Orders · History |
| Delta / CoinStats / CMC | hero total; **tap the total to cycle** fiat ↔ BTC/ETH (Delta); 24 h Δ; allocation donut (CMC) | portfolios list | icon + amount + value rows; CoinStats: custom "hide balances up to $X" and **grays out** excluded rows; eye mask (CMC) |

### 1.2 Cross-product patterns (with rough incidence)

1. **Hero total at top** — fiat-first with a **conversion-currency selector** and a **≈ BTC equivalent** secondary line
   (Binance/Bitget/KuCoin BTC-first, the rest fiat-first): ~8 of 9 CEXes.
2. **Eye mask beside the total**, masking *every* figure in the panel, not just the hero: confirmed Binance/OKX/CMC/Coinbase;
   industry table stakes.
3. **Skeleton = hero → summary strip → holdings table.** Same three tiers at Delta, CMC, Hyperliquid's rail, and (minus
   hero) MT4/cTrader/TradingView. Matches the pattern already chosen in `wallet_panel_research.md` §4.2.
4. **Summary strip = 4–6 label:value pairs**, one strip when wide (MT4/cTrader/TradingView), wrapped into a grid or
   stacked rows when narrow (Hyperliquid). Values few, same-currency, causally ordered (equity → available → margin).
5. **Ratios always number + bar/gauge, colored by escalating band**; 100 % is a hard boundary (ByBit/Bitget/Gate IM/MM;
   Binance margin ratio; Deribit MM bar; OKX inverts — treat direction as per-exchange config, never hardcode).
6. **Margin rates never aggregate across accounts** — every venue scopes IM/MM to one margined account. An "All accounts"
   view must list per-account gauges, not average them.
7. **Two subaccount philosophies**: *manage-from-above* — a page listing subs with per-row totals + Transfer (Binance,
   ByBit, OKX, KuCoin, Bitget, Gate) — vs *switch-into* — an account dropdown swapping the whole context (Kraken,
   Deribit, Coinbase, TradingView, IBKR with first-class "All" aggregate). A docked panel wants the second with the
   aggregate option; the first becomes our per-account rows/cards inside the aggregate view.
8. **Selector form follows account count**: chips/tabs only up to ~5 short labels (NN/g tabs guidance); dropdown with
   check-marked list + "All" beyond that; searchable two-column picker (GA-style) only for large estates.
9. **Coin table canon**: Coin | Total | Available | In-Order/Frozen | Value (conv. currency) | (Actions); **default sort
   value-desc**; right-aligned numerics; per-row allocation % is a Kraken column option. Columns are user-configurable
   at 4+ venues (Kraken, Deribit, cTrader, TradingView) — argues for a column picker eventually, not more default columns.
10. **Hide-small toggle with threshold** (Kraken, CoinStats; Binance widely reported *(uncertain)*), plus near-universal
    dust conversion (→ BNB/OKB/MNT/KCS/BGB/GT; ≈ 0.001 BTC threshold, cooldown). CoinStats grays out excluded rows
    rather than dropping them silently — the honest variant of our footer roll-up.
11. **Today's PnL near the total** (6 of 9) — needs a start-of-day baseline snapshot; our feed has UPL only, so drafts
    show *unrealised* and leave "today" as an engine follow-up.
12. **Live numbers in tabular figures, right-aligned**, so ticks don't reflow columns — OpenSans digits are tabular by
    default (`wallet_panel_research.md` §2.2), which is why every numeric in the drafts is OpenSans right-anchored.
13. **PnL encoded by sign + color, never hue alone** (~8 % male color-vision deficiency; East-Asian red/green inversion
    argues for palette-as-preference eventually).
14. **Allocation in narrow panels: horizontal stacked bar ≻ donut** (part-to-whole studies: angle is the weakest cue;
    donuts degrade past ~6 slices); Kraken instead exposes allocation as a per-row % column.
15. **The terminal's order-entry margin cluster is a separate widget** (Binance bottom-right, Deribit top-right,
    ByBit/OKX order panel) — the wallet panel does not have to carry order-gating figures once an order-entry panel exists.

## 2. Shared design tokens used by the drafts

- Palette = `ui_builder.cpp`: content `#1E1E23` (30,30,35), header `#28282D`, footer `#232328`, divider `#50505A`,
  hairline `#33333A`, label `#C8C8C8`, dim `#808080`, bright `#ECECF0`, accent `#6496E6` (the existing 100,150,230).
  Semantic: positive `#2ECC71`, negative `#E74C3C`, warn `#F1C40F` (research doc §4.3). Coin dots use brand hues.
- Margin bands: < 50 % normal · 50–70 % amber · ≥ 70 % red; gauge ticks at 70 / 90 / 100 %.
- Formatting: thousands separators; stables & fiat-like 2 dp, other coins 8 dp with trailing zeros kept in aligned
  tables; explicit `+` / `−` (U+2212); zero/absent = dim "—"; currency glyph dimmer than digits; `$` only at the hero.
- Chrome: 26 px header with title + split/close glyphs, 20–22 px footer used for the dust roll-up note.
- States not drawn but required (G8): `NoCredentials` ("No API credentials (--api-keyfile)"), `Loading`, `Live`;
  the eye mask replaces every figure with `••••` when on.

## 3. The drafts

All five render from one sample dataset (self-consistent by construction; totals derived from a per-(account × coin)
matrix): Main·Unified $98,705.58 equity (IM 12.8 %, MM 6.4 %), Main·Funding $17,289.45, sub `algo-grid-01`·Unified
$9,567.91 (IM 61.8 % — amber band on display), sub `hedge-desk`·Funding $4,438.16; total equity **$130,001.10**
(≈ 1.9394 BTC), unrealised **+$1,060.90**; 9 visible coins + 3 dust ($0.87) under the $1 hide-small threshold.

| Draft | File | Dock | Scope / filter model | Wins when | Costs |
|---|---|---|---|---|---|
| **A — Compact stack** | `draft_a_compact_stack.svg` | 360×644 (min ≈ 300×600) | chip row: All + per-account; search + hide-small on the list | default docked panel; ≤ ~5 accounts | chips overflow → pan+fade; no per-coin location info |
| **B — Master–detail** | `draft_b_master_detail.svg` | 560×574 | persistent account rail (groups Main / Subaccounts, per-account equity + MM health dot); detail pane = A's anatomy | wallet gets a wide dock or split half; frequent account hopping | ~190 px width for the rail |
| **C — Portfolio overview** | `draft_c_portfolio_cards.svg` | 400×614 | scope dropdown; account cards double as filters; coin rows expand into per-account splits | portfolio review; "where is my BTC held?" | tallest; two taps to a specific number |
| **D — Account matrix** | `draft_d_account_matrix.svg` | 560×470 | coins × accounts crosstab, Σ-equity footer row, TOTAL column tinted; Majors/Stables quick chips | power-user audit; treasury view of many accounts | needs ≥ 520 px; no margin bars (dot line only); UPL squeezed |
| **E — Summary strip** | `draft_e_summary_strip.svg` | 1200×84 | MT4-idiom horizontal strip: equity → available → UPL → margin micro-bars → top-coin ticker cells | shallow top/bottom dock beside charts | glance-only; companion to A–D, not a replacement |

Draft anatomy A (the baseline, = research doc §4.2 + the account dimension):
chrome → scope chips → hero (label + `USD ▾` selector, `$` dim, updated stamp, eye) → secondary (≈ BTC · signed UPL) →
2×3 KPI grid (Available, Wallet balance, Unrealised PnL, Margin balance, Initial margin, Maint. margin) →
**per-derivatives-account margin rows** (name + IM gauge with 70/90/100 ticks + IM/MM values; §1.2-6 is why this is a
list, not one bar) → coin table (search, hide-small, `9 / 12` count, sort marker on VALUE) → footer dust roll-up.
When the scope is a single Unified account, the margin rows collapse into the two full-width IM/MM bars of §4.2.

## 4. Recommendation

**A is the panel.** It keeps the §4.2 skeleton the elements-native plan already targets, adds the account dimension in
the cheapest form (chips = switch-into with a first-class All), and every element maps to `WalletViewModel` fields.
Adopt from the others as staged extensions rather than alternatives:

1. Replace the chip row with C's dropdown selector once accounts can exceed ~5 (pattern §1.2-8) — same view-model,
   different selector widget.
2. B is A with the selector made persistent: promote it only if the wallet leaf routinely gets ≥ 550 px.
3. C's expandable per-coin split and D's matrix become *modes* of the coin section later ("group by coin" / "matrix"),
   not separate panels; D is also the natural shape for a future multi-exchange row dimension.
4. E only after an order-entry/positions panel exists; it is the MT4 strip, not a wallet.

Data-layer deltas the account dimension adds on top of `wallet_panel_research.md` §3 (all deferred until multi-account
is actually in scope — phase 1 ships with Main·UNIFIED only and the chip row hidden or fixed to one chip):

- The wallet feed keys by `accountType` only; subaccounts make the key *(memberId, accountType)* — under the §3.2(b)
  split: totals keyed by *(member, accountType)*, coins keyed by *(member, accountType, coin)*, aggregation in the
  view-model, never in the feed.
- ByBit exposes subaccount balances to the master key (`/v5/user/query-sub-members` + per-member wallet queries /
  `query-account-coins-balance`), so "All accounts" needs no extra key files, but it is REST-only there — WS pushes
  arrive per authenticated member. Staleness therefore differs per account → the `updated` stamp belongs to the scope,
  not the panel.
- FUND (Funding) accounts have no margin fields — `WalletViewModel` rows need the "spot-like" degenerate form (equity
  = wallet balance, no gauges), which the drafts already exercise.

## 5. Template mechanics (hand-off to the SVG/ThorVG or elements route)

- Each SVG is: review canvas (caption + footnotes) around one self-contained `<g transform>` panel group — the group is
  liftable as a template skeleton on its own.
- Every dynamic value node carries a `v-*` class (`v-total-equity`, `v-btc-equiv`, `v-total-upl`, `v-kpi`, `v-im-rate`,
  `v-mm-rate`, `v-coin-sym`, `v-coin-amount`, `v-coin-usd`, `v-coin-upl`, `v-acct-equity`, `v-updated`) — injection-point
  markers for the CSS-class + data-injection route verified in `svg_template_research.md`; row groups would be stamped
  per entry at runtime.
- Feature diet is deliberately conservative for the ThorVG loader: rect/rrect, circle, path, line, text, one linear
  gradient (A's chip-row fade); no filters, masks, or text-on-path. Numbers are pre-formatted strings (fixed-point
  formatting stays on our side of the render border, per CONTRIBUTING).
- Fonts: `Open Sans, Segoe UI, DejaVu Sans, sans-serif` — OpenSans digits are tabular (§2.2 of the research doc), so
  live updates don't jitter; all numeric text is `text-anchor="end"` on fixed column edges.
- `gen_wallet_drafts.py` (same directory) regenerates all five from the shared sample dataset:
  `python3 gen_wallet_drafts.py` rewrites the `draft_*.svg` files beside itself; tweak data/geometry there rather than
  editing SVGs by hand.

## 6. Files

- `wallet_layout_survey.md` — this document
- `draft_a_compact_stack.svg` · `draft_b_master_detail.svg` · `draft_c_portfolio_cards.svg` ·
  `draft_d_account_matrix.svg` · `draft_e_summary_strip.svg`
- `gen_wallet_drafts.py` — mock generator (research tooling, not application code)

Key sources (survey passes, 2026-08-27): Binance wallet-overview & futures-interface FAQs; ByBit UTA asset-page,
UTA-trading-rules and v5 wallet-balance docs; OKX activity/portfolio and unified-account help; Kraken Pro interface
guide + module-settings (Balances widget columns, Hide Small Balances); Coinbase multiple-portfolios and perps docs;
KuCoin/Bitget/Gate account & subaccount help; Deribit account-summary API + subaccounts articles; MetaTrader 5 terminal
help (Trade tab strip); cTrader TradeWatch; TradingView Account Manager docs; Hyperliquid docs/guides; dYdX trade-tab
help; Delta/CoinStats/CoinGecko/CMC portfolio help; NN/g tabs guidance; Skau & Kosara EuroVis 2016 (pie/donut cue
study); tabular-figures write-ups. Exchange domains were egress-blocked for direct fetch — flagged items are
*(uncertain)*.
