// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include "scratcher.hpp"
#include "tvg_ptr.hpp"

namespace scratcher::cockpit {

class PriceRuler final : public Scratcher
{
    int mReservedWidth = 0;
    tvg_ptr<tvg::Scene> mScene;  // self-owned subtree; attached to panel.HudScene() in OnAttach

public:
    PriceRuler() = default;

    void OnAttach(InstrumentContentPanel& panel) override;
    void CalculateSize(InstrumentContentPanel& panel) override;
    void OnLayout(InstrumentContentPanel& panel) override;
    void OnDetach(InstrumentContentPanel& panel) override;
};

}
