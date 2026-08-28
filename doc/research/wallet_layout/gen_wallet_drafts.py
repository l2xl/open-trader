#!/usr/bin/env python3
"""Wallet panel layout drafts — SVG mock generator (research tooling, not application code).

Emits the kept wallet-panel SVG mocks that live next to this script
(doc/research/wallet_layout/): the chosen draft-B composition, the wallet-card
template, and the future-use summary strip. One shared sample-data model keeps every total
consistent across drafts; tweak data or geometry here and re-run rather than editing SVGs.
Palette mirrors src/app/ui_builder.cpp; type rules mirror wallet_panel_research.md §4.3.

Usage: python3 gen_wallet_drafts.py   (rewrites the SVG mocks beside the script)
Decision & implementation notes: src/app/WALLET_PANEL.md
"""

import os
import textwrap

OUT = os.path.dirname(os.path.abspath(__file__))

# ---------------------------------------------------------------- palette ----
CANVAS   = "#121216"   # review-canvas behind the panel mock
PANEL_BR = "#3A3A42"   # panel outline on the canvas
HDR_BG   = "#28282D"   # ui_builder header_bg (40,40,45)
FTR_BG   = "#232328"   # ui_builder footer_bg (35,35,40)
BG       = "#1E1E23"   # ui_builder content_bg (30,30,35)
CARD     = "#26262C"
CARD_HI  = "#2A2A31"
HAIR     = "#33333A"   # minor rules
DIV      = "#50505A"   # ui_builder divider (80,80,90)
DIM      = "#808080"   # dim text (128)
LAB      = "#C8C8C8"   # label text (200)
BRIGHT   = "#ECECF0"   # hero / emphasis
ACC      = "#6496E6"   # accent (100,150,230 — ui_builder loading tint)
ACC_BG   = "#24344F"
ACC_TX   = "#CFE0FF"
POS      = "#2ECC71"
NEG      = "#E74C3C"
WRN      = "#F1C40F"
TRACK    = "#33333A"
CAPT     = "#9A9AA5"

FONT = "Open Sans, Segoe UI, DejaVu Sans, sans-serif"

BRAND = {"BTC": "#F7931A", "ETH": "#627EEA", "USDT": "#26A17B", "USDC": "#2775CA",
         "SOL": "#9945FF", "BNB": "#B98A18", "XRP": "#7B8A93", "DOGE": "#C2A633", "TON": "#0098EA"}

MINUS = "−"
DOT = "·"          # ·
ARR_UP = "▲"       # ▲ (drawn as polygon instead where it matters)

# ------------------------------------------------------------- sample data ---
PRICE = {"BTC": 67030.00, "ETH": 3244.60, "SOL": 158.42, "XRP": 0.6789,
         "DOGE": 0.1248, "BNB": 572.30, "TON": 5.62, "USDT": 0.9999, "USDC": 0.9999}
DP = {"USDT": 2, "USDC": 2, "XRP": 2, "DOGE": 2}          # display decimals (default 8)

# account order: key, group, wallet-name, badge, is-derivatives
ACCTS = [
    ("uta",   "Main",        "Unified",  "U", True),
    ("fund",  "Main",        "Funding",  "F", False),
    ("algo",  "Subaccounts", "algo-grid-01", "U", True),
    ("hedge", "Subaccounts", "hedge-desk",   "F", False),
]
AK = [a[0] for a in ACCTS]

# per-coin amounts per account
HOLD = {
    "BTC":  {"uta": 0.52310, "fund": 0.20000},
    "ETH":  {"uta": 6.20000, "algo": 2.25000},
    "USDT": {"uta": 24850.30, "fund": 2120.65, "algo": 2410.00, "hedge": 2024.60},
    "USDC": {"uta": 9596.00, "hedge": 2414.00},
    "SOL":  {"uta": 41.25000},
    "BNB":  {"uta": 2.35000},
    "XRP":  {"fund": 1850.00},
    "DOGE": {"fund": 3500.00},
    "TON":  {"fund": 12.50000},
}
# unrealised PnL (USD) per coin per derivatives account
UPL = {"BTC": {"uta": 812.40}, "ETH": {"uta": 391.20, "algo": -142.20},
       "SOL": {"uta": -102.00}, "BNB": {"uta": 101.50}}

def usd_of(sym, amt):
    return round(amt * PRICE[sym], 2)

