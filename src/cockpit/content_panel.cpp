// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "content_panel.hpp"

namespace scratcher::cockpit {

std::string PanelTypeName(PanelType type)
{
    switch (type) {
    case PanelType::Empty:        return "Empty";
    case PanelType::MarketGraph:  return "Market Graph";
    case PanelType::OrderBook:    return "Order Book";
    case PanelType::Orders:       return "Orders";
    case PanelType::TradeHistory: return "Trade History";
    case PanelType::NewOrder:     return "New Order";
    case PanelType::TradeStats:   return "Trade Stats";
    case PanelType::Positions:    return "Positions";
    case PanelType::Watchlist:    return "Watchlist";
    }
    return "Unknown";
}

} // namespace scratcher::cockpit
