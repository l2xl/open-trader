// Scratcher project
// Copyright (c) 2024-2025 l2xl (l2xl/at/proton.me)
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


#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "cli11/CLI11.hpp"
#include "exchange_config.hpp"

class Config : public scratcher::IExchangeConfig {
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

public:
    Config() = delete;
    Config(int argc, const char *const argv[]);

    size_t Verbose() const { return mVerbose; }
    bool Trace() const {return mTrace; }
    const std::string& DataDir() const { return mDataDir; }

    const std::string& HttpHost() const override { return m_http_host; }
    const std::string& HttpPort() const override { return m_http_port; }

    const std::string& StreamHost() const override { return m_stream_host; }
    const std::string& StreamPort() const override { return m_stream_port; }

    const std::string& ApiKey() const override { return m_api_key; }
    const std::string& ApiSecret() const override { return m_api_secret; }
    bool HasApiCredentials() const override { return !m_api_key.empty() && !m_api_secret.empty(); }
};


#endif //CONFIG_HPP
