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
    check(c.time.synchronization == "ntp" || c.time.synchronization == "ptp",
          "invalid time synchronization mode");
    check(!c.time.timezone.empty() && c.time.timezone.size() <= 128 &&
              c.time.timezone.front() != '/' && c.time.timezone.find("..") == std::string::npos &&
              std::ranges::all_of(c.time.timezone,
                                  [](unsigned char x) {
                                      return std::isalnum(x) || x == '/' || x == '_' || x == '-' ||
                                             x == '+';
                                  }),
          "invalid timezone");
    validateNetwork(c.system.network);
}
} // namespace mnc::system