COINS = list(HOLD.keys())
coin_amt   = {s: round(sum(HOLD[s].values()), 8) for s in COINS}
coin_usd   = {s: round(sum(usd_of(s, v) for v in HOLD[s].values()), 2) for s in COINS}
coin_upl   = {s: round(sum(UPL.get(s, {}).values()), 2) for s in COINS}
cell_usd   = {s: {a: usd_of(s, HOLD[s].get(a, 0.0)) for a in AK} for s in COINS}
acct_wallet = {a: round(sum(cell_usd[s][a] for s in COINS), 2) for a in AK}
acct_upl    = {a: round(sum(UPL.get(s, {}).get(a, 0.0) for s in COINS), 2) for a in AK}
acct_equity = {a: round(acct_wallet[a] + acct_upl[a], 2) for a in AK}
TOTAL_WALLET = round(sum(acct_wallet.values()), 2)
TOTAL_UPL    = round(sum(acct_upl.values()), 2)
TOTAL_EQ     = round(TOTAL_WALLET + TOTAL_UPL, 2)
BTC_EQ       = TOTAL_EQ / PRICE["BTC"]

ACCT_RISK = {"uta": (12.8, 6.4), "algo": (61.8, 28.4)}      # (IM rate %, MM rate %)
ACCT_AVAIL = {"uta": 52110.00, "algo": 1920.44}
TOTAL_AVAIL = round(sum(ACCT_AVAIL.values()), 2)
DUST_N, DUST_SUM = 3, 0.87                                   # hidden small balances
UPDATED = "12:03:41"

# sorted for display (by USD desc)
ORDER = sorted(COINS, key=lambda s: -coin_usd[s])

# ------------------------------------------------------------- formatting ----
def num(v, dp=2):
    s = f"{abs(v):,.{dp}f}"
    return (MINUS if v < 0 else "") + s

def amt(sym, v=None):
    if v is None:
        v = coin_amt[sym]
    return num(v, DP.get(sym, 8))

def signed(v, dp=2):
    return ("+" if v >= 0 else MINUS) + f"{abs(v):,.{dp}f}"

def pnl_col(v):
    return POS if v > 0 else (NEG if v < 0 else DIM)

def band_col(pct):
    return POS if pct < 50 else (WRN if pct < 70 else NEG)

