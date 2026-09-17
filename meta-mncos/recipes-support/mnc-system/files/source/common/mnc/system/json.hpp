// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mnc/system/system.hpp"
#include <glaze/glaze.hpp>

// Optional JSON adapter. Consumers provide Glaze; the abstract API does not require it.
namespace glz {
template <> struct meta<mnc::system::NetworkMode> {
    using enum mnc::system::NetworkMode;
    static constexpr auto value = enumerate("dhcp", dhcp, "static_ipv4", static_ipv4);
};
template <> struct meta<mnc::system::PowerAction> {
    using enum mnc::system::PowerAction;
    static constexpr auto value = enumerate("reboot", reboot, "shutdown", shutdown);
};
} // namespace glz
