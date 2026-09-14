// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
namespace mnc::os::system {
inline bool settingsOperation(std::string_view name) {
    return name == "ApplyPreferences" || name == "BeginNetwork" || name == "PrepareNetworkCommit" ||
           name == "FinishNetwork";
}
inline bool controlOperation(std::string_view name) {
    return name == "Power" || name == "ResetDevice";
}
inline bool allowed(std::string_view name, std::uint32_t uid,
                    std::optional<std::uint32_t> settings_uid,
                    std::optional<std::uint32_t> control_uid) {
    if (settingsOperation(name))
        return uid == 0 || (settings_uid && uid == *settings_uid);
    if (controlOperation(name))
        return uid == 0 || (control_uid && uid == *control_uid);
    return name == "GetStatus" || name == "GetTime" || name == "ListTimezones" ||
           name == "GetTemperatures" || name == "GetNetworkTransaction" || name == "GetJob";
}
} // namespace mnc::os::system
