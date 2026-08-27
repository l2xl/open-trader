# Volume-Weighted Buoy Candles

A rendering algorithm for an alternative to the Japanese candlestick, where each
period is drawn as a *buoy* — a float riding the price axis, widest at its
waterline — whose **silhouette encodes the intra-period price distribution**
and whose **area encodes traded volume**.

---

## 1. The visual model

For a single period the buoy replaces the OHLC candle:

- The **vertical extent** runs from the period low `L` to the period high `H`.
  Both tips are sharp points (the price extremes).
- The **waist** — the widest horizontal slice, the buoy's waterline — sits at the volume-weighted
  average price (VWAP) `μ`. Because the VWAP is the single most important value
  of the period, it is additionally marked by a **fixed-size horizontal line**
  (one slot wide, `LINE` px thick in screen pixels) drawn on top of the body, so
  the level stays crisply readable at any zoom. That marker is a *prefiltered*
  line — §9.
- The **wall** between a tip and the waist follows a Gaussian profile, so the
  body looks like a vertical bell rotated 90°.
- The buoy is **not vertically symmetric**. The waist sits at the volume-weighted
  mean — not midway between `H` and `L` — and the two half-bells are computed
  separately: the upper wall decays with `σ⁺`, the spread of the trades above
  `μ`, and the lower wall with `σ⁻`, the spread below. A half whose volume
  clusters tight to the mean pinches quickly toward its tip; a half whose
  volume spreads toward the extreme stays full.
- The contour is always the fitted two-piece normal model — a monotone decay
  from the waist to each tip. However uneven the actual intra-period trading
  was, the wall never traces the raw volume-by-price profile with its local
  bulges and pinches; the distribution is always *modelled* as normal.
- The **total area** of the body is proportional to the period's **traded
  volume**. The peak half-width at the waist (the amplitude, may be individual per each of
  halves) follows from that area constraint, so at any price level the width
  reads as the model's volume density around that price.

The result reads at a glance: tall thin buoys are wide-ranging low-conviction
periods; short fat buoys are tight high-volume periods; a waist pinned to one
end signals a strongly skewed session — and the fuller half shows which side of
the mean absorbed the spread.

---

## 2. Inputs

Per period, the algorithm consumes the intra-period trade stream
at 
```
ticks = [{ price, volume }, ...]
```

If only OHLCV is available, `μ` and `σ±` can be substituted with proxies (e.g.
`μ ≈ (H+L+2C)/4`, `σ⁺ ≈ (H−μ)/2`, `σ⁻ ≈ (μ−L)/2`), but the trade stream gives
a faithful distribution and is preferred.

A second input is the chart's **vertical scale**

$$s_y = \frac{\text{plot height in px}}{\text{price span}} \quad [\text{px per price unit}]$$

because the non-overlap cap (§4) and the degenerate-buoy test (§5) are defined
in screen space, not price space. It also appears in the width formula, but
there it cancels against the calibration factor — see *Scale invariance* in §4.

---

## 3. Volume-weighted statistics

One pass over the ticks yields the aggregate statistics; the per-side spreads
additionally depend on the final mean (see the computation note below).

**Total volume**

$$V = \sum_i v_i$$

**Waist (VWAP)**

$$\mu = \frac{\sum_i p_i\,v_i}{V}$$

**Per-side volume-weighted deviation** — the spread of each side's trades
around the waist, which fixes how quickly that half of the bell decays:

$$\sigma^{+} = \sqrt{\frac{\sum_{p_i \ge \mu} v_i\,(p_i - \mu)^2}{V^{+}}}, \qquad
  \sigma^{-} = \sqrt{\frac{\sum_{p_i < \mu} v_i\,(p_i - \mu)^2}{V^{-}}}$$

where `V⁺` / `V⁻` is the volume traded on that side of `μ`. Each `σ±` is
clamped to a small positive floor to keep later divisions finite when a side is
empty or all its trades land on one price.

*Computation note.* The side split depends on the final `μ`, so exact `σ±`
take a second pass over the period's ticks. Where only online accumulation is
possible, per-side raw moments (`V±`, `Σv·p`, `Σv·p²`) classified against the
running mean give an approximation acceptable for a visual encoding; closed
periods can be recomputed exactly wherever the tick stream is retained.

---

## 4. From volume to width: the area constraint

