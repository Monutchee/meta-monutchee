// SPDX-License-Identifier: GPL-3.0-only
#include "mnc/os/system/client.hpp"
#include <cassert>
#ifdef MNC_CLIENT_TEST_SYSTEMD
#include <systemd/sd-bus.h>
#include <cerrno>
#include <cstdarg>
#include <string>
namespace { std::uint64_t applied_timeout = 0; bool setter_failure = false; unsigned method_calls = 0; std::string request_json; }
extern "C" int __wrap_sd_bus_open_system(sd_bus **bus) { *bus = reinterpret_cast<sd_bus *>(1); return 0; }
extern "C" int __wrap_sd_bus_set_method_call_timeout(sd_bus *, std::uint64_t timeout) { applied_timeout = timeout; return setter_failure ? -EINVAL : 0; }
extern "C" int __wrap_sd_bus_call_method(sd_bus *, const char *, const char *, const char *, const char *, sd_bus_error *, sd_bus_message **, const char *signature, ...) { va_list args; va_start(args, signature); request_json = va_arg(args, const char *); va_end(args); ++method_calls; return -ETIMEDOUT; }
extern "C" sd_bus * __wrap_sd_bus_unref(sd_bus *) { return nullptr; }
extern "C" sd_bus_message * __wrap_sd_bus_message_unref(sd_bus_message *) { return nullptr; }
extern "C" void __wrap_sd_bus_error_free(sd_bus_error *) {}
#endif
int main() {
    using namespace std::chrono_literals;
    using mnc::os::system::Client;
    assert(Client{}.requestTimeout() == 60s);
    assert(Client{1500ms}.requestTimeout() == 1500ms);
#ifdef MNC_CLIENT_TEST_SYSTEMD
    const auto bounded = Client{1500ms}.time();
    assert(!bounded && bounded.error().code == mnc::system::ErrorCode::timeout);
    assert(applied_timeout == 1500000);
    const auto legacy = Client{}.status();
    assert(!legacy && applied_timeout == 60000000);
    Client client;
    (void)client.power(mnc::system::PowerAction::reboot, {"operator", "correlation"});
    assert(request_json.find("operator") != std::string::npos && request_json.find("correlation") != std::string::npos);
    (void)client.power(mnc::system::PowerAction::reboot);
    assert(request_json.find("operator") == std::string::npos);
    (void)client.finishNetwork("transaction", true, {"confirmer", "network-correlation"});
    assert(request_json.find("confirmer") != std::string::npos);
    setter_failure = true;
    const auto count = method_calls;
    const auto failure = Client{1500ms}.time();
    assert(!failure && failure.error().code == mnc::system::ErrorCode::internal_error);
    assert(method_calls == count);
#endif
    for (auto timeout : {0ms, -1ms, 60001ms}) {
        bool rejected = false;
        try { Client client{timeout}; } catch (const std::invalid_argument &) { rejected = true; }
        assert(rejected);
    }
}
