// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)

#pragma once

namespace scratcher::cockpit {

class InstrumentContentPanel;

struct Scratcher
{
    virtual ~Scratcher() = default;

    virtual void CalculateSize(InstrumentContentPanel&) {}
    virtual void EmitChanges(InstrumentContentPanel&) = 0;
};

}
