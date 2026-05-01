// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include "scratcher.hpp"

namespace scratcher::cockpit {

class PriceRuler final : public Scratcher
{
    int mReservedWidth = 0;

public:
    PriceRuler() = default;

    void CalculateSize(InstrumentContentPanel&) override;
    void EmitChanges(InstrumentContentPanel&) override;
};

}
