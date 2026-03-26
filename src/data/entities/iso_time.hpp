// Scratcher project
// Copyright (c) 2025-2026 l2xl (l2xl/at/proton.me)
// Distributed under the Intellectual Property Reserve License (IPRL)
// -----BEGIN PGP PUBLIC KEY BLOCK-----
// mDMEYdxcVRYJKwYBBAHaRw8BAQdAfacBVThCP5QDPEgSbSIudtpJS4Y4Imm5dzaN
// lM1HTem0IkwyIFhsIChsMnhsKSA8bDJ4bEBwcm90b21tYWlsLmNvbT6IkAQTFggA
// OBYhBKRCfUyWnduCkisNl+WRcOaCK79JBQJh3FxVAhsDBQsJCAcCBhUKCQgLAgQW
// AgMBAh4BAheAAAoJEOWRcOaCK79JDl8A/0/AjYVbAURZJXP3tHRgZyYyN9txT6mW
// 0bYCcOf0rZ4NAQDoFX4dytPDvcjV7ovSQJ6dzvIoaRbKWGbHRCufrm5QBA==
// =KKu7
// -----END PGP PUBLIC KEY BLOCK-----

#ifndef SCRATCHER_ISO_TIME_HPP
#define SCRATCHER_ISO_TIME_HPP

#include <string>
#include <compare>

namespace scratcher {

// ISO 8601 time representation, lexicographically comparable
// Format: "2026-03-23T14:30:00.000Z"
struct iso_time {
    std::string value;

    iso_time() = default;
    explicit iso_time(std::string val) : value(std::move(val)) {}

    bool empty() const { return value.empty(); }

    auto operator<=>(const iso_time& other) const { return value <=> other.value; }
    bool operator==(const iso_time& other) const = default;
};

} // namespace scratcher

#endif // SCRATCHER_ISO_TIME_HPP