### Fitting the bell inside the range

The raw `σ±` of §3 describe the trades, not a drawable bell. In the extreme
two-trade period they degenerate to `σ⁺ = H−μ` and `σ⁻ = μ−L` exactly, so the
Gaussian would still be at `e^{−1/2} ≈ 61 %` of its waist width *at the tip* —
the §6 tip pin would then chop a fat wall into an artificial point, and the
area formula below would count tail ink that is never drawn. The model is
therefore fitted to the range before anything else: declare the bell "zero"
at `TAIL` standard deviations and clamp each side so that point never lands
beyond its tip,

$$\sigma^{+} \leftarrow \min\!\left(\sigma^{+},\, \frac{H-\mu}{\texttt{TAIL}}\right), \qquad
  \sigma^{-} \leftarrow \min\!\left(\sigma^{-},\, \frac{\mu-L}{\texttt{TAIL}}\right), \qquad
  \texttt{TAIL}=3.$$

At `TAIL = 3` the residual width at a tip is `e^{−9/2} ≈ 1.1 %` of the waist,
so the tip pin trims something invisible and the untruncated area formula
holds to ~1 %. The clamp is deliberately one-sided: when `TAIL·σ` falls well
inside the tip (a lone spike wick), the long thin taper is an honest picture
of the distribution and is left alone.

No separate area compensation is needed. The width formula below has
`A_px ∝ V/(σ^{+}+σ^{-})`, so shrinking the σ's re-inflates the shared waist by
exactly the factor that preserves `Area ∝ V` — jointly across both halves,
which is the only way to compensate without breaking the seam: the halves
share one amplitude, so per-half rescaling is not available. Everything
downstream — the width formula, the K-calibration medians, the ±1σ ticks, the
half-normal wall — consumes the clamped, fitted `σ±`, never the raw ones.

Each wall is a Gaussian half-width centred on the waist, evaluated with the σ
of its own side:

$$w(y) = A_{px} \, \exp\!\left(-\frac{(y-\mu)^2}{2\sigma_{\pm}^2}\right), \qquad
  \sigma_{\pm} = \begin{cases}\sigma^{+} & y > \mu \\ \sigma^{-} & y \le \mu\end{cases}$$

where `A_px` is the peak half-width (the half-width at the waist). The two
halves meet at the waist at the same width `A_px` and with zero slope — a
Gaussian is flat at its peak whatever its σ — so the split never shows a seam.
The rendered area of the mirrored body is

$$\text{Area}_{px} = \sqrt{2\pi}\; A_{px}\, s_y \,(\sigma^{+}+\sigma^{-}).$$

Requiring `Area ∝ V`, folding `√(2π)` into the calibration factor `K`, and
capping so neighbouring buoys can never overlap gives the core formula:

$$\boxed{\,A_{px} = \min\!\left(\frac{K \cdot V}{s_y\,(\sigma^{+}+\sigma^{-})},\; A_{slot}\right)}, \qquad
  A_{slot} = \frac{\text{slot\_width}_{px} - \texttt{gap}}{2},$$

with `slot_width` the horizontal span one period occupies on screen.

Read it directly: more volume means more ink; a larger spread stretches the
same volume over more height and therefore narrows the body. The `s_y` in the
denominator is what makes *screen* area, not price area, track volume — at a
fixed `K`. But `K` is recalibrated per window and carries its own factor of
`s_y`, so the two cancel and the width ends up invariant under price-axis zoom;
see *Scale invariance* below. The waist width alone reads as volume *density* —
volume per unit of price range — while the total ink is the volume itself.
Since volume and volatility co-move empirically (`σ` grows roughly like `√V`),
widths grow sublinearly with volume, which naturally damps the heavy tail and
keeps the cap rare.

A buoy truncated to `A_slot` no longer shows its full volume and must
be visually distinguished; the overflow is to be moved onto a **colour
dimension** — an open design question tracked in §10. Until it is resolved,
implementations apply the width clamp alone.

### Calibration of K

`K` is solved once for the whole visible window so widths are comparable across
the chart. Pick a target waist width `Wₜ` (e.g. 14 px) for the representative
buoy and read it off the dataset medians:

$$K = \frac{W_t}{2} \cdot \frac{\operatorname{median}_j\!\big(s_y\,(\sigma^{+}_j+\sigma^{-}_j)\big)}{\operatorname{median}_j (V_j)}$$

