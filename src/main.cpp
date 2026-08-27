// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "app/main_window.hpp"
#include "trade_cockpit.hpp"

#include <iostream>

#include <SQLiteCpp/SQLiteCpp.h>

#include "scheduler.hpp"
#include "config.hpp"
#include "config_helper.hpp"
#include "log.hpp"

namespace SQLite {

void assertion_failed(const char* apFile, int apLine, const char* apFunc, const char* apExpr, const char* apMsg) {
    cex::log::at(cex::log::error) << "SQLite assertion failed: " << apFile << ":" << apLine << " in " << apFunc << "() - " << apExpr;
    if (apMsg) cex::log::at(cex::log::error) << " (" << apMsg << ")";
    cex::log::at(cex::log::error) << std::endl;
    std::abort();
}

}

int main(int argc, char* argv[])
{
    try {
        auto config = std::make_shared<Config>(argc, argv);
        cex::log::set_verbose(config->Verbose() > 0);

        auto sched = scratcher::scheduler::create(1);
        auto database = std::make_shared<SQLite::Database>(config->DataDir() + "/market_data.sqlite", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

        auto cockpit = scratcher::cockpit::TradeCockpit::Create(sched, config->App(), database);

        scratcher::elements::UiBuilder builder;
        scratcher::elements::MainWindow window(builder, cockpit);

        return window.Run();
    }
    catch (std::system_error& e) {
        cex::log::at(cex::log::error) << "System error: " << e.what() << " (" << e.code() << ')' << std::endl;
        return -1;
    }
    catch (boost::system::system_error& e) {
        cex::log::at(cex::log::error) << "System error: " << e.what() << " (" << e.code() << ')' << std::endl;
        return -1;
    }
    catch (std::exception& e) {
        cex::log::at(cex::log::error) << "Error: " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        cex::log::at(cex::log::error) << "Unknown error" << std::endl;
        return -1;
    }
}
