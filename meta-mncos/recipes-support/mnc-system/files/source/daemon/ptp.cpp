// SPDX-License-Identifier: GPL-3.0-only
#include "ptp.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <linux/ethtool.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mnc::os::system {
TimestampCapabilities timestampCapabilities(const std::string &name) {
    if (!validInterfaceName(name)) return {};
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return {};
    ethtool_ts_info info{};
    info.cmd = ETHTOOL_GET_TS_INFO;
    ifreq request{};
    std::memcpy(request.ifr_name, name.c_str(), name.size() + 1);
    request.ifr_data = reinterpret_cast<char *>(&info);
    const int result = ioctl(fd, SIOCETHTOOL, &request);
    close(fd);
    if (result < 0) return {};
    constexpr auto required = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE |
                              SOF_TIMESTAMPING_RAW_HARDWARE;
    return {info.phc_index, info.phc_index >= 0 && (info.so_timestamping & required) == required};
}

std::vector<PtpInterface> discoverPtpInterfaces(
    const Profile &profile, const std::filesystem::path &root,
    const std::function<TimestampCapabilities(const std::string &)> &query) {
    std::vector<PtpInterface> result;
    // Enumerate actual kernel interfaces; never manufacture entries from policy/settings.
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        const auto name = entry.path().filename().string();
        if (!validInterfaceName(name) || name == "lo") continue;
        PtpInterface value;
        value.interface = name;
        std::ifstream carrier(entry.path() / "carrier");
        int up = 0;
        carrier >> up;
        value.carrier = up == 1;
        const auto capabilities = query(name);
        value.hardware_timestamping = capabilities.hardware;
        value.phc_index = capabilities.phc_index;
        const auto policy = std::ranges::find(profile.interfaces, name, &InterfacePolicy::name);
        bool managed = policy != profile.interfaces.end();
        if (managed && !policy->device_tree_node.empty()) {
            std::error_code error;
            const auto node = std::filesystem::canonical(entry.path() / "device/of_node", error);
            managed = !error && node.filename() == policy->device_tree_node;
        }
        if (!managed) value.reason = "Interface is not managed by this product";
        else if (profile.ptp_unit_templates.empty()) value.reason = "PTP is unavailable on this product";
        else if (!capabilities.hardware) value.reason = "Hardware PTP timestamps are unavailable";
        else value.selectable = true;
        result.push_back(std::move(value));
    }
    std::ranges::sort(result, {}, &PtpInterface::interface);
    return result;
}

void validatePtpSelection(const TimePreferences &time, const std::vector<PtpInterface> &interfaces) {
    if (time.synchronization != "ptp") return;
    const auto found = std::ranges::find(interfaces, time.ptp_interface, &PtpInterface::interface);
    if (found == interfaces.end())
        throw Error({ErrorCode::invalid_argument, "Selected PTP interface is unavailable"});
    if (!found->selectable)
        throw Error({ErrorCode::invalid_argument, found->reason});
}

ClockUnits clockUnits(const Profile &profile, const TimePreferences &time) {
    std::vector<std::string> selected;
    auto instance = [](const std::string &unit, const std::string &name) {
        if (!validInterfaceName(name) || !unit.ends_with("@.service"))
            throw Error({ErrorCode::invalid_argument, "Invalid PTP unit template or interface"});
        std::string escaped;
        // systemd unit-instance escaping; %I restores the original interface name.
        for (const char c : name) escaped += c == '-' ? "\\x2d" : std::string(1, c);
        return unit.substr(0, unit.size() - 8) + escaped + ".service";
    };
    if (time.synchronization == "ptp") {
        const auto found = std::ranges::find(profile.interfaces, time.ptp_interface, &InterfacePolicy::name);
        if (found == profile.interfaces.end() || profile.ptp_unit_templates.empty())
            throw Error({ErrorCode::invalid_argument, "Selected PTP interface is not supported"});
        for (const auto &unit : profile.ptp_unit_templates)
            selected.push_back(instance(unit, time.ptp_interface));
    }
    auto all = profile.ptp_units; // Also stop instances from pre-selector product policy.
    for (const auto &port : profile.interfaces)
        for (const auto &unit : profile.ptp_unit_templates) {
            const auto name = instance(unit, port.name);
            if (std::ranges::find(all, name) == all.end()) all.push_back(name);
        }
    ClockUnits result;
    for (auto it = all.rbegin(); it != all.rend(); ++it)
        if (std::ranges::find(selected, *it) == selected.end()) result.stop.push_back(*it);
    if (time.synchronization == "ptp") {
        result.stop.insert(result.stop.end(), profile.ntp_units.rbegin(), profile.ntp_units.rend());
        result.start = std::move(selected);
    } else result.start = profile.ntp_units;
    return result;
}
} // namespace mnc::os::system