The medians run over the visible, non-empty buoys. They are robust to the
degenerate outliers handled in §5, so a single pass is enough; a second pass
excluding flagged buoys is optional.

### Scale invariance

Substituting the calibrated `K` back into the width formula, `s_y` cancels
outright:

$$A_{px} = \frac{W_t}{2}\cdot\frac{V}{\operatorname{median}_j V_j}\cdot\frac{\operatorname{median}_j\big(\sigma^{+}_j+\sigma^{-}_j\big)}{\sigma^{+}+\sigma^{-}}$$

The pre-clamp half-width is therefore the target width scaled by a
**dimensionless ratio** comparing this buoy's volume and spread against the
window medians. It carries no screen units: zooming the price axis does not
change any buoy's width, and the median buoy is always exactly `Wₜ` wide.
Widths are *relative* by construction — they state how one period's volume
density compares with the rest of the window, never anything absolute.

Screen **area** does still grow with `s_y` — a zoomed-in buoy is taller at the
same width — but uniformly across the window, so `Area ∝ V` survives as a
statement about ratios between buoys, which is all the encoding claims.

Two practical consequences. `s_y` reaches the drawing only through the
`A_slot` cap and the §5 aspect test, so a vertical zoom on its own is never a
reason to re-derive `K`; only a change in the window's *contents* is. And the
optional second calibration pass above forfeits this exactness, because the
set of §5-flagged buoys is itself screen-dependent — the single-pass form
preserves it.

---

## 5. Classification

Before drawing, each buoy is sorted into one of three regimes. Let

$$\text{top}_{px} = (H-\mu)\,s_y, \qquad
  \text{bot}_{px} = (\mu-L)\,s_y, \qquad
  \text{fullWidth} = 2 A_{px}.$$

**Special (degenerate).** A tight-range, high-volume period packs its volume
into little height and forces `A_px` large; the body becomes wider
than it is tall and the Gaussian wall can no longer be read. When even the
longer half is shorter than two body widths,

$$\max(\text{top}_{px},\,\text{bot}_{px}) < \texttt{ASPECT} \cdot \text{fullWidth},
  \qquad \texttt{ASPECT}=2,$$

the buoy is excluded from the width calibration (§4) and stamped with the
**fixed-size marker** — it is the case that cannot be scaled by height, so it is
not scaled at all. That marker is the waist line itself, in the same colour
channel a readable buoy's waist carries: what a degraded period still says is its
VWAP level and whether that level grew. Its **range spines** (§9) are drawn too —
a period has tips whether or not its distribution can be drawn. A single-price
period (`H = L`) has zero height on both halves and is therefore special by
construction, whatever its volume; its spines are then nominal, of zero length.

**Half-normal (skewed waist).** When the waist hugs one extreme,

$$\frac{H-\mu}{H-L} < \texttt{SKEW} \quad\text{or}\quad
  \frac{\mu-L}{H-L} < \texttt{SKEW}, \qquad \texttt{SKEW}=0.15,$$

the period is interpreted as **half a bell**: the flat, widest side sits at the
waist (near the crowded extreme) and the body tapers to a single point at the
far tip. The single remaining wall decays with the far side's σ.

**Normal.** Everything else — a full two-sided buoy pinched to points at both
`H` and `L`.

---

## 6. Building the contour with splines

The wall is sampled, then smoothed, rather than evaluated analytically — exact
shape is not required, only a convincing bell.

1. **Sample.** Take `N+1` price levels from `H` to `L`, always including `μ`
   itself as a level — a uniform grid generally misses the mean, and with
   asymmetric σ± the smoothing would then interpolate the peak from two
   off-peak neighbours and bulge the waist off the mean price. At each level
   `y` compute the right-hand point `(cx + w(y), y_px(y))`, using the σ of the
   side `y` lies on. The tip levels are pinned to `w = 0` so the buoy ends in
   points — with the §4 range fit this trims at most `e^{−\texttt{TAIL}^2/2}`
   of the waist width, a cosmetic snap rather than a structural cut.
2. **Mirror.** Reflect the interior samples about the centre line `cx` to build
   the left wall, skipping the shared tip points to avoid duplicates.
