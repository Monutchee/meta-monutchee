// SPDX-License-Identifier: GPL-3.0-only
#include "mnc/system/system.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <set>
#include <string_view>
namespace mnc::system {
namespace {
void check(bool value, const char *message) {
    if (!value)
        throw Error({ErrorCode::invalid_argument, message});
}
std::uint32_t ipv4(const std::string &text) {
    std::uint32_t result = 0;
    std::string_view rest(text);
    for (int i = 0; i < 4; ++i) {
        auto dot = rest.find('.');
        auto part = rest.substr(0, dot);
        unsigned n = 0;
        auto p = std::from_chars(part.data(), part.data() + part.size(), n);
        check(!part.empty() && (part.size() == 1 || part.front() != '0') && p.ec == std::errc{} && p.ptr == part.data() + part.size() &&
                  n <= 255,
              "invalid IPv4 address");
        result = (result << 8) | n;
        check((i == 3) == (dot == std::string_view::npos), "invalid IPv4 address");
        if (dot != std::string_view::npos)
            rest.remove_prefix(dot + 1);
    }
    check((result >> 24) != 0 && (result >> 24) < 224 && (result >> 24) != 127,
          "IPv4 must be a unicast address");
    return result;
}
} // namespace
bool validNtpServer(const std::string &server) {
    if (server.empty() || server.size() > 253) return false;
    auto decimalAddress = [](std::string_view text) {
        for (int i = 0; i < 4; ++i) {
            const auto dot = text.find('.');
            const auto part = text.substr(0, dot);
            unsigned value = 0;
            const auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
            if (part.empty() || (part.size() > 1 && part.front() == '0') ||
                parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || value > 255 ||
                ((i == 3) != (dot == std::string_view::npos))) return false;
            if (dot != std::string_view::npos) text.remove_prefix(dot + 1);
        }
        return true;
    };
    std::string_view text(server);
    if (text.find(':') != text.npos) {
        // IPv6 literal, including an optional dotted IPv4 tail; no ports or zone IDs.
        const auto compression = text.find("::");
        if (compression != text.npos && text.find("::", compression + 2) != text.npos) return false;
        auto count = [&](std::string_view part, bool allowV4) {
            int words = 0;
            while (!part.empty()) {
                auto colon = part.find(':');
                auto word = part.substr(0, colon);
                if (word.find('.') != word.npos)
                    return allowV4 && colon == part.npos && decimalAddress(word) ? words + 2 : -1;
                unsigned value = 0;
                const auto parsed = std::from_chars(word.data(), word.data() + word.size(), value, 16);
                if (word.empty() || word.size() > 4 || parsed.ec != std::errc{} ||
                    parsed.ptr != word.data() + word.size()) return -1;
                ++words;
                if (colon == part.npos) break;
                part.remove_prefix(colon + 1);
                if (part.empty()) return -1;
            }
            return words;
        };
        if (compression == text.npos) return count(text, true) == 8;
        auto left = count(text.substr(0, compression), false);
        auto right = count(text.substr(compression + 2), true);
        return left >= 0 && right >= 0 && left + right < 8;
    }
    if (std::ranges::all_of(text, [](char c) { return (c >= '0' && c <= '9') || c == '.'; }))
        return decimalAddress(text);
    if (text.back() == '.') text.remove_suffix(1);
    while (!text.empty()) {
        auto dot = text.find('.');
        auto label = text.substr(0, dot);
        if (label.empty() || label.size() > 63 || label.front() == '-' || label.back() == '-' ||
            !std::ranges::all_of(label, [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
            })) return false;
        if (dot == text.npos) return true;
        text.remove_prefix(dot + 1);
    }
    return false;
}
bool validInterfaceName(const std::string &name) {
    return !name.empty() && name.size() < 16 &&
        std::ranges::all_of(name, [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-'; });
}
void validateNetwork(const std::vector<NetworkConfig> &configs) {
    check(configs.size() <= 8, "too many network interfaces");
    check(std::ranges::count(configs, true, &NetworkConfig::preferred_default) <= 1,
          "only one preferred default interface is allowed");
    std::set<std::string> names;
    for (const auto &c : configs) {
        check(!c.interface.empty() && c.interface.size() < 16 &&
                  std::ranges::all_of(
                      c.interface,
                      [](unsigned char x) { return std::isalnum(x) || x == '_' || x == '-'; }),
              "invalid interface name");
        check(names.insert(c.interface).second, "duplicate interface");
        check(c.dns_servers.size() <= 4, "too many DNS servers");
        for (const auto &dns : c.dns_servers)
            (void)ipv4(dns);
        if (c.mode == NetworkMode::dhcp) {
            check(c.address.empty() && c.gateway.empty(),
                  "DHCP cannot specify a static address or gateway");
        } else if (c.mode == NetworkMode::static_ipv4) {
            check(c.prefix_length >= 1 && c.prefix_length <= 32, "invalid IPv4 prefix");
            check(!c.preferred_default || !c.gateway.empty(),
                  "preferred static interface requires a gateway");
            auto address = ipv4(c.address);
            auto mask = 0xffffffffu << (32 - c.prefix_length);
            if (c.prefix_length < 31)
                check((address & ~mask) != 0 && (address & ~mask) != ~mask,
                      "network/broadcast address is not a host");
            if (!c.gateway.empty()) {
                auto gateway = ipv4(c.gateway);
                check(gateway != address && (gateway & mask) == (address & mask),
                      "gateway must be a different address on the subnet");
                if (c.prefix_length < 31)
                    check((gateway & ~mask) != 0 && (gateway & ~mask) != ~mask,
                          "gateway cannot be a network or broadcast address");
            }
        } else
            check(false, "unsupported network mode");
    }
}
void validate(const Configuration &c) {
    check(!c.system.hostname.empty() && c.system.hostname.size() <= 63 &&
              c.system.hostname.front() != '-' && c.system.hostname.back() != '-' &&
              std::ranges::all_of(c.system.hostname,
                                  [](unsigned char x) { return std::isalnum(x) || x == '-'; }),
          "invalid hostname");
    check(c.time.synchronization == "ntp" || c.time.synchronization == "ptp" || c.time.synchronization == "local",
          "invalid time synchronization mode");
    check(!c.time.timezone.empty() && c.time.timezone.size() <= 128 &&
              c.time.timezone.front() != '/' && c.time.timezone.find("..") == std::string::npos &&
              std::ranges::all_of(c.time.timezone,
                                  [](unsigned char x) {
                                      return std::isalnum(x) || x == '/' || x == '_' || x == '-' ||
                                             x == '+';
                                  }),
          "invalid timezone");
    check((c.time.ptp_interface.empty() && c.time.synchronization != "ptp") ||
              validInterfaceName(c.time.ptp_interface), "invalid PTP interface name");
    check(c.time.ntp_servers.size() <= 8, "at most eight NTP servers are allowed");
    std::set<std::string> servers;
    for (const auto &server : c.time.ntp_servers) {
        check(validNtpServer(server), "invalid NTP server: use a hostname or IP address without a port");
        check(servers.insert(server).second, "duplicate NTP server");
    }
    validateNetwork(c.system.network);
}
} // namespace mnc::system
