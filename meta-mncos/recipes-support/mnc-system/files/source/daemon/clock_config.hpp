// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "backend.hpp"
namespace mnc::os::system {
struct ClockPolicyChanges { bool servers = false; bool network = false; };
// Called before network/time services start, and when preferences change.
ClockPolicyChanges writeClockPolicy(const Profile &, const TimePreferences &,
                                    const std::filesystem::path &runtime = "/run");
}