3. **Smooth.** Convert the ordered points to a path with a closed Catmull-Rom
   spline expressed as cubic Béziers. For consecutive points
   `P₀ P₁ P₂ P₃`, the segment `P₁→P₂` uses control points

   $$C_1 = P_1 + \tfrac{1}{6}(P_2 - P_0), \qquad
     C_2 = P_2 - \tfrac{1}{6}(P_3 - P_1).$$

For the **half-normal** case the two waist samples become the flat side: the
path runs from the waist down/up to the tip and back, and the closing segment
`Z` draws the straight waist edge. The path is left open (not wrapped) so those
two corners stay sharp.

The positions of `+1σ⁺` and `−1σ⁻` are drawn as faint horizontal ticks at
`(cx ± w(μ+σ⁺), y_px(μ+σ⁺))` and `(cx ± w(μ−σ⁻), y_px(μ−σ⁻))` — the same width
`A_px·e^{−1/2}` on both sides, at generally different heights — giving each
standard deviation a visible place on the body.

---

## 7. Algorithm summary

```
for each period:
    s        = stats(ticks)              # μ, σ⁺, σ⁻, V, H, L — O(n), side split per §3
    σ±       = min(σ±, tip± / TAIL)      # fit the bell inside [L, H] (§4)
K            = calibrate(all stats)      # once per window: Wₜ/2 · median(σΣ·s_y) / median(V), clamped σ
for each period:
    A        = min(K * V / (s_y·(σ⁺+σ⁻)), A_slot)   # peak half-width, non-overlap cap
    cls      = classify(s, s_y, A)       # normal | half | special
    draw range spines                    # both halves, always (§9)
    if special:
        draw waist line                  # the fixed-size marker (§5)
    else:
        pts  = sample per-side Gaussian walls (pinned tips; flat side if half)
        path = catmull_rom_to_bezier(pts)
        draw path + waist line + ±1σ ticks
        # A truncated to A_slot → saturation colour cue (§11, open)
```

Per period the cost is one `O(n)` pass over ticks for the raw statistics (a
second pass where exact per-side spreads are wanted, §3) and a constant `O(N)`
for the contour, so the whole window is linear in total ticks — no acceleration
structure required.

---

## 8. Tunable parameters

| Symbol | Default | Effect |
| --- | --- | --- |
| `Wₜ` (target waist width) | 14 px | median-volume buoy's full waist width via `K` |
| `gap` | 2 px | horizontal clearance between neighbours; `A_slot = (slot − gap)/2` |
| `TAIL` | 3 | σ-multiple treated as the bell's zero; clamps `σ±` to `tip±/TAIL` (§4) |
| `ASPECT` | 2 | aspect ratio below which a buoy becomes *special* |
| `SKEW` | 0.15 | waist-to-extreme fraction below which it becomes *half-normal* |
| `N` (contour samples) | 12 | wall smoothness |
| `LINE` (line core) | 2 px | solid core of every prefiltered line (§9) |
| `FLANK` | 1 px | half-alpha prefilter flank on each side of a line (§9) |
| `σ` floor | 1 raw price unit | per side; the fixed-point floor at the instrument's smallest tick — guards `w(y)` and the width division for empty sides and near-single-price periods; the §4 range fit never clamps below it |

All thresholds are screen-space and dataset-relative, so the encoding stays
legible across instruments, timeframes, and zoom levels.

---

## 9. Prefiltered lines

Two of the notation's elements are pure lines: the **waist marker** at `μ`, and
the per-half **range spine** from the waist out to that half's tip, which keeps
the range visible where the 3σ taper thins below a pixel and is the only range
cue a *special* (§5) period has. Both sit on geometry that sweeps continuously
across the pixel grid as the chart scrolls and rescales, and that is precisely
the case a hard-edged hairline handles worst.

Under analytic (box-filtered) coverage a rect exactly one pixel wide alternates
between **one fully covered column** and **two half covered ones** as its centre
crosses a pixel boundary. The ink is conserved; its distribution is not, and the
eye reads the line pulsing between crisp-and-dark and soft-and-pale — the
"especially noticeable when the lines are animated" case *Fast Prefiltered Lines*
(Chan & Durand, GPU Gems 2 ch. 22) opens with.

