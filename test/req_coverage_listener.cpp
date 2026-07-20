// XCockpit
// Copyright (c) 2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License, v2 (IPRL)

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <cstdlib>
#include <fstream>
#include <regex>
#include <string>

namespace {

std::string json_escaped(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) break;
            out += c;
        }
    }
    return out;
}

class req_coverage_listener : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseEnded(const Catch::TestCaseStats& stats) override
    {
        // getenv is a C API boundary: const char* is the mandated return type.
        const char* coverage_file = std::getenv("REQ_COVERAGE_FILE");
        if (!coverage_file) return;

        static const std::regex uid_re{"[A-Z][A-Z_]*-[0-9]+"};
        static const std::regex binding_re{"[a-z0-9_]+"};

        const auto& tags = stats.testInfo->tags;
        for (std::size_t i = 0; i < tags.size(); ++i) {
            std::string uid{tags[i].original};
            if (!std::regex_match(uid, uid_re)) continue;
            std::string line = "{\"tags\":[\"" + uid + "\"";
            if (i + 1 < tags.size()) {
                std::string name{tags[i + 1].original};
                if (std::regex_match(name, binding_re) && !std::regex_match(name, uid_re))
                    line += ",\"" + name + "\"";
            }
            line += "],\"passed\":";
            line += stats.totals.assertions.allOk() ? "true" : "false";
            line += ",\"name\":\"" + json_escaped(stats.testInfo->name) + "\"}";
            std::ofstream{coverage_file, std::ios::app} << line << "\n";
        }
    }
};

} // namespace

CATCH_REGISTER_LISTENER(req_coverage_listener)
