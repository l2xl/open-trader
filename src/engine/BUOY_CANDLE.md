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
  of the period, it is additionally marked by a **fixed-size thin horizontal
  diamond** (one slot wide, half a slot tall, in screen pixels) drawn on top of
  the body, so the level stays crisply readable at any zoom.
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
dimension** — an open design question tracked in §9. Until it is resolved,
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

the buoy is excluded from the width calibration (§4) and stamped with a
**fixed-size, high-contrast marker** (a bright lozenge) — it is the case that
cannot be scaled by height, so it is not scaled at all. A single-price period
(`H = L`) has zero height on both halves and is therefore special by
construction, whatever its volume.

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
    if special:
        draw fixed marker                # bright, fixed size
    else:
        pts  = sample per-side Gaussian walls (pinned tips; flat side if half)
        path = catmull_rom_to_bezier(pts)
        draw path + waist diamond + ±1σ ticks
        # A truncated to A_slot → saturation colour cue (§9, open)
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
| `σ` floor | 1 raw price unit | per side; the fixed-point floor at the instrument's smallest tick — guards `w(y)` and the width division for empty sides and near-single-price periods; the §4 range fit never clamps below it |

All thresholds are screen-space and dataset-relative, so the encoding stays
legible across instruments, timeframes, and zoom levels.

---

## 9. Further research

**Volume colour dimension.** Beyond the area encoding, every buoy's fill is to
carry a colour component sampled from its traded volume — a redundant channel
for all buoys that survives where geometry cannot: a buoy clamped to `A_slot`
shows the cap's width, not its volume, so colour becomes its only remaining
volume signal. Open questions:

- which colour channel encodes volume (fill luminance/saturation ramp, outline
  emphasis, gradient toward the flanks) without fighting the up/down trend
  colouring already used by the implementation;
- whether the ramp should be perceptually linear or logarithmic in volume;
- colour-vision accessibility of the chosen ramp.

Declared as requirement `BUOY_RENDER-001`, deliberately unimplemented until
this research resolves. Until then the geometry encoding alone applies (§4).