The two escapes are mutually exclusive. **Snapping** the line to the pixel grid
is crisp but quantises its motion, so it belongs to static rendering (SVG's
`shape-rendering="crispEdges"`), not to an animated one. **Prefiltering** is the
other: widen the line to at least the filter footprint and let a soft skirt
absorb the sub-pixel remainder — the same reasoning as the *hairline* rule Skia
and Direct2D apply below one pixel, where the width is clamped to one pixel and
the *alpha* modulated by the true width rather than the geometry shrunk.

So each line is drawn as a solid **core** plus a half-alpha **flank** on each
side:

- the core is `LINE` = 2 px across. At two pixels at least one column is fully
  covered at *every* sub-pixel offset — `[1, 1]` centred, `[½, 1, ½]` at a
  half-pixel offset — so peak intensity and total ink are both invariant under
  motion. At one pixel the peak halves twice per pixel of travel;
- the flank is `FLANK` = 1 px at half the core's alpha: the two-level
  quantisation of the filter skirt a flat fill can express. It turns the leftover
  edge redistribution into a steady soft edge rather than a change in apparent
  width.

A flank carries its line's own hue and is painted directly under its core, so it
never washes out the line it belongs to, and where it lands on same-coloured ink
it is invisible.

The waist marker is **axis-aligned for the same reason**. It replaced a diamond
of the same footprint, which met the scrolling axis with four slanted edges — the
worst case for horizontal sub-pixel motion — where a horizontal line presents
none at all and only its two short ends ever cross the grid.

---

## 10. The volume colour dimension

Beyond the area encoding, every buoy's fill is to carry a colour component
sampled from its traded volume — a redundant channel for all buoys that survives
where geometry cannot: a buoy clamped to `A_slot` shows the cap's width, not its
volume, so colour becomes its only remaining volume signal.

The design below resolves the direction of the ramp and its construction.
Declared as requirement `BUOY_RENDER-001` and still unimplemented; until it
lands, the geometry encoding alone applies (§4) and the fills stay the flat
per-direction pair the renderer uses today.

### 10.1 Which end means "more"

The perception literature gives three competing priors readers bring to a
colour-to-magnitude mapping when no legend pins it down:

- **dark-is-more.** Readers assume darker colours map to larger quantities, and
  a visualisation that violates the mapping they inferred is measurably slower
  and harder to read (Schloss, Gramazio, Silverman, Parker & Wang, *Mapping
  Color to Meaning in Colormap Data Visualizations*, IEEE TVCG 2019).
- **opaque-is-more.** Readers assume the end that looks *more opaque* — the end
  furthest from the background — is the larger one (Sibrel, Rathore, Lessard &
  Schloss, *The relation between color and spatial structure for interpreting
  colormap data visualizations*, Journal of Vision 2020; Schloss et al.,
  *Understanding the opaque-is-more bias and saturated-is-more bias for colormap
  data visualizations*, Attention, Perception & Psychophysics 2025).
- **saturated-is-more / high-chroma-is-more.** More chroma reads as more, and it
  fires even without much lightness variation (same 2025 paper).

The decisive result is how the first two interact. **They agree on a light
background — dark is both darker and more opaque — and conflict on a dark one,
where opaque-is-more can negate or even supersede dark-is-more.** On black, the
*lighter*, more solid-looking end is what reads as "more".

The buoy chart is dark-themed, so:

> **bright and chromatic = high volume; dark and desaturated = low volume.**

Three things converge on that direction:

1. on a dark ground both opaque-is-more and saturated-is-more point that way,
   and they are what override dark-is-more there;
2. the geometry channel already gives a high-volume buoy *more* area. Encoding
   it with *less* ink would set the two channels fighting; ramping brightness
   upward makes colour redundant with area, which is the whole point of a
   redundant channel;
3. a low-volume period *should* recede — a dark, low-chroma buoy is quiet on
   screen, which is the correct visual weight for a period nothing traded in.

The published background-robust advice — prefer a ramp that never *looks* like
it varies in opacity, and put "more" at the dark end — is written for colormaps
that must survive a background swap. It does not apply here unchanged: the dark
end of any green or red ramp on near-black necessarily reads as fading toward
transparent. The corollary is that **a light theme must invert the mapping**
(dark = more), because on white the two biases agree again.

### 10.2 More on the chroma / saturation cue

