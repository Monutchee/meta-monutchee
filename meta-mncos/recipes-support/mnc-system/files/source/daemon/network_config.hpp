// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mnc/system/system.hpp"
namespace mnc::os::system {
std::string renderNetwork(const mnc::system::NetworkConfig &,
                          bool hasPreferred);
}
