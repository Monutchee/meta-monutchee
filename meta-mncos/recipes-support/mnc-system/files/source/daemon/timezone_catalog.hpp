// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mnc/system/system.hpp"
#include <chrono>
#include <filesystem>
namespace mnc::os::system {
mnc::system::TimezoneCatalog
readTimezoneCatalog(const std::vector<std::string> &installed,
                    std::chrono::system_clock::time_point instant,
                    const std::filesystem::path &zoneTable);
}