# --------------------------------------------------------------- svg build ---
class SB:
    def __init__(s, w, h):
        s.w, s.h = w, h
        s.b = []

    def raw(s, t): s.b.append(t)

    def R(s, x, y, w, h, fill, rx=0, stroke=None, sw=1, op=None, dash=None):
        a = f'<rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}" fill="{fill}"'
        if rx: a += f' rx="{rx:g}"'
        if stroke: a += f' stroke="{stroke}" stroke-width="{sw:g}"'
        if dash: a += f' stroke-dasharray="{dash}"'
        if op is not None: a += f' opacity="{op:g}"'
        s.b.append(a + "/>")

    def L(s, x1, y1, x2, y2, stroke=HAIR, sw=1, dash=None):
        a = f'<line x1="{x1:g}" y1="{y1:g}" x2="{x2:g}" y2="{y2:g}" stroke="{stroke}" stroke-width="{sw:g}"'
        if dash: a += f' stroke-dasharray="{dash}"'
        s.b.append(a + "/>")

    def T(s, x, y, txt, size=12, fill=LAB, anchor="start", weight=None, cls=None, spacing=None, family=None):
        txt = str(txt).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
        a = f'<text x="{x:g}" y="{y:g}" font-size="{size:g}" fill="{fill}"'
        if anchor != "start": a += f' text-anchor="{anchor}"'
        if weight: a += f' font-weight="{weight}"'
        if cls: a += f' class="{cls}"'
        if spacing: a += f' letter-spacing="{spacing:g}"'
        if family: a += f' font-family="{family}"'
        s.b.append(a + f">{txt}</text>")

    def C(s, cx, cy, r, fill, stroke=None, sw=1):
        a = f'<circle cx="{cx:g}" cy="{cy:g}" r="{r:g}" fill="{fill}"'
        if stroke: a += f' stroke="{stroke}" stroke-width="{sw:g}"'
        s.b.append(a + "/>")

    def P(s, d, fill="none", stroke=None, sw=1.2):
        a = f'<path d="{d}" fill="{fill}"'
        if stroke: a += f' stroke="{stroke}" stroke-width="{sw:g}" stroke-linecap="round" stroke-linejoin="round"'
        s.b.append(a + "/>")

    def g(s, tr=None, clip=None, opac=None):
        a = "<g"
        if tr: a += f' transform="{tr}"'
        if clip: a += f' clip-path="url(#{clip})"'
        if opac is not None: a += f' opacity="{opac:g}"'
        s.b.append(a + ">")

    def gend(s): s.b.append("</g>")

    def clip(s, cid, x, y, w, h):
        s.b.append(f'<clipPath id="{cid}"><rect x="{x:g}" y="{y:g}" width="{w:g}" height="{h:g}"/></clipPath>')

    # ---- icons ----------------------------------------------------------
    def eye(s, cx, cy, col=DIM):
        s.P(f"M {cx-7:g} {cy:g} Q {cx:g} {cy-6.5:g} {cx+7:g} {cy:g} Q {cx:g} {cy+6.5:g} {cx-7:g} {cy:g} Z", stroke=col, sw=1.2)
        s.C(cx, cy, 2.0, col)

    def search(s, cx, cy, col=DIM):
        s.C(cx - 1.5, cy - 1.5, 4.2, "none", stroke=col, sw=1.3)
        s.L(cx + 1.8, cy + 1.8, cx + 5, cy + 5, stroke=col, sw=1.3)

    def chev(s, cx, cy, col=DIM, up=False):
        d = -3 if up else 3
        s.P(f"M {cx-4:g} {cy-d/2:g} L {cx:g} {cy+d/2:g} L {cx+4:g} {cy-d/2:g}", stroke=col, sw=1.3)

    def tri(s, cx, cy, col, up=True, r=3.4):
        if up:
            s.b.append(f'<path d="M {cx-r:g} {cy+r*0.8:g} L {cx+r:g} {cy+r*0.8:g} L {cx:g} {cy-r:g} Z" fill="{col}"/>')
        else:
            s.b.append(f'<path d="M {cx-r:g} {cy-r*0.8:g} L {cx+r:g} {cy-r*0.8:g} L {cx:g} {cy+r:g} Z" fill="{col}"/>')

    def dots(s, cx, cy, col=DIM):
        for dy in (-4, 0, 4):
            s.C(cx, cy + dy, 1.3, col)

    def coin(s, cx, cy, sym, r=8):
        s.C(cx, cy, r, BRAND.get(sym, "#555"))
        s.T(cx, cy + r * 0.42, sym[0], size=r * 1.05, fill="#FFFFFF", anchor="middle", weight="bold")

    def dot(s, cx, cy, col, r=3):
        s.C(cx, cy, r, col)

    # ---- widgets --------------------------------------------------------
    def chipw(s, label, size=10.5):
        return round(len(label) * size * 0.56) + 18

    def chip(s, x, y, label, sel=False, h=20, size=10.5):
        w = s.chipw(label, size)
        s.R(x, y, w, h, ACC_BG if sel else CARD, rx=h / 2,
            stroke=ACC if sel else HAIR, sw=1)
        s.T(x + w / 2, y + h / 2 + size * 0.36, label, size=size,
            fill=ACC_TX if sel else LAB, anchor="middle")
        return w

    def gauge(s, x, y, w, pct, h=6):
        """IM-usage bar with 70/90/100 band ticks."""
        s.R(x, y, w, h, TRACK, rx=h / 2)
        fw = max(h, w * min(pct, 100) / 100.0)
        s.R(x, y, fw, h, band_col(pct), rx=h / 2)
        for m in (70, 90, 100):
            mx = x + w * m / 100.0
            s.L(mx, y - 2, mx, y + h + 2, stroke=DIM, sw=1)

    def badge(s, x, y, ch, w=13, h=13):
        s.R(x, y, w, h, HAIR, rx=3)
        s.T(x + w / 2, y + h - 3.4, ch, size=8.5, fill="#A8A8B0", anchor="middle", weight="bold")

    def scrollhint(s, x, y, h, th=44):
        s.R(x, y + 4, 3, h - 8, "#2A2A30", rx=1.5)
        s.R(x, y + 8, 3, th, DIV, rx=1.5)

    def fade_right(s, x, y, w, h, gid):
        s.b.append(f'<linearGradient id="{gid}" x1="0" y1="0" x2="1" y2="0">'
                   f'<stop offset="0" stop-color="{BG}" stop-opacity="0"/>'
                   f'<stop offset="1" stop-color="{BG}" stop-opacity="1"/></linearGradient>')
        s.R(x, y, w, h, f"url(#{gid})")

    def searchbox(s, x, y, w, h=22, ph="Search coin"):
        s.R(x, y, w, h, CARD, rx=4, stroke=HAIR)
        s.search(x + 12, y + h / 2 + 1)
        s.T(x + 22, y + h / 2 + 3.6, ph, size=10.5, fill=DIM)

    def checkbox(s, x, y, label, on=True, size=10.5):
        s.R(x, y, 12, 12, ACC_BG if on else CARD, rx=3, stroke=ACC if on else DIV)
        if on:
            s.P(f"M {x+2.7:g} {y+6:g} L {x+5.2:g} {y+8.7:g} L {x+9.4:g} {y+3.2:g}", stroke=ACC_TX, sw=1.5)
        s.T(x + 17, y + 10, label, size=size, fill=LAB)
        return 17 + len(label) * size * 0.56

    def close_split(s, xr, cy):
        """panel chrome buttons: split + close, right-aligned at xr."""
        x = xr - 34
        s.R(x, cy - 5.5, 11, 11, "none", stroke=DIM, sw=1.1)
        s.L(x + 5.5, cy - 5.5, x + 5.5, cy + 5.5, stroke=DIM, sw=1.1)
        x = xr - 14
        s.P(f"M {x-4:g} {cy-4:g} L {x+4:g} {cy+4:g} M {x-4:g} {cy+4:g} L {x+4:g} {cy-4:g}", stroke=DIM, sw=1.2)

    def svg(s, title):
        head = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {s.w} {s.h}" '
                f'width="{s.w}" height="{s.h}" font-family="{FONT}">'
                f"<title>{title}</title>"
                "<style>text{font-variant-numeric:tabular-nums;font-feature-settings:'tnum'}</style>")
        return head + "".join(s.b) + "</svg>"


