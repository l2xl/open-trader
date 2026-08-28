# Wallet panel — decision & implementation notes

Requirement branch: `req/hud/WALLET_PANEL.yml`. Layout decided 2026-08-28 after the wallet-layout review
([PR #21](https://github.com/l2xl/open-trader/pull/21)). Mocks and their generator live in `doc/research/wallet_layout/`.

## Decision

Master–detail wallet panel (review draft B), split along the render-technology boundary:

| Part | Technology | Content |
|---|---|---|
| Account selector rail (~186 px) | **common elements widget** — native cycfi/elements, reusable beyond the wallet | "All accounts" aggregate row; MAIN / SUBACCOUNTS groups; per row: wallet-type badge (U/F), name, equity, IM-band health dot for margined accounts; band legend at the bottom. Owns scope selection. |
| Wallet card | **SVG template** rendered by the ThorVG path, data-injected per selected scope | scope title + `updated hh:mm:ss` stamp; hero: total equity in the conversion currency, `≈ x.xxxx BTC`, signed unrealised PnL; coin table COIN / AMOUNT / VALUE·USD / UPL |
| Panel chrome | elements (existing `UiBuilder` pattern) | header; footer carries the dust note `N small balances hidden (< $1.00)` |

Removed from the card by review (the values stay available in the view-model, they are just not drawn):
the KPI strip (Available / Wallet balance / Unrealised PnL) and the MARGIN USAGE gauge section.

`draft_e_summary_strip.svg` (MT4-style horizontal glance strip) is **reserved for future use**; not part of this iteration.

## Mocks

- `doc/research/wallet_layout/wallet_card_template.svg` — the template itself (380×360 at 9 rows); the SVG is the
  geometry source of truth
- `doc/research/wallet_layout/draft_b_master_detail.svg` — full composition; dashed outline marks the template region
- `doc/research/wallet_layout/gen_wallet_drafts.py` — regenerates the mocks from one consistent sample dataset

## Card template contract

- Injection markers are CSS classes on text nodes: `v-scope-title`, `v-updated`, `v-total-equity`, `v-btc-equiv`,
  `v-total-upl`, `v-coin-count` (the `9 / 12` readout), and the per-coin row group `v-coin-sym` / `v-coin-amount` /
  `v-coin-usd` / `v-coin-upl`. Rows are stamped per coin at runtime (24 px each); the coin list is the flexible,
  scrollable region.
- Every injected value is a **pre-formatted string** produced in the cockpit layer from `currency::raw()/decimals()` —
  no float conversion before the render border (CONTRIBUTING).
- Interactive furniture drawn inside the card area — eye mask, search box, hide-small checkbox — is in the mock for
  layout only; at runtime these are native elements overlays above the rendered scene.
- SVG feature diet for the ThorVG loader: `rect / circle / path / line / text` only; no gradients, filters or masks.
- Typography: numerics in OpenSans (digits are tabular by default → no live-update jitter), `text-anchor="end"` on
  fixed column edges (AMOUNT at inset+158, VALUE at inset+258, UPL at the content right edge); title 13 px bold,
  hero 24 px bold with dim 18 px `$`, secondary and rows 10.5 px, column headers 9 px letter-spaced.

## Display rules

- Sort: VALUE descending. Coins under the hide-small threshold ($1.00 default) leave the list and roll into the
  footer note.
- Amounts: stables/fiat-like coins 2 dp; other coins 8 dp with trailing zeros kept. Values and PnL: 2 dp with
  thousands separators.
- PnL: explicit sign (`+` / U+2212 `−`) **and** color (`#2ECC71` positive / `#E74C3C` negative); zero or absent =
  dim `—`; warn amber `#F1C40F`.
- IM-band health (rail dots and any future gauges): < 50 % normal · 50–70 % amber · ≥ 70 % red.
- Conversion currency: USD in the mocks; hero currency selection (USD / EUR / BTC / USDT) and the BTC-equivalent line
  are phase 2 — the latter needs a BTCUSDT price from the public trade feed.
- Masking (eye): every figure in the panel renders as `••••`; state persists.
- Panel states: `NoCredentials` ("No API credentials (--api-keyfile)") / `Loading` / `Live`.
- Palette: `ui_builder.cpp` tokens (content 30,30,35 · header 40,40,45 · footer 35,35,40 · divider 80,80,90 ·
  label 200 · dim 128) plus semantic: bright `#ECECF0`, accent `#6496E6`, hairline `#33333A`, pos/neg/warn above.

## Data & view-model requirements

- `WalletCardViewModel` (cockpit layer, UI-toolkit-free, unit-testable): `state`, `scope_title`, `updated`,
  `equity`, `btc_equiv`, `upl` (+ sign class), `coin_count`, rows `{sym, amount, usd, upl, sign}` pre-sorted,
  `hidden {count, sum}`.
- Rail model: `{group, name, badge, equity, im_band}` per (account, wallet type), plus the aggregate row.
- Scope = "All accounts" or one (member, accountType); selecting swaps the whole card content. `updated` is
  per-scope — subaccount balances can be REST-only while the main account also gets WS pushes, so staleness differs
  per scope.
- Data-layer prerequisites tracked in the wallet research (§3 of `wallet_panel_research.md`) still hold: per-coin
  feed keying / partial-push merge (G6), periodic REST re-query for UPL freshness (G7), `HasPrivateAccess()` (G8),
  and `accountIMRate` on the entity (G5) — the rail dots need it even though the card no longer draws margin.
