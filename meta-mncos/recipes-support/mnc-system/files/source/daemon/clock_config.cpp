// SPDX-License-Identifier: GPL-3.0-only
#include "clock_config.hpp"
namespace mnc::os::system {
namespace fs = std::filesystem;
namespace {
bool readableWrite(const fs::path &path, const std::string &value) {
    if (fs::exists(path) && readFile(path) == value) return false;
    // Bootstrap runs with umask 0077. Newly created systemd directories must
    // also be traversable by the unprivileged timesyncd/networkd services.
    std::vector<fs::path> missing;
    for (auto directory = path.parent_path(); !fs::exists(directory); directory = directory.parent_path())
        missing.push_back(directory);
    fs::create_directories(path.parent_path());
    for (const auto &directory : missing)
        fs::permissions(directory, fs::perms::owner_all | fs::perms::group_read |
                        fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
    fs::permissions(path.parent_path(), fs::perms::owner_all | fs::perms::group_read |
                    fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
    atomicWrite(path, value);
    fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write |
                    fs::perms::group_read | fs::perms::others_read);
    return true;
}
}
ClockPolicyChanges writeClockPolicy(const Profile &profile, const TimePreferences &time,
                                    const fs::path &runtime) {
    Configuration validation;
    validation.time = time;
    validate(validation); // Reject configuration injection even on bootstrap.
    ClockPolicyChanges changes;
    for (const auto *mode : {"ntp", "ptp"}) {
        const auto marker = runtime / fs::path(profile.runtime_root).filename() / "system" / (std::string(mode) + "-disabled");
        if (time.synchronization == mode) durableRemove(marker);
        else atomicWrite(marker, "disabled\n");
    }
    std::string config = "[Time]\n";
    if (!time.ntp_servers.empty()) {
        config += "NTP=\nNTP=";
        for (const auto &server : time.ntp_servers) config += server + " ";
        config += "\nFallbackNTP=\n";
    }
    changes.servers = readableWrite(runtime / "systemd/timesyncd.conf.d/90-mnc-time.conf", config);
    for (const auto &port : profile.interfaces) {
        if (!validInterfaceName(port.name)) throw Error({ErrorCode::invalid_argument, "invalid policy interface"});
        const auto path = runtime / "systemd/network" / ("10-mnc-" + port.name + ".network.d") / "50-mnc-time.conf";
        const bool explicitServers = !time.ntp_servers.empty();
        if (explicitServers) {
            changes.network |= readableWrite(path, "[Network]\nNTP=\n[DHCPv4]\nUseNTP=no\n[DHCPv6]\nUseNTP=no\n");
        } else if (fs::exists(path)) {
            durableRemove(path);
            changes.network = true;
        }
    }
    return changes;
}
}