def panel_header(s, px, py, pw, title):
    s.R(px, py, pw, 26, HDR_BG)
    s.T(px + 12, py + 17, title, size=12, fill=LAB, cls="v-panel-title")
    s.close_split(px + pw - 8, py + 13)
    s.L(px, py + 26, px + pw, py + 26, stroke=HAIR)
    return py + 26


def caption(s, x, y, title, sub):
    s.T(x, y, title, size=14, fill=CAPT, weight="bold")
    s.T(x, y + 17, sub, size=11, fill=DIM)


def wrap_notes(notes, maxw, size=11):
    cpl = max(20, int(maxw / (size * 0.545)))
    lines = []
    for n in notes:
        for i, l in enumerate(textwrap.wrap(n, cpl)):
            lines.append((DOT + "  " + l) if i == 0 else ("   " + l))
    return lines


def notes_h(lines):
    return 30 + len(lines) * 16 + 18


def footnotes(s, x, y, lines):
    for i, l in enumerate(lines):
        s.T(x, y + i * 16, l, size=11, fill=DIM)


# =====================================================================
# Draft B — master–detail: account rail + detail pane (560 x 640)
# =====================================================================
def wallet_card(s, ix, xr, y0, scroll_x=None, scope="All accounts"):
    """The SVG-template "wallet card": scope title + hero + coin list.

    Decision 2026-08-28: this is the whole template. The account selector, panel
    chrome and dust footer are native elements widgets; the KPI strip and the
    margin-usage section were removed from the card per review. The eye, search
    box and hide-toggle are interactive and therefore native overlays at runtime;
    they are drawn here for layout only. Returns the y just below the last row.
    """
    y = y0 + 14
    s.T(ix, y, scope, size=13, fill=BRIGHT, weight="bold", cls="v-scope-title")
    s.T(xr - 24, y, "updated " + UPDATED, size=9.5, fill=DIM, anchor="end", cls="v-updated")
    s.eye(xr - 8, y - 4)
    y += 30
    s.T(ix, y, "$", size=18, fill=DIM, weight="bold")
    s.T(ix + 14, y, num(TOTAL_EQ), size=24, fill=BRIGHT, weight="bold", cls="v-total-equity")
    y += 17
    s.T(ix, y, "≈ " + f"{BTC_EQ:.4f} BTC", size=10.5, fill=DIM, cls="v-btc-equiv")
    s.T(ix + 84, y, signed(TOTAL_UPL) + " unrealised", size=10.5, fill=POS, cls="v-total-upl")
    y += 11
    s.L(ix, y, xr, y, stroke=HAIR)

    y += 10
    s.searchbox(ix, y, 150)
    s.checkbox(ix + 162, y + 5, "hide < $1", on=True)
    s.T(xr, y + 15, "9 / 12", size=9.5, fill=DIM, anchor="end", cls="v-coin-count")
    y += 32
    cA, cV, cU = ix + 158, ix + 258, xr
    s.T(ix, y, "COIN", size=9, fill=DIM, spacing=0.8)
    s.T(cA, y, "AMOUNT", size=9, fill=DIM, anchor="end", spacing=0.8)
    s.T(cV, y, "VALUE " + DOT + " USD", size=9, fill=DIM, anchor="end", spacing=0.8)
    s.T(cU, y, "UPL", size=9, fill=DIM, anchor="end", spacing=0.8)
    y += 6
    rows_top = y
    rh = 24
    for sym in ORDER:
        s.L(ix, y, xr, y, stroke="#26262B")
        s.coin(ix + 8, y + rh / 2, sym, r=7)
        s.T(ix + 21, y + rh / 2 + 3.6, sym, size=10.5, fill=LAB, cls="v-coin-sym")
        s.T(cA, y + rh / 2 + 3.6, amt(sym), size=10.5, fill=LAB, anchor="end", cls="v-coin-amount")
        s.T(cV, y + rh / 2 + 3.6, num(coin_usd[sym]), size=10.5, fill=LAB, anchor="end", cls="v-coin-usd")
        u = coin_upl[sym]
        s.T(cU, y + rh / 2 + 3.6, signed(u) if u else "—", size=10.5,
            fill=pnl_col(u) if u else DIM, anchor="end", cls="v-coin-upl")
        y += rh
    if scroll_x is not None:
        s.scrollhint(scroll_x, rows_top, y - rows_top)
    return y


