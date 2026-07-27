# Volume-Weighted Spindle Charts

A rendering algorithm for an alternative to the Japanese candlestick, where each
period is drawn as a *spindle* (a violin-like body) whose **silhouette encodes
the intra-period price distribution** and whose **area encodes traded volume**.

---

## 1. The visual model

For a single period the spindle replaces the OHLC candle:

- The **vertical extent** runs from the period low `L` to the period high `H`.
  Both tips are sharp points (the price extremes).
- The **waist** — the widest horizontal slice — sits at the volume-weighted
  average price (VWAP) `μ`. It is the spindle's analogue of the candlestick's
  "average trade" and, on its own, draws a horizontal cross-bar.
- The **wall** between a tip and the waist follows a Gaussian profile, so the
  body looks like a vertical bell rotated 90°.
- The **horizontal width** at any price `y` is proportional to how much trading
  clustered around that price, and the **total area** is proportional to the
  period's traded volume.

The result reads at a glance: tall thin spindles are wide-ranging low-conviction
periods; short fat spindles are tight high-volume periods; a waist pinned to one
end signals a strongly skewed session.

---

## 2. Inputs

Per period, the algorithm consumes the intra-period trade stream

```
ticks = [{ price, volume }, ...]
```

If only OHLCV is available, `μ` and `σ` can be substituted with proxies (e.g.
`μ ≈ (H+L+2C)/4`, `σ ≈ (H−L)/4`), but the trade stream gives a faithful
distribution and is preferred.

A second input is the chart's **vertical scale**

$$s_y = \frac{\text{plot height in px}}{\text{price span}} \quad [\text{px per price unit}]$$

because the degenerate-spindle test (§5) is defined in screen space, not price
space.

---

## 3. Volume-weighted statistics

A single pass over the ticks yields everything the shape needs.

**Total volume**

$$V = \sum_i v_i$$

**Waist (VWAP)**

$$\mu = \frac{\sum_i p_i\,v_i}{V}$$

**Volume-weighted standard deviation** — the spread of trades around the waist,
which fixes how quickly the bell decays:

$$\sigma = \sqrt{\frac{\sum_i v_i\,(p_i - \mu)^2}{V}}$$

`σ` is clamped to a small positive floor to keep later divisions finite when all
trades land on one price.

---

## 4. From volume to width: the area constraint

The wall is a Gaussian half-width centred on the waist:

$$w(y) = A \, \exp\!\left(-\frac{(y-\mu)^2}{2\sigma^2}\right)$$

where `A` is the peak half-width (the half-width at the waist). The full spindle
is the mirror of `w(y)` about the centre line, so its rendered area is

$$\text{Area}_{px} = \int 2\,w_{px}(y)\,\mathrm{d}y_{px}
   = 2 A_{px}\, s_y \,\sigma \sqrt{2\pi}.$$

Requiring `Area ∝ V` and folding the constants `2√(2π)` into a single
calibration factor `K` gives the core formula:

$$\boxed{\,A_{px} = K \cdot \dfrac{V}{\sigma \cdot s_y}\,}$$

Read it directly: more volume widens the body; a larger spread (`σ`) spreads the
same volume over more height and therefore narrows it; zooming the price axis
(larger `s_y`) shrinks the px width so that *screen* area, not price area, tracks
volume.

### Calibration of K

`K` is solved once for the whole visible window so widths are comparable across
the chart. Pick a target peak width `Wₜ` (e.g. 14 px) for a representative
spindle and read it off the dataset medians:

$$K = W_t \cdot \frac{\operatorname{median}_j (\sigma_j\, s_y)}{\operatorname{median}_j (V_j)}$$

Medians are robust to the degenerate outliers handled in §5, so a single pass is
enough; a second pass excluding flagged spindles is optional.

---

## 5. Classification

Before drawing, each spindle is sorted into one of three regimes. Let

$$\text{top}_{px} = (H-\mu)\,s_y, \qquad
  \text{bot}_{px} = (\mu-L)\,s_y, \qquad
  \text{fullWidth} = 2 A_{px}.$$

**Special (degenerate).** A short-range, high-volume period forces `A_{px}`
large; the body becomes wider than it is tall and the Gaussian wall can no
longer be read. When even the longer half is shorter than two body widths,

$$\max(\text{top}_{px},\,\text{bot}_{px}) < \texttt{ASPECT} \cdot \text{fullWidth},
  \qquad \texttt{ASPECT}=2,$$

the spindle is removed from the area normalization and stamped with a
**fixed-size, high-contrast marker** (a bright lozenge). Its area is, by
convention, the chart's *unit* — it is the case that cannot be scaled by height,
so it is not scaled at all.

**Half-normal (skewed waist).** When the waist hugs one extreme,

$$\frac{H-\mu}{H-L} < \texttt{SKEW} \quad\text{or}\quad
  \frac{\mu-L}{H-L} < \texttt{SKEW}, \qquad \texttt{SKEW}=0.15,$$

the period is interpreted as **half a bell**: the flat, widest side sits at the
waist (near the crowded extreme) and the body tapers to a single point at the
far tip.

**Normal.** Everything else — a full two-sided spindle pinched to points at both
`H` and `L`.

---

## 6. Building the contour with splines

The wall is sampled, then smoothed, rather than evaluated analytically — exact
shape is not required, only a convincing bell.

1. **Sample.** Take `N+1` price levels from `H` to `L`. At each level `y`
   compute the right-hand point `(cx + w(y), y_px(y))`. The tip levels are
   pinned to `w = 0` so the spindle ends in points.
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

The position of `±1σ` is drawn as faint horizontal ticks at
`(cx ± w(μ±σ), y_px(μ±σ))`, giving the standard deviation a visible place on the
body.

---

## 7. Algorithm summary

```
for each period:
    s        = stats(ticks)              # μ, σ, V, H, L      — O(n)
K            = calibrate(all stats, s_y) # once per window
for each period:
    cls      = classify(s, s_y, K)       # normal | half | special
    if special:
        draw fixed marker                # bright, area = 1 unit
    else:
        A    = K * V / (σ * s_y)          # peak half-width
        pts  = sample Gaussian wall (pinned tips; flat side if half)
        path = catmull_rom_to_bezier(pts)
        draw path + waist line + ±1σ ticks
```

Per period the cost is one `O(n)` pass over ticks for the statistics and a
constant `O(N)` for the contour, so the whole window is linear in total ticks —
no acceleration structure required.

---

## 8. Tunable parameters

| Symbol | Default | Effect |
| --- | --- | --- |
| `Wₜ` (target peak width) | 14 px | overall body thickness via `K` |
| `ASPECT` | 2 | aspect ratio below which a spindle becomes *special* |
| `SKEW` | 0.15 | waist-to-extreme fraction below which it becomes *half-normal* |
| `N` (contour samples) | 12 | wall smoothness |
| `σ` floor | 1e-6 | guards single-price periods |

All thresholds are screen-space and dataset-relative, so the encoding stays
legible across instruments, timeframes, and zoom levels.
