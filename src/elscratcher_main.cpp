// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
//
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b25tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#include "app/elements/main_window.hpp"
#include "trade_cockpit.hpp"

#include <iostream>

#include <SQLiteCpp/SQLiteCpp.h>

#include "scheduler.hpp"
#include "config.hpp"
#include "config_helper.hpp"

namespace SQLite {

void assertion_failed(const char* apFile, int apLine, const char* apFunc, const char* apExpr, const char* apMsg) {
    std::cerr << "SQLite assertion failed: " << apFile << ":" << apLine << " in " << apFunc << "() - " << apExpr;
    if (apMsg) std::cerr << " (" << apMsg << ")";
    std::cerr << std::endl;
    std::abort();
}

}

int main(int argc, char* argv[])
{
    try {
        auto config = std::make_shared<Config>(argc, argv);
        auto sched = scratcher::scheduler::create(1);
        auto database = std::make_shared<SQLite::Database>(config->DataDir() + "/market_data.sqlite", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

        auto cockpit = scratcher::cockpit::TradeCockpit::Create(sched, config->App(), database);

        scratcher::elements::UiBuilder builder;
        scratcher::elements::MainWindow window(builder, cockpit);

        return window.Run();
    }
    catch (std::system_error& e) {
        std::cerr << "System error: " << e.what() << " (" << e.code() << ')' << std::endl;
        return -1;
    }
    catch (boost::system::system_error& e) {
        std::cerr << "System error: " << e.what() << " (" << e.code() << ')' << std::endl;
        return -1;
    }
    catch (std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
    catch (...) {
        std::cerr << "Unknown error" << std::endl;
        return -1;
    }
}