def draft_b():
    PW, PH = 560, 396
    NOTES = [
        "Account rail = common elements widget, OUT of the SVG template; it owns selection (All / per-wallet / per-subaccount) and feeds scope + data into the card.",
        "Dashed region = the SVG template (wallet_card_template.svg): scope title, hero, coin list. KPI strip and margin-usage section removed per review 2026-08-28.",
        "Panel header and dust footer stay elements chrome; eye / search / hide-toggle are interactive " + "→" + " native overlays above the template (drawn for layout only).",
        "Rail rows: per-account equity + IM-band health dot; legend at rail bottom.",
    ]
    LINES = wrap_notes(NOTES, PW + 24)
    W, H = PW + 80, PH + 76 + notes_h(LINES)
    s = SB(W, H)
    s.R(0, 0, W, H, CANVAS)
    caption(s, 40, 26, "Draft B (chosen) " + MINUS + " native account rail + SVG-template card",
            "rail is a common elements widget; the card is the SVG template " + DOT + " shown 560" + "×" + "396")
    s.T(40, 59, "dashed outline = SVG template (wallet_card_template.svg) " + DOT + " rail & chrome = native elements widgets", size=11, fill=DIM)
    px, py = 40, 76
    s.R(px - 1, py - 1, PW + 2, PH + 2, "none", stroke=PANEL_BR)
    s.R(px, py, PW, PH, BG)
    s.g(tr=f"translate({px},{py})")

    panel_header(s, 0, 0, PW, "Wallet")
    RW = 186                              # rail width (native elements widget)
    s.R(0, 26, RW, PH - 26, FTR_BG)
    s.L(RW, 26, RW, PH, stroke=HAIR)

    # ---- rail (native account selector; not part of the template) ----
    y = 26 + 12
    s.R(0, y - 4, RW, 34, CARD_HI)
    s.R(0, y - 4, 3, 34, ACC)
    s.T(12, y + 9, "All accounts", size=11.5, fill=BRIGHT)
    s.T(RW - 10, y + 24, "$" + num(TOTAL_EQ), size=10.5, fill=LAB, anchor="end")
    y += 42
    for group in ("Main", "Subaccounts"):
        s.T(12, y + 8, group.upper(), size=8.5, fill=DIM, spacing=1.2)
        y += 16
        for key, grp, name, badge, deriv in ACCTS:
            if grp != group:
                continue
            s.badge(12, y + 3, badge)
            s.T(31, y + 13, name, size=11, fill=LAB)
            if key in ACCT_RISK:
                s.dot(RW - 10, y + 9, band_col(ACCT_RISK[key][0]), r=2.5)
            s.T(RW - 10, y + 28, num(acct_equity[key]), size=10, fill=DIM, anchor="end")
            y += 36
        y += 8
    s.L(8, y, RW - 8, y, stroke=HAIR)
    y += 16
    s.T(12, y, "IM usage", size=9, fill=DIM)
    s.dot(70, y - 3, POS, r=2.5); s.T(76, y, "ok", size=9, fill=DIM)
    s.dot(96, y - 3, WRN, r=2.5); s.T(102, y, "50–70", size=9, fill=DIM)
    s.dot(140, y - 3, NEG, r=2.5); s.T(146, y, "≥70", size=9, fill=DIM)

    # ---- the SVG-template card ----
    ix = RW + 14
    xr = PW - 14
    wallet_card(s, ix, xr, 26 + 8, scroll_x=550)
    s.R(RW + 6, 29, PW - 9 - (RW + 6), PH - 24 - 29, "none", stroke=ACC, sw=1, dash="5,4", op=0.7)

    fy = PH - 20
    s.R(RW + 1, fy, PW - RW - 1, 20, FTR_BG)
    s.L(RW + 1, fy, PW, fy, stroke=HAIR)
    s.T(ix, fy + 14, f"{DUST_N} small balances hidden (< $1.00)", size=9.5, fill=DIM)
    s.gend()

    footnotes(s, 40, py + PH + 30, LINES)
    return s.svg("Wallet draft B " + MINUS + " native rail + template card")


