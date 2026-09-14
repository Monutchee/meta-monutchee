// SPDX-License-Identifier: GPL-3.0-only
#include "mnc/os/system/client.hpp"
#include "mnc/os/system/protocol.hpp"
#ifdef MNC_SYSTEM_HAVE_SYSTEMD
#include <systemd/sd-bus.h>
#endif
namespace mnc::os::system {
using namespace mnc::system;
namespace {
Result<Reply> request(const char *method, const std::string &json) {
#ifdef MNC_SYSTEM_HAVE_SYSTEMD
    sd_bus *bus = nullptr;
    sd_bus_message *response = nullptr;
    sd_bus_error error = SD_BUS_ERROR_NULL;
    struct Cleanup {
        sd_bus *&b;
        sd_bus_message *&m;
        sd_bus_error &e;
        ~Cleanup() {
            sd_bus_message_unref(m);
            sd_bus_error_free(&e);
            sd_bus_unref(b);
        }
    } cleanup{bus, response, error};
    int r = sd_bus_open_system(&bus);
    if (r < 0)
        return std::unexpected(SystemError{ErrorCode::unavailable, "system bus unavailable"});
    sd_bus_set_method_call_timeout(bus, 60000000);
    r = sd_bus_call_method(bus, bus_name, object_path, interface_name, method, &error, &response,
                           "s", json.c_str());
    if (r < 0)
        return std::unexpected(SystemError{
            r == -EACCES ? ErrorCode::permission_denied
                         : (r == -ETIMEDOUT ? ErrorCode::timeout : ErrorCode::unavailable),
            error.message ? error.message : "system manager unavailable"});
    const char *text = nullptr;
    if (sd_bus_message_read(response, "s", &text) < 0 || !text)
        return std::unexpected(SystemError{ErrorCode::internal_error, "invalid manager reply"});
    try {
        return decode<Reply>(text, 512 * 1024);
    } catch (const Error &e) {
        return std::unexpected(SystemError{e.code, e.what()});
    }
#else
    (void)method;
    (void)json;
    return std::unexpected(
        SystemError{ErrorCode::unavailable, "system management requires libsystemd"});
#endif
}
template <class T, class Request = Empty>
Result<T> call(const char *method, const Request &value = {}) {
    try {
        auto reply = request(method, encode(value));
        if (!reply)
            return std::unexpected(reply.error());
        if (reply->code != ErrorCode::none)
            return std::unexpected(SystemError{reply->code, reply->message});
        if constexpr (!std::is_void_v<T>)
            return decode<T>(reply->json, 512 * 1024);
        else
            return {};
    } catch (const Error &e) {
        return std::unexpected(SystemError{e.code, e.what()});
    } catch (const std::exception &e) {
        return std::unexpected(SystemError{ErrorCode::internal_error, e.what()});
    }
}
} // namespace
Result<SystemStatus> Client::status() { return call<SystemStatus>("GetStatus"); }
Result<TimeStatus> Client::time() { return call<TimeStatus>("GetTime"); }
Result<std::vector<std::string>> Client::timezones() {
    return call<std::vector<std::string>>("ListTimezones");
}
Result<TimezoneCatalog> Client::timezoneCatalog() { return call<TimezoneCatalog>("GetTimezoneCatalog"); }
Result<std::vector<Temperature>> Client::temperatures() {
    return call<std::vector<Temperature>>("GetTemperatures");
}
Result<void> Client::apply(const Configuration &c) { return call<void>("ApplyPreferences", c); }
Result<NetworkTransaction> Client::beginNetwork(const NetworkProposal &c) {
    return call<NetworkTransaction>("BeginNetwork", c);
}
Result<NetworkTransaction> Client::networkTransaction() {
    return call<NetworkTransaction>("GetNetworkTransaction");
}
Result<void> Client::prepareNetworkCommit(const std::string &id) {
    return call<void>("PrepareNetworkCommit", Id{id});
}
Result<void> Client::finishNetwork(const std::string &id, bool commit) {
    return call<void>("FinishNetwork", Finish{id, commit});
}
Result<Job> Client::power(PowerAction action) { return call<Job>("Power", Power{action}); }
Result<Job> Client::resetDevice(bool confirmed) {
    return call<Job>("ResetDevice", Reset{confirmed});
}
Result<Job> Client::job() { return call<Job>("GetJob"); }
} // namespace mnc::os::system
