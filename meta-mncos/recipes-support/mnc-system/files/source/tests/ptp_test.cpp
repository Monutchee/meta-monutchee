// SPDX-License-Identifier: GPL-3.0-only
#include "ptp.hpp"
#include <cassert>
#include <fstream>
using namespace mnc::os::system;
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const Error &) { rejected = true; }
    assert(rejected);
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("ptp-test-" + randomId());
    std::filesystem::create_directories(root);
    struct Cleanup { std::filesystem::path root; ~Cleanup() { std::filesystem::remove_all(root); } } cleanup{root};
    for (const auto *name : {"lo", "lan7", "enp2s0", "usb0", "veth9"}) {
        std::filesystem::create_directory(root / name);
        std::ofstream(root / name / "carrier") << (std::string(name) == "lan7" ? 1 : 0);
    }
    Profile profile;
    profile.interfaces = {{"lan7", ""}, {"enp2s0", ""}, {"usb0", ""}, {"absent", ""}};
    profile.ptp_unit_templates = {"ptp4l@.service", "phc2sys@.service"};
    auto query = [](const std::string &name) { return TimestampCapabilities{3, name != "usb0"}; };
    auto ports = discoverPtpInterfaces(profile, root, query);
    assert(ports.size() == 4); // No loopback or fabricated missing policy link.
    assert(ports[0].interface == "enp2s0" && ports[0].selectable && !ports[0].carrier);
    assert(ports[1].interface == "lan7" && ports[1].carrier && ports[1].phc_index == 3);
    assert(!ports[2].selectable && !ports[2].reason.empty());
    assert(!ports[3].selectable); // Unsupported product interface, even with hardware timestamps.
    TimePreferences time{"ptp", "UTC", "lan7"};
    validatePtpSelection(time, ports);
    time.ptp_interface = "enp2s0";
    validatePtpSelection(time, ports); // Link-down is visible, but may be selected before cable insertion.
    for (const auto *name : {"usb0", "veth9", "absent", ""}) {
        time.ptp_interface = name;
        rejects([&] { validatePtpSelection(time, ports); });
    }
    time.synchronization = "ntp";
    validatePtpSelection(time, {}); // NTP does not require working PTP hardware.
    profile.interfaces = {{"lan7", ""}, {"enp2s0", ""}};
    time = {"ptp", "UTC", "enp2s0"};
    const auto ptp = clockUnits(profile, time);
    assert((ptp.stop == std::vector<std::string>{"phc2sys@lan7.service", "ptp4l@lan7.service", "systemd-timesyncd.service"}));
    assert((ptp.start == std::vector<std::string>{"ptp4l@enp2s0.service", "phc2sys@enp2s0.service"}));
    time.synchronization = "ntp";
    const auto ntp = clockUnits(profile, time);
    assert(ntp.stop.size() == 4 && ntp.stop[0] == "phc2sys@enp2s0.service");
    assert(ntp.start == profile.ntp_units);
    time = {"ptp", "UTC", "../bad"};
    rejects([&] { validate(Configuration{{}, time}); });
    rejects([&] { clockUnits(profile, time); });
    profile.interfaces = {{"lan-1", ""}};
    time.ptp_interface = "lan-1";
    assert(clockUnits(profile, time).start[0] == "ptp4l@lan\\x2d1.service");
    profile.interfaces = {{"lan7", "ethernet@expected"}};
    assert(!discoverPtpInterfaces(profile, root, query)[1].selectable); // Hardware identity mismatch.
}
