// SPDX-License-Identifier: GPL-3.0-only
#include "clock_config.hpp"
#include <cassert>
#include <sys/stat.h>
using namespace mnc::os::system;
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() / ("clock-policy-test-" + randomId());
    const auto oldMask = umask(0077);
    struct Cleanup { fs::path path; mode_t mask; ~Cleanup() { umask(mask); fs::remove_all(path); } } cleanup{root, oldMask};
    fs::create_directories(root);
    Profile profile;
    profile.interfaces = {{"lan7", ""}, {"lan8", ""}};
    TimePreferences time{"ntp", "UTC", "", {"ntp.example.test", "2001:db8::1"}};
    auto change = writeClockPolicy(profile, time, root);
    assert(change.servers && change.network);
    assert(!fs::exists(root / "mnc-system/ntp-disabled"));
    assert(fs::exists(root / "mnc-system/ptp-disabled"));
    const auto config = root / "systemd/timesyncd.conf.d/90-mnc-time.conf";
    assert(readFile(config) == "[Time]\nNTP=\nNTP=ntp.example.test 2001:db8::1 \nFallbackNTP=\n");
    for (auto path = config.parent_path(); path != root; path = path.parent_path())
        assert((fs::status(path).permissions() & fs::perms::others_exec) != fs::perms::none);
    assert((fs::status(config).permissions() & fs::perms::others_read) != fs::perms::none);
    for (const auto &port : profile.interfaces) {
        const auto network = root / "systemd/network" / ("10-mnc-" + port.name + ".network.d/50-mnc-time.conf");
        assert(readFile(network).find("[DHCPv4]\nUseNTP=no\n[DHCPv6]\nUseNTP=no") != std::string::npos);
    }
    change = writeClockPolicy(profile, time, root);
    assert(!change.servers && !change.network);
    time.synchronization = "local";
    writeClockPolicy(profile, time, root);
    assert(fs::exists(root / "mnc-system/ntp-disabled") && fs::exists(root / "mnc-system/ptp-disabled"));
    time.synchronization = "ptp";
    time.ptp_interface = "lan7";
    writeClockPolicy(profile, time, root);
    assert(fs::exists(root / "mnc-system/ntp-disabled") && !fs::exists(root / "mnc-system/ptp-disabled"));
    time.synchronization = "ntp";
    time.ntp_servers.clear();
    change = writeClockPolicy(profile, time, root);
    assert(change.servers && change.network);
    assert(readFile(config) == "[Time]\n");
    assert(!fs::exists(root / "systemd/network/10-mnc-lan7.network.d/50-mnc-time.conf"));
    time.ntp_servers = {"host\nFallbackNTP=evil"};
    bool rejected = false;
    try { writeClockPolicy(profile, time, root); } catch (const Error &) { rejected = true; }
    assert(rejected && readFile(config) == "[Time]\n");
}