def wallet_card_template():
    PW, PH = 380, 360
    NOTES = [
        "The template ONLY: ThorVG renders this scene per selected scope; panel chrome, the account-selector widget, footer and interactive controls (eye, search, hide-toggle) are native elements " + MINUS + " controls are drawn here for layout only.",
        "Injection classes: v-scope-title, v-updated, v-total-equity, v-btc-equiv, v-total-upl, v-coin-count, and per-row v-coin-sym / v-coin-amount / v-coin-usd / v-coin-upl (row group stamped per coin).",
        "Same layout for any scope: a single Funding account renders UPL as — and drops the unrealised part of the secondary line.",
        "Conservative SVG diet for the ThorVG loader: rect / circle / path / line / text; no gradients, filters or masks in this file.",
    ]
    LINES = wrap_notes(NOTES, PW + 24)
    W, H = PW + 80, PH + 58 + notes_h(LINES)
    s = SB(W, H)
    s.R(0, 0, W, H, CANVAS)
    caption(s, 40, 26, "Wallet card " + MINUS + " the SVG template",
            "scope title + hero + coin list; everything else is native " + DOT + " shown 380" + "×" + "360")
    px, py = 40, 58
    s.R(px - 1, py - 1, PW + 2, PH + 2, "none", stroke=PANEL_BR)
    s.R(px, py, PW, PH, BG)
    s.g(tr=f"translate({px},{py})")
    wallet_card(s, 12, PW - 12, 12, scroll_x=PW - 6)
    s.gend()
    footnotes(s, 40, py + PH + 30, LINES)
    return s.svg("Wallet card SVG template")


