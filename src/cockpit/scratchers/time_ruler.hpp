// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

#include "scratcher.hpp"

namespace scratcher::cockpit {

class TimeRuler final : public Scratcher
{
    int mReservedHeight = 0;

public:
    TimeRuler() = default;

    void CalculateSize(InstrumentContentPanel&) override;
    void EmitChanges(InstrumentContentPanel&) override;
};

}