The saturation half of the encoding deserves its own note, because it is the
part that is easiest to get wrong and the part the literature has moved on
most recently.

- Chroma carries magnitude **on its own**. The opaque-is-more bias was
  originally explained as a lightness-against-background effect, but it fires
  even when a ramp has little lightness variation — which is what exposed a
  separate **saturated-is-more** (equivalently **high-chroma-is-more**) bias:
  readers infer that regions greater in saturation map to larger magnitudes
  (Schloss lab, JOV 2022 and Atten Percept Psychophys 2025).
- It is **not** a bare contrast effect. Background colour has little to no
  effect when the colours in a visualisation do not appear to vary in opacity,
  so what drives the inference is apparent opacity/solidity, not raw
  luminance distance from the ground. Practically: a ramp that keeps its chroma
  high along its whole length is *less* background-sensitive than one that fades
  to grey at one end.
- Chroma and lightness must move **together and in the same direction**. A
  single-hue sequential palette varies from dark-and-colourful to
  light-and-greyish (or the reverse) at constant hue; the monotone co-variation
  is what makes steps read as ordered rather than as separate categories. Ramping
  chroma *against* lightness produces a scale readers order inconsistently.
- Chroma cannot carry the fine end of the scale. Lightness is the channel with
  the discrimination bandwidth; chroma reinforces it. Around **five** steps is
  the practical limit for reading magnitude off a lightness ramp on marks this
  small, and distinguishability degrades further as soon as the lightness
  progression stops being perceptually linear — which is the whole argument for
  building the ramp in a uniform space rather than in HSL or sRGB.
- The gamut ceiling is not flat. Maximum available chroma rises steeply with
  lightness and differs per hue: at `L = 0.32` the sRGB ceiling is `C ≈ 0.10` for
  hue 145° but `C ≈ 0.13` for hue 30°; by `L = 0.62` it is `0.20` and `0.25`.
  A ramp with a *fixed* chroma slope therefore drifts away from the gamut edge
  and looks progressively duller than the retired flat pair (which sat at 100 %
  of the edge, being pure single-channel sRGB). Expressing chroma as a
  **fraction of the per-`(L, hue)` ceiling** keeps the vividness constant
  instead.

### 10.3 The colour set

Built in OKLCH, which is perceptually uniform enough that a fixed step in `L` is
a fixed perceived step.

Let `q ∈ [0, 1]` be the buoy's **rank in the visible window's volume
distribution** — the same window §4's width calibration is taken over. Rank, not
raw volume: traded volume is heavy-tailed, so a ramp linear in volume parks
almost every buoy at the bottom and a single large print at the top.
`log(V / median V)` clamped to a fixed span is an equivalent choice, and reuses
the median the width calibration already computes.

| | bullish | bearish |
| --- | --- | --- |
| hue | 145° | 30° |
| body lightness | `L = 0.34 + 0.34·q` | `L = 0.26 + 0.34·q` |
| chroma (both) | `C = 0.80 · Cmax(L, hue)` | `C = 0.80 · Cmax(L, hue)` |
| waist marker / range spine | body `L + 0.13`, same chroma rule | body `L + 0.13`, same chroma rule |

`Cmax(L, hue)` is the largest chroma that stays inside sRGB at that lightness and
hue — found by bisection, since OKLCH has no closed-form gamut boundary. The
0.80 fraction is what keeps the ramp as vivid as the retired flat pair while
leaving enough headroom that gamut clipping never flattens a step.

Sampled at five steps:

| `q` | bull body | bull marker | bear body | bear marker |
| --- | --- | --- | --- | --- |
| 0.00 | `#154319` | `#266b2d` | `#440e08` | `#7a2117` |
| 0.25 | `#205d26` | `#32873a` | `#671a12` | `#a02e21` |
| **0.50** | **`#2c7833`** | **`#3fa548`** | **`#8c271c`** | **`#c83c2c`** |
| 0.75 | `#389541` | `#4cc357` | `#b33426` | `#ec523f` |
| 1.00 | `#45b34f` | `#59e266` | `#db4231` | `#f08575` |

**The `q = 0.5` row is what the renderer ships today** as its flat, volume-blind
default pair — so when the ramp lands it slots in continuously, the midpoint
buoy keeping exactly the colour it had.

Measured properties of that ramp (sRGB relative-luminance contrast ratios):