# =====================================================================
# Draft E — summary strip (960 x 84)
# =====================================================================
def draft_e():
    PW, PH = 1200, 84
    NOTES = [
        "The MT4 / cTrader account-status idiom: equity " + "→" + " available " + "→" + " PnL " + "→" + " margin, then top holdings as ticker cells.",
        "Fits a shallow bottom/top dock next to charts; scope dropdown switches account; ‹+ 5 more› opens the full panel.",
        "Reserved for future use " + MINUS + " a companion glance strip to the wallet panel, not part of this iteration.",
    ]
    LINES = wrap_notes(NOTES, PW + 24)
    W, H = PW + 80, PH + 58 + notes_h(LINES)
    s = SB(W, H)
    s.R(0, 0, W, H, CANVAS)
    caption(s, 40, 26, "Draft E " + MINUS + " Summary strip " + DOT + " horizontal dock",
            "MT4-style account status strip for a bottom / top dock " + DOT + " shown 1200" + "×" + "84")
    px, py = 40, 58
    s.R(px - 1, py - 1, PW + 2, PH + 2, "none", stroke=PANEL_BR)
    s.R(px, py, PW, PH, BG)
    s.g(tr=f"translate({px},{py})")

    def sep(x):
        s.L(x, 12, x, PH - 12, stroke=HAIR)

    # scope cell
    x = 14
    s.T(x, 26, "WALLET", size=9, fill=DIM, spacing=1.2)
    s.R(x, 38, 74, 22, CARD, rx=4, stroke=HAIR)
    s.T(x + 8, 53, "All", size=10.5, fill=LAB)
    s.chev(x + 62, 49)
    x += 92; sep(x); x += 14

    def kpi(x, label, value, col=BRIGHT, sub=None, subcol=DIM, vsize=15):
        s.T(x, 26, label, size=9, fill=DIM, spacing=1.0)
        s.T(x, 48, value, size=vsize, fill=col, weight="bold", cls="v-kpi")
        if sub:
            s.T(x, 64, sub, size=9.5, fill=subcol)

    kpi(x, "TOTAL EQUITY", "$" + num(TOTAL_EQ), sub="≈ " + f"{BTC_EQ:.4f} BTC")
    x += 138; sep(x); x += 14
    kpi(x, "AVAILABLE", "$" + num(TOTAL_AVAIL), col=LAB)
    x += 112; sep(x); x += 14
    kpi(x, "UNREALISED PNL", signed(TOTAL_UPL), col=POS, sub=f"+{TOTAL_UPL / TOTAL_WALLET * 100:.2f}%", subcol=POS)
    x += 116; sep(x); x += 14

    # margin micro-bars
    s.T(x, 26, "MARGIN " + DOT + " IM", size=9, fill=DIM, spacing=1.0)
    for i, (key, label) in enumerate((("uta", "M" + DOT + "U"), ("algo", "algo"))):
        im = ACCT_RISK[key][0]
        yy = 38 + i * 15
        s.T(x, yy + 7, label, size=8.5, fill=DIM)
        s.gauge(x + 26, yy, 58, im, h=5)
        s.T(x + 130, yy + 6, f"{im:.1f}%", size=9, fill=band_col(im), anchor="end")
    x += 140; sep(x); x += 14

    # top coins
    for sym in ORDER[:4]:
        s.coin(x + 8, 32, sym, r=8)
        s.T(x + 22, 36, sym, size=10.5, fill=LAB)
        s.T(x, 52, amt(sym), size=9.5, fill=LAB)
        s.T(x, 66, num(coin_usd[sym], 0), size=9, fill=DIM)
        x += 94
        sep(x - 8)
    x += 4
    s.T(x, 46, "+ 5 more ⋯", size=10, fill=ACC)
    x += 70; sep(x)
    xe = PW - 14
    s.eye(xe - 8, 26)
    s.T(xe, 50, UPDATED, size=9.5, fill=DIM, anchor="end")
    s.dot(xe - 24, 63, POS, r=2.4)
    s.T(xe, 66, "live", size=8.5, fill=DIM, anchor="end")
    s.gend()

    footnotes(s, 40, py + PH + 30, LINES)
    return s.svg("Wallet draft E " + MINUS + " summary strip")


# --------------------------------------------------------------------- main --
DRAFTS = {
    "draft_b_master_detail.svg": draft_b,
    "wallet_card_template.svg": wallet_card_template,
    "draft_e_summary_strip.svg": draft_e,
}

if __name__ == "__main__":
    for name, fn in DRAFTS.items():
        p = os.path.join(OUT, name)
        with open(p, "w") as f:
            f.write(fn())
        print(p)
    print("--- data check ---")
    print("total wallet", f"{TOTAL_WALLET:,.2f}", "upl", f"{TOTAL_UPL:,.2f}", "equity", f"{TOTAL_EQ:,.2f}", "btc", f"{BTC_EQ:.4f}")
    for a in AK:
        print(f"{a:6s} wallet {acct_wallet[a]:>12,.2f} upl {acct_upl[a]:>9,.2f} equity {acct_equity[a]:>12,.2f}")
