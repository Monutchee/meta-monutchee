// SPDX-License-Identifier: GPL-3.0-only
#include "mnc/system/system.hpp"
#include <cassert>
#include <functional>
int main() {
    using namespace mnc::system;
    Configuration c;
    c.system.hostname = "meter";
    c.system.network = {{"end0", NetworkMode::dhcp, "", 24, "", {}}};
    validate(c);
    auto fails = [](const std::function<void()> &fn) {
        try {
            fn();
            return false;
        } catch (const Error &) {
            return true;
        }
    };
    c.system.network[0].address = "10.0.0.2";
    assert(fails([&] { validate(c); }));
    c.system.network[0].mode = NetworkMode::static_ipv4;
    c.system.network[0].gateway = "10.0.0.1";
    validate(c);
    c.system.network[0].gateway = "10.0.1.1";
    assert(fails([&] { validate(c); }));
    for (const auto *gateway : {"10.0.0.0", "10.0.0.255", "010.0.0.1", "0.0.0.1"}) {
        c.system.network[0].gateway = gateway;
        assert(fails([&] { validate(c); }));
    }
    c.system.network[0].gateway = "";
    c.system.network[0].address = "10.0.0.255";
    assert(fails([&] { validate(c); }));
    c.system.network.clear();
    c.system.hostname = "x\n[Network]";
    assert(fails([&] { validate(c); }));
    c.system.hostname = "meter";
    c.time.timezone = "../../etc/shadow";
    assert(fails([&] { validate(c); }));
    c.time.timezone = "America/Toronto";
    validate(c);
    c.time.synchronization = "local";
    c.time.ptp_interface.clear();
    c.time.ntp_servers = {"ntp.example.test", "192.168.1.2", "2001:db8::1"};
    validate(c);
    for (const auto *server : {"pool.ntp.org.", "::1", "::ffff:192.0.2.1", "1.2.3.4"})
        assert(validNtpServer(server));
    for (const auto *server : {"", "a b", "a\nFallbackNTP=evil", "-bad", "bad-", "a..b",
                              "http://host", "host:123", "[::1]", "fe80::1%eth0", ":::1",
                              "256.1.1.1", "01.2.3.4", "1.2.3", "a_b"})
        assert(!validNtpServer(server));
    c.time.ntp_servers = {"host", "host"};
    assert(fails([&] { validate(c); }));
    c.time.ntp_servers = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    assert(fails([&] { validate(c); }));
}
