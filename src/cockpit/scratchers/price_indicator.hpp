// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include "scratcher.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

// PriceIndicator overlays the current (last-trade) price marker on top of the chart: a dotted
// horizontal line spanning the inner data rect at the last price, plus a filled label box in the
// price-ruler strip showing that price. The box is coloured green when the last trade sits at or
// above the active candle's volume-weighted mean (up bias) and red below it (down bias). It
// reserves no layout space — the box overlays the strip the PriceRuler already reserved — and
// reads the last price and mean live from panel.QuoteScratcherInstance() on every layout.
//
// Scene structure (attached under the panel's mHudScene, pixel space, added last so it draws on
// top of the candles and rulers):
//   mScene
//   ├─ mLine   — dotted horizontal last-price line across the inner rect
//   ├─ mBox    — filled price-tag rectangle in the price strip, coloured by up/down bias
//   └─ mLabel  — white price text inside the box
class PriceIndicator final : public Scratcher
{
public:
    PriceIndicator() = default;

    void OnAttach(InstrumentPanel& panel) override;
    void OnLayout(InstrumentPanel& panel) override;
    void OnDetach(InstrumentPanel& panel) override;

private:
    tvg_ptr<tvg::Scene> mScene;
    tvg_ptr<tvg::Shape> mLine;
    tvg_ptr<tvg::Shape> mBox;
    tvg_ptr<tvg::Text>  mLabel;
};

}
