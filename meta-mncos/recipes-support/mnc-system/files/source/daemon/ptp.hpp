// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "backend.hpp"
namespace mnc::os::system {
struct TimestampCapabilities {
    int phc_index = -1;
    bool hardware = false;
};
TimestampCapabilities timestampCapabilities(const std::string &interface);
std::vector<PtpInterface> discoverPtpInterfaces(
    const Profile &, const std::filesystem::path &netRoot = "/sys/class/net",
    const std::function<TimestampCapabilities(const std::string &)> &query = timestampCapabilities);
void validatePtpSelection(const TimePreferences &, const std::vector<PtpInterface> &);
struct ClockUnits { std::vector<std::string> stop, start; };
ClockUnits clockUnits(const Profile &, const TimePreferences &);
} // namespace mnc::os::system
