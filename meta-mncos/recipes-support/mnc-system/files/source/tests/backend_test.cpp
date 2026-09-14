// SPDX-License-Identifier: GPL-3.0-only
#include "access_policy.hpp"
#include "backend.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace mnc::os::system;
namespace fs = std::filesystem;
void check(bool ok) {
    if (!ok)
        throw std::runtime_error("check failed");
}
template <class F> void fails(F f) {
    try {
        f();
    } catch (const Error &) {
        return;
    }
    throw std::runtime_error("expected rejection");
}
struct Fake : Platform {
    Configuration config;
    bool fail_network = false, fail_power = false;
    unsigned reboots = 0;
    Configuration discover() override { return config; }
    SystemStatus status() override { return {}; }
    TimeStatus time() override { return {}; }
    TimezoneCatalog timezoneCatalog() override { return {}; }
    std::vector<std::string> timezones() override { return {"UTC"}; }
    std::vector<Temperature> temperatures() override { return {}; }
    void validateConfiguration(const Configuration &c) override { validate(c); }
    void preferences(const Configuration &c) override { config = c; }
    void network(const std::vector<NetworkConfig> &n, bool) override {
        if (fail_network)
            throw Error({ErrorCode::apply_failed, "injected network failure"});
        config.system.network = n;
    }
    void sshBootPolicy(bool b) override { config.system.ssh_enabled = b; }
    void power(PowerAction) override {
        if (fail_power)
            throw Error({ErrorCode::apply_failed, "injected power failure"});
        ++reboots;
    }
};
int main() {
    try {
        check(
            decode<NetworkConfig>(
                R"({"interface":"end0","mode":"static_ipv4","address":"10.0.0.2","prefix_length":24,"gateway":"10.0.0.1","dns_servers":[]})")
                .mode == NetworkMode::static_ipv4);
        check(encode(NetworkConfig{}).find("\"dhcp\"") != std::string::npos);
        check(allowed("ApplyPreferences", 100, 100, 200));
        check(!allowed("ApplyPreferences", 200, 100, 200));
        check(!allowed("ResetDevice", 100, 100, 200));
        check(allowed("ResetDevice", 200, 100, 200));
        check(!allowed("Power", 999, {}, {}));
        check(!allowed("ExecuteCommand", 0, 100, 200));
        check(allowed("Power", 0, {}, {}));

        const auto root = fs::temp_directory_path() / ("mnc-system-test-" + randomId());
        fs::create_directories(root);
        struct Cleanup {
            fs::path p;
            ~Cleanup() { fs::remove_all(p); }
        } cleanup{root};
        Profile p;
        p.state_directory = (root / "private").string();
        p.active_settings = (root / "product/active.json").string();
        p.reset_paths = {(root / "product").string()};
        Fake fake;
        fake.config.system.network = {{"end0", NetworkMode::dhcp, "", 24, "", {}}};
        p.defaults = fake.config;
        std::uint64_t now = 1000;
        auto clock = [&] { return now; };
        atomicWrite(p.active_settings, "previous");
        atomicWrite(root / "candidate", "candidate");
        auto oldhash = fileHash(p.active_settings), newhash = fileHash(root / "candidate");
        auto network = fake.config.system.network;
        network[0] = {"end0", NetworkMode::static_ipv4, "10.0.0.2", 24, "10.0.0.1", {}};
        network[0].preferred_default = true;
        NetworkProposal proposal{network, oldhash, newhash};
        {
            Backend b(p, fake, clock);
            b.start();
            auto t = b.beginNetwork(proposal);
            check(t.state == "pending" && t.remaining_ms == 120000);
            fails([&] { b.beginNetwork(proposal); });
            fails([&] { b.apply(fake.config); });
            fails([&] { b.finishNetwork("invalid", true); });
            fails([&] { b.finishNetwork(t.id, true); });
            now += 120001;
            b.tick();
            check(b.networkTransaction().state == "rolled_back");
            check(fake.config.system.network == p.defaults.system.network);
            t = b.beginNetwork(proposal);
            b.prepareNetworkCommit(t.id);
            atomicWrite(p.active_settings, "candidate");
            // Lose the final reply and restart after the settings authority committed.
        }
        {
            Backend b(p, fake, clock);
            b.bootstrap();
            check(fake.config.system.network == network);
            b.start();
            check(b.networkTransaction().state == "committed");
            atomicWrite(p.active_settings, "previous");
            proposal.network = p.defaults.system.network;
            auto t = b.beginNetwork(proposal);
            b.prepareNetworkCommit(t.id);
        }
        {
            Backend b(p, fake, clock);
            b.start();
            check(b.networkTransaction().state == "rolled_back");
            check(fake.config.system.network == network);
            fails([&] { b.resetDevice(false); });
            // A failed application retains the pending journal until rollback succeeds.
            fake.fail_network = true;
            fails([&] { b.beginNetwork(proposal); });
            fake.fail_network = false;
            auto failed = b.networkTransaction();
            b.finishNetwork(failed.id, false);
            check(fake.config.system.network == network);
            b.resetDevice(true);
            check(fs::exists(p.active_settings));
            now += 5001;
            b.tick();
            check(fake.reboots == 1);
        }
        {
            Backend b(p, fake, clock);
            fake.fail_network = true;
            fails([&] { b.bootstrap(); });
            check(decode<State>(readFile(root / "private/state.json")).reset_intent);
            fake.fail_network = false;
        }
        {
            Backend b(p, fake, clock);
            b.bootstrap();
            check(!fs::exists(p.active_settings));
            check(fs::exists(root / "private/state.json"));
            check(b.job().state == "completed");
            check(fake.config.system.network == p.defaults.system.network);
            b.bootstrap();
        }
        {
            Backend b(p, fake, clock);
            b.start();
            fake.fail_power = true;
            b.resetDevice(true);
            now += 5001;
            fails([&] { b.tick(); });
            check(b.job().state == "failed");
            check(decode<State>(readFile(root / "private/state.json")).reset_intent);
            fails([&] { b.apply(p.defaults); });
            fake.fail_power = false;
        }
        {
            Backend b(p, fake, clock);
            b.bootstrap();
            b.start();
            fs::rename(root / "private", root / "saved-private");
            atomicWrite(root / "private", "block directory creation");
            bool rejected = false;
            try {
                b.power(PowerAction::reboot);
            } catch (const std::exception &) {
                rejected = true;
            }
            check(rejected);
            now += 6000;
            fails([&] { b.tick(); });
            fs::remove(root / "private");
            fs::rename(root / "saved-private", root / "private");
            check(decode<State>(readFile(root / "private/state.json")).job.state == "completed");
        }
        const auto hw = root / "hwmon/hwmon27";
        fs::create_directories(hw);
        atomicWrite(hw / "temp1_label", "Temp_PL\n");
        atomicWrite(hw / "temp1_input", "41250\n");
        atomicWrite(hw / "temp3_label", "Temp_LPD\n");
        atomicWrite(hw / "temp3_input", "broken\n");
        auto temperatures = readTemperatures(
            root / "hwmon", {{"PL", "Temp_PL"}, {"LPD", "Temp_LPD"}, {"FPD", "Temp_FPD"}});
        check(temperatures[0].millidegrees_c == 41250);
        check(!temperatures[1].available() && !temperatures[2].available());
        std::cout << "system transaction/recovery/reset/temperature tests passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
