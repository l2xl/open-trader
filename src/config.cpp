// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include "exception"
#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <stdexcept>

#include "data/bybit/bybit_config.hpp"
#include "cockpit/trade_cockpit_config.hpp"

namespace {

const char* const CONFIG  = "--config";
const char* const DATADIR = "--data-dir,-d";
const char* const VERBOSE = "--verbose,-v";
const char* const TRACE   = "--debug-trace,-t";

namespace bybit_keys = scratcher::bybit::config_keys;
namespace cockpit_keys = scratcher::cockpit::config_keys;

void require_options(const CLI::App* sub, std::initializer_list<const char*> flags)
{
    for (const char* flag : flags) {
        const auto* opt = sub->get_option(flag);
        if (!opt) throw std::runtime_error(std::string("Unknown config option: ") + sub->get_name() + " " + flag);
        if (opt->empty()) throw std::runtime_error(std::string("Missing required config option: [") + sub->get_name() + "] " + (flag + 2));
    }
}

}

Config::Config(int argc, const char *const argv[])
{
    std::filesystem::path home;
    if (const char* psz_home = std::getenv("HOME")) {
        home = psz_home;
    }

    std::filesystem::path data_path;
    std::filesystem::path conf_path;

    CLI::App preConf;
    preConf.set_version_flag("--version", []{ return std::string("BScratcher Wallet v.0.1"); });
    preConf.add_option(DATADIR, data_path, "Directory path to store wallet data")->default_val(home/".scratcher");
    preConf.add_option(CONFIG, conf_path, "Config file path")->default_val("config");
    try {
       preConf.parse(argc, argv);
    }
    catch (const CLI::CallForVersion& e) {
        std::cout << preConf.version() << std::endl;
        std::rethrow_exception(std::current_exception());
    }
    catch (const CLI::CallForHelp& ) {}
    catch (const CLI::CallForAllHelp& ) {}
    catch (const CLI::ParseError &e) {
        preConf.exit(e);
        std::rethrow_exception(std::current_exception());
    }

    if (!conf_path.is_absolute() && conf_path.string().front() != '.')
        conf_path = data_path / conf_path;

    mApp.set_help_flag("--help,-h", "Display this help information and exit");
    mApp.set_version_flag("--version", []{ return std::string("BScratcher Wallet v.0.1"); });
    mApp.set_config(CONFIG, conf_path, "Configuration file");
    mApp.add_option(DATADIR, mDataDir, "Directory path to store wallet data")->default_val(home/".scratcher")->configurable(false);
    mApp.add_flag(TRACE, mTrace, "Print debug traces to log");

    auto* bybit = mApp.add_subcommand(bybit_keys::section, "ByBit exchange options")->configurable()->group("Config File Sections");
    bybit->add_option(bybit_keys::http_host,   m_http_host,   "ByBit exchange HTTP API host")->configurable(true);
    bybit->add_option(bybit_keys::http_port,   m_http_port,   "ByBit exchange HTTP API port")->configurable(true);
    bybit->add_option(bybit_keys::stream_host, m_stream_host, "ByBit exchange web-socket stream API host")->configurable(true);
    bybit->add_option(bybit_keys::stream_port, m_stream_port, "ByBit exchange web-socket stream API port")->configurable(true);
    bybit->add_option(bybit_keys::api_key,     m_api_key,     "ByBit API key")->configurable(true)->envname("BYBIT_API_KEY");
    bybit->add_option(bybit_keys::api_secret,  m_api_secret,  "ByBit API secret")->configurable(true)->envname("BYBIT_API_SECRET");

    auto* defaults = mApp.add_subcommand(cockpit_keys::section, "Default UI options")->configurable()->group("Config File Sections");
    defaults->add_option(cockpit_keys::default_instrument,    mDefaultInstrument,           "Default instrument symbol shown in the first MarketGraph panel")->configurable(true);
    defaults->add_option(cockpit_keys::default_candle_period, mDefaultCandlePeriodSeconds,  "Default candle period in seconds")->default_val(60)->configurable(true);
    defaults->add_option(cockpit_keys::default_candle_width,  mDefaultCandleWidthPixels,    "Default candle width in pixels including margin/padding")->default_val(8)->configurable(true);

    try {
        mApp.parse(argc, argv);
    }
    catch (const CLI::ParseError &e) {
        mApp.exit(e);
        std::rethrow_exception(std::current_exception());
    }

    require_options(bybit, {bybit_keys::http_host, bybit_keys::http_port, bybit_keys::stream_host, bybit_keys::stream_port});
    require_options(defaults, {cockpit_keys::default_candle_period, cockpit_keys::default_candle_width});
}
