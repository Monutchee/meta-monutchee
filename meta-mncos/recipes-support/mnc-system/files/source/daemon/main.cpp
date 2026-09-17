// SPDX-License-Identifier: GPL-3.0-only
#include "access_policy.hpp"
#include "backend.hpp"
#include "mnc/service/service.hpp"
#include <array>
#include <iostream>
#include <pwd.h>
#include <systemd/sd-bus.h>
#include <thread>
namespace mnc::os::system {
namespace {
std::optional<std::uint32_t> account(const std::string &name) {
    auto *entry = getpwnam(name.c_str());
    if (entry)
        return entry->pw_uid;
    return {};
}
class Daemon final : public mnc::Service {
    Profile profile_;
    std::unique_ptr<Platform> platform_;
    Backend backend_;
    sd_bus *bus_ = nullptr;
    sd_bus_slot *slot_ = nullptr;
    std::thread worker_;
    std::atomic<bool> failed_{false};
    static int request(sd_bus_message *m, void *userdata, sd_bus_error *) noexcept {
        auto &self = *static_cast<Daemon *>(userdata);
        Reply reply;
        const char *method = sd_bus_message_get_member(m);
        uid_t uid = static_cast<uid_t>(-1);
        try {
            sd_bus_creds *credentials = nullptr;
            if (sd_bus_query_sender_creds(m, SD_BUS_CREDS_EUID, &credentials) < 0)
                throw Error({ErrorCode::permission_denied, "peer identity unavailable"});
            const int rc = sd_bus_creds_get_euid(credentials, &uid);
            sd_bus_creds_unref(credentials);
            if (rc < 0)
                throw Error({ErrorCode::permission_denied, "peer UID unavailable"});
            const std::string_view name = method ? method : "";
            const bool settings = settingsOperation(name), control = controlOperation(name);
            if (!allowed(name, uid, account(self.profile_.settings_user),
                         account(self.profile_.control_user)))
                throw Error({ErrorCode::permission_denied,
                             "operation is not allowed for this service identity"});
            const char *json = nullptr;
            if (sd_bus_message_read(m, "s", &json) < 0 || !json)
                throw Error({ErrorCode::invalid_argument, "missing request"});
            if (name == "GetStatus") {
                decode<Empty>(json);
                reply.json = encode(self.backend_.status());
            } else if (name == "GetTime") {
                decode<Empty>(json);
                reply.json = encode(self.backend_.time());
            } else if (name == "ListTimezones") {
                decode<Empty>(json);
                reply.json = encode(self.backend_.timezones());
            } else if (name == "GetTimezoneCatalog") {
                decode<Empty>(json);
                reply.json = encode(self.backend_.timezoneCatalog());
            } else if (name == "GetTemperatures") {
                decode<Empty>(json);
                reply.json = encode(self.backend_.temperatures());
            } else if (name == "ApplyPreferences")
                self.backend_.apply(decode<Configuration>(json));
            else if (name == "BeginNetwork")
                reply.json = encode(self.backend_.beginNetwork(decode<NetworkProposal>(json)));
            else if (name == "GetNetworkTransaction") {
                decode<Empty>(json);
                reply.json = encode(self.backend_.networkTransaction());
            } else if (name == "PrepareNetworkCommit")
                self.backend_.prepareNetworkCommit(decode<Id>(json).id);
            else if (name == "FinishNetwork") {
                auto v = decode<Finish>(json);
                self.backend_.finishNetwork(v.id, v.commit);
            } else if (name == "Power")
                reply.json = encode(self.backend_.power(decode<Power>(json).action));
            else if (name == "ResetDevice")
                reply.json = encode(self.backend_.resetDevice(decode<Reset>(json).confirmed));
            else if (name == "GetJob") {
                decode<Empty>(json);
                reply.json = encode(self.backend_.job());
            } else
                throw Error({ErrorCode::unsupported, "unknown system operation"});
            if (settings || control) {
                std::array<mnc::logging::Field, 2> fields{
                    {{"MNC_OPERATION", std::string(name)}, {"MNC_PEER_UID", std::to_string(uid)}}};
                self.logger().write(mnc::logging::Priority::notice, "system operation accepted",
                                    "system_operation", fields);
            }
        } catch (const Error &e) {
            reply.code = e.code;
            reply.message = e.what();
        } catch (const std::exception &e) {
            reply.code = ErrorCode::internal_error;
            reply.message = e.what();
        } catch (...) {
            reply.code = ErrorCode::internal_error;
            reply.message = "system operation failed";
        }
        if (reply.code != ErrorCode::none)
            self.logger().write(mnc::logging::Priority::warning, reply.message,
                                "system_operation_rejected");
        try {
            auto text = encode(reply);
            return sd_bus_reply_method_return(m, "s", text.c_str());
        } catch (...) {
            return -ENOMEM;
        }
    }
    void on_start() override {
        backend_.start();
        if (sd_bus_open_system(&bus_) < 0)
            throw Error({ErrorCode::unavailable, "cannot open system bus"});
        static const sd_bus_vtable table[] = {SD_BUS_VTABLE_START(0),
#define METHOD(name) SD_BUS_METHOD(name, "s", "s", request, SD_BUS_VTABLE_UNPRIVILEGED)
                                              METHOD("GetStatus"),
                                              METHOD("GetTime"),
                                              METHOD("ListTimezones"),
                                              METHOD("GetTimezoneCatalog"),
                                              METHOD("GetTemperatures"),
                                              METHOD("ApplyPreferences"),
                                              METHOD("BeginNetwork"),
                                              METHOD("GetNetworkTransaction"),
                                              METHOD("PrepareNetworkCommit"),
                                              METHOD("FinishNetwork"),
                                              METHOD("Power"),
                                              METHOD("ResetDevice"),
                                              METHOD("GetJob"),
#undef METHOD
                                              SD_BUS_VTABLE_END};
        if (sd_bus_add_object_vtable(bus_, &slot_, object_path, interface_name, table, this) < 0 ||
            sd_bus_request_name(bus_, bus_name, 0) < 0)
            throw Error({ErrorCode::unavailable, "cannot register system manager"});
        worker_ = std::thread([this] {
            try {
                while (!stop_requested()) {
                    backend_.tick();
                    int r = sd_bus_process(bus_, nullptr);
                    if (r < 0)
                        throw Error({ErrorCode::unavailable, "system bus disconnected"});
                    if (r == 0)
                        sd_bus_wait(bus_, 250000);
                }
            } catch (const std::exception &e) {
                logger().write(mnc::logging::Priority::error, e.what(), "system_worker_failed");
                failed_ = true;
                request_stop();
            }
        });
    }
    void on_reload() override {} // Immutable profile changes take effect on service restart.
    void on_stop() noexcept override {
        if (worker_.joinable())
            worker_.join();
        sd_bus_slot_unref(slot_);
        slot_ = nullptr;
        sd_bus_unref(bus_);
        bus_ = nullptr;
    }
    mnc::ServiceHealth health() const override {
        return {!failed_, failed_ ? "system worker failed" : "system manager ready"};
    }

  public:
    int run() {
        const int result = execute();
        return failed_ ? 1 : result;
    }
    explicit Daemon(Profile p)
        : Service("MNC system manager", "system"), profile_(std::move(p)),
          platform_(nativePlatform(profile_)), backend_(profile_, *platform_) {}
};
} // namespace
} // namespace mnc::os::system
int main(int argc, char **argv) {
    using namespace mnc::os::system;
    try {
        auto profile = loadProfile("/usr/share/mnc/system/profile.json");
        if (argc == 2 && std::string_view(argv[1]) == "--bootstrap") {
            auto platform = nativePlatform(profile);
            Backend backend(profile, *platform);
            backend.bootstrap();
            return 0;
        }
        if (argc != 1) {
            std::cerr << "usage: mnc-system-manager [--bootstrap]\n";
            return 2;
        }
        Daemon daemon(std::move(profile));
        return daemon.run();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
