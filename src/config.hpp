// Open Trader
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstdint>
#include <string>

#include "cli11/CLI11.hpp"

class Config
{
    CLI::App mApp;

    size_t mVerbose;
    bool mTrace;

    std::string mDataDir;

    std::string m_http_host;
    std::string m_http_port;

    std::string m_stream_host;
    std::string m_stream_port;

    std::string m_api_key;
    std::string m_api_secret;

    std::string mDefaultInstrument;
    uint32_t mDefaultCandlePeriodSeconds = 60;
    uint32_t mDefaultCandleWidthPixels = 8;

public:
    Config() = delete;
    Config(int argc, const char *const argv[]);

    size_t Verbose() const { return mVerbose; }
    bool Trace() const { return mTrace; }
    const std::string& DataDir() const { return mDataDir; }

    CLI::App& App() { return mApp; }
    const CLI::App& App() const { return mApp; }
};


#endif //CONFIG_HPP