| `q` | body vs black | marker vs its body | deuteranope `ΔL` |
| --- | --- | --- | --- |
| 0.00 | 1.85 / 1.31 | 1.74 / 1.57 | 0.075 |
| 0.25 | 2.66 / 1.73 | 1.76 / 1.68 | 0.071 |
| 0.50 | 3.84 / 2.42 | 1.74 / 1.71 | 0.068 |
| 0.75 | 5.54 / 3.44 | 1.67 / 1.70 | 0.069 |
| 1.00 | 7.81 / 4.84 | 1.60 / 1.72 | 0.067 |

(bullish / bearish; the retired flat pair scored 1.69 / 1.20 against black and
1.56 / 1.23 for marker-over-body — the bearish body barely cleared the
background at all, which is what made the old palette read as too dark.)

Notes on the construction:

- the marker offset is a *relative* one, so the waist line and the range spines
  keep the same contrast against their own body at every volume;
- the two directions ride **offset lightness lanes** — bullish 0.08 `L` above
  bearish at equal volume. Within one direction lightness is monotone in volume;
  across directions the constant offset is the only cue a red-green dichromat
  has left. It is deliberately sized to reproduce the ≈0.078 lightness
  separation the retired `#003e00` / `#3e0000` pair happened to carry, measured
  through a Machado et al. (2009) severity-1.0 deuteranope simulation; the ramp
  holds 0.067–0.075 across its whole length;
- five steps is the discrimination limit of §10.2, so a HUD legend should show
  five stops even though the fill itself can vary continuously.

### 10.4 Colour-vision caveat

Red against green is the worst possible pair for the ~8 % of men with deuter- or
protanopia, and §10.3's lanes leave them a lightness cue but no hue cue. A
genuinely colour-universal alternative, worth a settings toggle, keeps the same
lanes, the same chroma rule and the same marker offset, and swaps only the hues
for an Okabe-Ito-style pair — bullish `h = 245°` (blue), bearish `h = 45°`
(vermillion):

| `q` | bull body | bull marker | bear body | bear marker |
| --- | --- | --- | --- | --- |
| 0.00 | `#143b59` | `#245f8c` | `#3b1909` | `#6b3317` |
| 0.25 | `#1e527a` | `#3079b0` | `#5a2a12` | `#8d4522` |
| 0.50 | `#2a6b9d` | `#3c93d5` | `#7b3b1c` | `#b0582d` |
| 0.75 | `#3685c1` | `#5eadee` | `#9d4e27` | `#d56c38` |
| 1.00 | `#429fe7` | `#94c7f3` | `#c16132` | `#f08858` |

Under a deuteranope simulation those stay plainly apart — `#2a6b9d` → `#3a5c99`
against `#7b3b1c` → `#584d07` at the midpoint — where green and red collapse
onto each other (`#2c7833` → `#706738` against `#8c271c` → `#5e5418`, a hue
difference of ~1°). The reference qualitative set to draw hues from is
Okabe-Ito: blue `#0072B2`, bluish green `#009E73`, vermillion `#D55E00`, orange
`#E69F00`, reddish purple `#CC79A7`.

### 10.5 Reproducing the numbers

Everything above is derived, not eyeballed, so it can be regenerated when the
implementation lands:

1. **OKLCH ↔ sRGB.** Björn Ottosson's Oklab: `OKLCH(L, C, h)` → `Oklab(L, C·cos h,
   C·sin h)` → the LMS cube-root matrices → linear RGB → the sRGB transfer
   function. Perceptually uniform in `L`, so a fixed `ΔL` is a fixed perceived
   step.
2. **`Cmax(L, hue)`.** Bisect `C` on "does the result stay inside `[0, 1]` on all
   three linear channels".
3. **Colour-vision simulation.** Machado, Oliveira & Fernandes (2009)
   severity-1.0 matrices applied in *linear* RGB — deuteranopia
   `((0.367322, 0.860646, −0.227968), (0.280085, 0.672501, 0.047413), (−0.011820,
   0.042940, 0.968881))`, with protanopia and tritanopia alongside it in the
   paper.
4. **Contrast ratios.** WCAG relative luminance `0.2126 R + 0.7152 G + 0.0722 B`
   on linearised channels, `(L₁ + 0.05) / (L₂ + 0.05)`.

### 10.6 Still open

- validation against a live feed: whether rank-in-window or log-over-median
  gives the steadier picture while the window scrolls, and how much hysteresis
  the ramp needs so a buoy does not shimmer between shades as its neighbours
  come and go — the width calibration's ~10 % median band is the obvious model;
- whether the waist marker should carry volume at all, or hold one fixed pair so
  the VWAP level stays a constant anchor while only the body ramps;
- the HUD legend: where the five stops live and whether they are labelled in
  volume units or as window quantiles;
- whether the empty-buoy gray and the move connector should sit on the same
  lightness scale, or stay deliberately outside it as "no volume at all".

### 10.7 Sources

Perception of colour-to-magnitude mappings:

- Schloss, Gramazio, Silverman, Parker & Wang, *Mapping Color to Meaning in
  Colormap Data Visualizations*, IEEE TVCG 2019 — the dark-is-more bias.
  <https://schlosslab.discovery.wisc.edu/wp-content/uploads/2018/09/SchlossGramazioSilvermanParkerWanginPress.pdf>
  · <https://pubmed.ncbi.nlm.nih.gov/30188827/>
- Sibrel, Rathore, Lessard & Schloss, *The relation between color and spatial
  structure for interpreting colormap data visualizations*, Journal of Vision
  2020 — background colour, spatial structure, and where dark-is-more breaks.
  <https://www.ncbi.nlm.nih.gov/pmc/articles/PMC7683863/>
- Schloss et al., *Understanding the opaque-is-more bias and saturated-is-more
  bias for colormap data visualizations*, Attention, Perception & Psychophysics
  2025 — the biases conflict on dark backgrounds and opaque-is-more can
  supersede dark-is-more; introduces saturated-is-more.
  <https://link.springer.com/article/10.3758/s13414-025-03172-w>
  · <https://www.ncbi.nlm.nih.gov/pmc/articles/PMC12929228/>
- *Investigation of the opaque-is-more bias reveals a high chroma-is-more bias
  for colormap data visualizations*, Journal of Vision 2022.
  <https://jov.arvojournals.org/article.aspx?articleid=2791962>
- *The dark is more (Dark+) bias in colormap data visualizations with legends*,
  Journal of Vision — the bias survives an explicit legend.
  <https://jov.arvojournals.org/article.aspx?articleid=2550611>
- *Effects of data distribution and granularity on color semantics for colormap
  data visualizations*, arXiv 2309.00131 — why the ramp should follow the data's
  distribution rather than its raw range.
  <https://arxiv.org/abs/2309.00131>
- Schloss Visual Reasoning Lab publication index.
  <https://schlosslab.discovery.wisc.edu/category/publication/>

Colour space and palette construction:

- Sequential colour palette generation using OKLCH (Chris Henrick, Observable) —
  the single-hue lightness+chroma ramp recipe.
  <https://observablehq.com/@clhenrick/sequential-color-palette-generation-using-oklch>
- Zhou & Hansen, *A Survey of Colormaps in Visualization*, IEEE TVCG 2016 —
  monotone luminance as the ordering channel, and step distinguishability.
  <https://pmc.ncbi.nlm.nih.gov/articles/PMC4959790/>
- seaborn, *Choosing color palettes* — practical single-hue sequential guidance.
  <https://seaborn.pydata.org/tutorial/color_palettes.html>

Colour-vision deficiency:

- Tableau, *Examining data viz rules: don't use red/green together*.
  <https://www.tableau.com/blog/examining-data-viz-rules-dont-use-red-green-together>
- EU data-visualisation guide, accessible colour palettes (Okabe-Ito values).
  <https://data.europa.eu/apps/data-visualisation-guide/accessible-colour-palettes>
- Machado, Oliveira & Fernandes, *A physiologically-based model for simulation of
  color vision deficiency*, IEEE TVCG 2009 — the simulation matrices used above.
  <https://www.inf.ufrgs.br/~oliveira/pubs_files/CVD_Simulation/CVD_Simulation.html>

---

## 11. Further research

**Colour saturation cue for the width clamp.** A buoy whose waist is truncated
to `A_slot` shows the cap's width rather than its volume (§4). Once §10 lands,
that buoy's colour carries its true volume and the cue is covered; until then
the clamp is silent, and whether it deserves a separate marking of its own is
open.
