// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include "timezone_catalog.hpp"
#include "network_config.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <charconv>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <net/if.h>
#include <sstream>
#include <sys/utsname.h>
#include <systemd/sd-bus.h>
#include <thread>
namespace mnc::os::system {
namespace fs = std::filesystem;
namespace {
struct Bus {
    sd_bus *bus = nullptr;
    Bus() {
        if (sd_bus_open_system(&bus) < 0)
            throw Error({ErrorCode::unavailable, "system bus unavailable"});
        sd_bus_set_method_call_timeout(bus, 10000000);
    }
    ~Bus() { sd_bus_unref(bus); }
    Bus(const Bus &) = delete;
};
struct Message {
    sd_bus_message *value = nullptr;
    ~Message() { sd_bus_message_unref(value); }
    Message() = default;
    Message(const Message &) = delete;
};
void checked(int code, sd_bus_error &error) {
    if (code < 0) {
        std::string message = error.message ? error.message : std::strerror(-code);
        sd_bus_error_free(&error);
        throw Error({code == -EACCES
                         ? ErrorCode::permission_denied
                         : (code == -ETIMEDOUT ? ErrorCode::timeout : ErrorCode::apply_failed),
                     message});
    }
    sd_bus_error_free(&error);
}
template <class... A>
void call(Bus &b, const char *dest, const char *path, const char *iface, const char *method,
          Message &reply, const char *signature, A... args) {
    sd_bus_error e = SD_BUS_ERROR_NULL;
    int r =
        sd_bus_call_method(b.bus, dest, path, iface, method, &e, &reply.value, signature, args...);
    checked(r, e);
}
std::string property(Bus &b, const char *dest, const char *path, const char *iface,
                     const char *name) {
    char *v = nullptr;
    sd_bus_error e = SD_BUS_ERROR_NULL;
    int r = sd_bus_get_property_string(b.bus, dest, path, iface, name, &e, &v);
    checked(r, e);
    std::string s = v ? v : "";
    free(v);
    return s;
}
std::string unitState(Bus &b, const std::string &unit) {
    if (unit.empty())
        return "inactive"; // Optional listener service on socket-activated products.
    Message m;
    call(b, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
         "org.freedesktop.systemd1.Manager", "LoadUnit", m, "s", unit.c_str());
    const char *path = nullptr;
    if (sd_bus_message_read(m.value, "o", &path) < 0)
        throw Error({ErrorCode::unavailable, "invalid unit reply"});
    return property(b, "org.freedesktop.systemd1", path, "org.freedesktop.systemd1.Unit",
                    "ActiveState");
}
void unit(Bus &b, const std::string &name, bool start) {
    auto state = unitState(b, name);
    if ((start && state == "active") || (!start && (state == "inactive" || state == "failed")))
        return;
    Message m;
    call(b, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
         "org.freedesktop.systemd1.Manager", start ? "StartUnit" : "StopUnit", m, "ss",
         name.c_str(), "replace");
    // A submitted job is not completion. In particular, stop the old clock
    // discipline before enabling its replacement.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        state = unitState(b, name);
        if ((start && state == "active") || (!start && (state == "inactive" || state == "failed")))
            return;
        if (start && state == "failed")
            throw Error({ErrorCode::apply_failed, "unit failed: " + name});
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } while (std::chrono::steady_clock::now() < deadline);
    throw Error({ErrorCode::timeout, "unit transition timed out: " + name});
}

std::string trim(std::string v) {
    const auto first = v.find_first_not_of(" \r\n\t");
    if (first == std::string::npos)
        return {};
    return v.substr(first, v.find_last_not_of(" \r\n\t") - first + 1);
}
std::string address(const std::vector<std::uint8_t> &bytes, int family) {
    char buffer[INET6_ADDRSTRLEN]{};
    if (bytes.size() != (family == AF_INET ? 4u : 16u) ||
        !inet_ntop(family, bytes.data(), buffer, sizeof buffer))
        return {};
    return buffer;
}
struct AddressInfo {
    int Family = 0;
    std::vector<std::uint8_t> Address;
    unsigned PrefixLength = 0;
    std::string ConfigSource;
};
struct RouteInfo {
    int Family = 0;
    std::vector<std::uint8_t> Gateway;
    unsigned DestinationPrefixLength = 0;
    std::string ConfigSource;
};
struct DnsInfo {
    int Family = 0;
    std::vector<std::uint8_t> Address;
    std::string ConfigSource;
};
struct LinkInfo {
    std::string NetworkFile;
    std::vector<std::string> NetworkFileDropins;
    std::vector<AddressInfo> Addresses;
    std::vector<RouteInfo> Routes;
    std::vector<DnsInfo> DNS;
    std::string CarrierState;
};
LinkInfo describe(Bus &bus, const std::string &name) {
    auto index = if_nametoindex(name.c_str());
    if (!index)
        throw Error({ErrorCode::unavailable, "interface unavailable: " + name});
    Message m;
    call(bus, "org.freedesktop.network1", "/org/freedesktop/network1",
         "org.freedesktop.network1.Manager", "DescribeLink", m, "i", static_cast<int>(index));
    const char *json = nullptr;
    if (sd_bus_message_read(m.value, "s", &json) < 0 || !json)
        throw Error({ErrorCode::unavailable, "networkd returned invalid link data"});
    LinkInfo info;
    if (glz::read<glz::opts{.error_on_unknown_keys = false}>(info, std::string_view(json)))
        throw Error({ErrorCode::unavailable, "unsupported networkd description"});
    return info;
}
bool dhcpEnabled(const LinkInfo &info) {
    if (info.NetworkFile.empty())
        throw Error({ErrorCode::unavailable, "interface is not managed by networkd"});
    std::vector<std::string> files{info.NetworkFile};
    files.insert(files.end(), info.NetworkFileDropins.begin(), info.NetworkFileDropins.end());
    bool dhcp = false;
    for (const auto &file : files) {
        std::istringstream in(readFile(file));
        std::string line, section;
        while (std::getline(in, line)) {
            line = trim(line);
            if (line.starts_with("["))
                section = line;
            else if (section == "[Network]" && line.starts_with("DHCP=")) {
                auto value = trim(line.substr(5));
                dhcp = value == "yes" || value == "true" || value == "ipv4";
            }
        }
    }
    return dhcp;
}
class Native final : public Platform {
    Profile profile_;
    void interfacePolicy(const std::string &name, bool boot) {
        auto p = std::ranges::find(profile_.interfaces, name, &InterfacePolicy::name);
        if (p == profile_.interfaces.end())
            throw Error({ErrorCode::invalid_argument, "interface is not managed by this product"});
        const auto device = fs::path("/sys/class/net") / name / "device/of_node";
        if (boot && !fs::exists(device))
            return; // Match is still restricted to the installed product name.
        if (!if_nametoindex(name.c_str()) ||
            (!p->device_tree_node.empty() &&
             fs::canonical(device).filename() != p->device_tree_node))
            throw Error({ErrorCode::invalid_argument, "interface hardware identity mismatch"});
    }

  public:
    explicit Native(Profile p) : profile_(std::move(p)) {}
    Configuration discover() override {
        Bus bus;
        Configuration config = profile_.defaults;
        config.system.hostname =
            property(bus, "org.freedesktop.hostname1", "/org/freedesktop/hostname1",
                     "org.freedesktop.hostname1", "Hostname");
        config.time.timezone =
            property(bus, "org.freedesktop.timedate1", "/org/freedesktop/timedate1",
                     "org.freedesktop.timedate1", "Timezone");
        if (config.time.timezone.empty())
            config.time.timezone = "UTC";
        config.system.ssh_enabled = unitState(bus, profile_.ssh_socket) == "active" ||
                                    unitState(bus, profile_.ssh_service) == "active";
        config.system.network.clear();
        for (const auto &policy : profile_.interfaces) {
            interfacePolicy(policy.name, false);
            const auto info = describe(bus, policy.name);
            NetworkConfig n;
            n.interface = policy.name;
            n.mode = dhcpEnabled(info) ? NetworkMode::dhcp : NetworkMode::static_ipv4;
            for (const auto &a : info.Addresses)
                if (a.Family == AF_INET && a.ConfigSource == "static") {
                    if (n.mode == NetworkMode::dhcp || !n.address.empty())
                        throw Error(
                            {ErrorCode::unsupported,
                             "migration cannot represent mixed or multiple IPv4 addresses"});
                    n.address = address(a.Address, a.Family);
                    n.prefix_length = static_cast<std::uint8_t>(a.PrefixLength);
                }
            for (const auto &r : info.Routes)
                if (r.Family == AF_INET && r.DestinationPrefixLength == 0 &&
                    r.ConfigSource == "static" && !r.Gateway.empty())
                    n.gateway = address(r.Gateway, r.Family);
            for (const auto &d : info.DNS)
                if (d.Family == AF_INET && d.ConfigSource == "static")
                    n.dns_servers.push_back(address(d.Address, d.Family));
            config.system.network.push_back(n);
        }
        return config;
    }
    SystemStatus status() override {
        Bus bus;
        SystemStatus s;
        utsname u{};
        if (uname(&u) == 0)
            s.kernel_release = u.release;
        std::istringstream release(readFile("/etc/os-release"));
        std::string line;
        while (std::getline(release, line))
            if (line.starts_with("PRETTY_NAME=")) {
                s.operating_system = trim(line.substr(12));
                if (s.operating_system.starts_with('"') && s.operating_system.ends_with('"'))
                    s.operating_system =
                        s.operating_system.substr(1, s.operating_system.size() - 2);
            }
        s.ssh_listening = unitState(bus, profile_.ssh_socket) == "active" ||
                          unitState(bus, profile_.ssh_service) == "active";
        for (const auto &p : profile_.interfaces) {
            auto info = describe(bus, p.name);
            NetworkStatus n;
            n.interface = p.name;
            n.mac_address = trim(readFile(fs::path("/sys/class/net") / p.name / "address"));
            n.carrier = info.CarrierState == "carrier";
            for (const auto &a : info.Addresses) {
                auto ip = address(a.Address, a.Family);
                if (!ip.empty())
                    n.addresses.push_back(ip + "/" + std::to_string(a.PrefixLength));
            }
            for (const auto &r : info.Routes)
                if (r.Family == AF_INET && r.DestinationPrefixLength == 0 && !r.Gateway.empty())
                    n.gateway = address(r.Gateway, r.Family);
            for (const auto &d : info.DNS) {
                auto ip = address(d.Address, d.Family);
                if (!ip.empty())
                    n.dns_servers.push_back(ip);
            }
            s.network.push_back(n);
        }
        return s;
    }
    TimeStatus time() override {
        Bus bus;
        TimeStatus result;
        result.timezone = property(bus, "org.freedesktop.timedate1", "/org/freedesktop/timedate1",
                                   "org.freedesktop.timedate1", "Timezone");
        if (result.timezone.empty())
            result.timezone = "UTC";
        timespec now{};
        if (clock_gettime(CLOCK_REALTIME, &now) != 0)
            throw Error({ErrorCode::unavailable, "wall clock unavailable"});
        result.unix_time_ms = static_cast<std::int64_t>(now.tv_sec) * 1000 + now.tv_nsec / 1000000;
        result.uptime_ms = bootMilliseconds();
        tzset();
        tm utc{}, local{};
        gmtime_r(&now.tv_sec, &utc);
        localtime_r(&now.tv_sec, &local);
        result.utc_offset_seconds = static_cast<std::int32_t>(local.tm_gmtoff);
        auto format = [&](const tm &t, bool isUtc) {
            char b[64];
            strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%S", &t);
            std::ostringstream out;
            out << b << '.' << std::setw(3) << std::setfill('0') << now.tv_nsec / 1000000;
            if (isUtc)
                out << 'Z';
            else {
                auto offset = std::abs(result.utc_offset_seconds);
                out << (result.utc_offset_seconds < 0 ? '-' : '+') << std::setw(2) << offset / 3600
                    << ':' << std::setw(2) << (offset / 60) % 60;
            }
            return out.str();
        };
        result.utc_time = format(utc, true);
        result.local_time = format(local, false);
        return result;
    }
    std::vector<std::string> timezones() override {
        Bus b;
        Message m;
        call(b, "org.freedesktop.timedate1", "/org/freedesktop/timedate1",
             "org.freedesktop.timedate1", "ListTimezones", m, "");
        if (sd_bus_message_enter_container(m.value, 'a', "s") < 0)
            throw Error({ErrorCode::unavailable, "invalid timezone list"});
        std::vector<std::string> result;
        const char *value = nullptr;
        while (sd_bus_message_read(m.value, "s", &value) > 0)
            result.emplace_back(value);
        return result;
    }
    TimezoneCatalog timezoneCatalog() override {
        return readTimezoneCatalog(timezones(), std::chrono::system_clock::now(), "/usr/share/zoneinfo/zone.tab");
    }
    std::vector<Temperature> temperatures() override {
        return readTemperatures("/sys/class/hwmon", profile_.sensors);
    }
    void validateConfiguration(const Configuration &c) override {
        validate(c);
        if (c.system.network.size() != profile_.interfaces.size())
            throw Error({ErrorCode::invalid_argument, "supply all managed interfaces"});
        for (const auto &n : c.system.network)
            interfacePolicy(n.interface, false);
        auto zones = timezones();
        if (std::ranges::find(zones, c.time.timezone) == zones.end())
            throw Error({ErrorCode::invalid_argument, "timezone is not installed"});
        if (c.time.synchronization == "ptp" && profile_.ptp_units.empty())
            throw Error({ErrorCode::unsupported, "PTP is unavailable on this product"});
    }
    void preferences(const Configuration &c) override {
        Bus b;
        Message tz;
        call(b, "org.freedesktop.timedate1", "/org/freedesktop/timedate1",
             "org.freedesktop.timedate1", "SetTimezone", tz, "sb", c.time.timezone.c_str(), 0);
        Message host;
        call(b, "org.freedesktop.hostname1", "/org/freedesktop/hostname1",
             "org.freedesktop.hostname1", "SetStaticHostname", host, "sb",
             c.system.hostname.c_str(), 0);
        Message transient;
        call(b, "org.freedesktop.hostname1", "/org/freedesktop/hostname1",
             "org.freedesktop.hostname1", "SetHostname", transient, "sb", c.system.hostname.c_str(),
             0);
        const bool ntp = c.time.synchronization == "ntp";
        const auto &stop = ntp ? profile_.ptp_units : profile_.ntp_units;
        const auto &start = ntp ? profile_.ntp_units : profile_.ptp_units;
        for (auto i = stop.rbegin(); i != stop.rend(); ++i)
            unit(b, *i, false);
        for (const auto &name : start)
            unit(b, name, true);
        sshBootPolicy(c.system.ssh_enabled);
        unit(b, profile_.ssh_socket, c.system.ssh_enabled);
        if (!c.system.ssh_enabled)
            unit(b, profile_.ssh_service, false);
    }
    void network(const std::vector<NetworkConfig> &configs, bool bootstrap) override {
        validateNetwork(configs);
        if (configs.size() != profile_.interfaces.size())
            throw Error({ErrorCode::invalid_argument, "supply all managed interfaces"});
        // Bootstrap runs with umask 0077, but networkd reads these as its service user.
        const fs::path directory = "/run/systemd/network";
        fs::create_directories(directory);
        fs::permissions(directory, fs::perms::owner_all | fs::perms::group_read |
                                       fs::perms::group_exec | fs::perms::others_read |
                                       fs::perms::others_exec);
        // networkd has no persistent address setter. These generated files are owned exclusively
        // here.
        for (const auto &c : configs) {
            interfacePolicy(c.interface, bootstrap);
            const bool hasPreferred = std::ranges::any_of(configs, &NetworkConfig::preferred_default);
            const auto rendered = renderNetwork(c, hasPreferred);
            const fs::path path =
                fs::path("/run/systemd/network") / ("10-mnc-" + c.interface + ".network");
            atomicWrite(path, rendered);
            fs::permissions(path, fs::perms::owner_read | fs::perms::owner_write |
                                      fs::perms::group_read | fs::perms::others_read);
        }
        if (bootstrap)
            return;
        Bus b;
        Message reload;
        call(b, "org.freedesktop.network1", "/org/freedesktop/network1",
             "org.freedesktop.network1.Manager", "Reload", reload, "");
        for (const auto &c : configs) {
            Message m;
            call(b, "org.freedesktop.network1", "/org/freedesktop/network1",
                 "org.freedesktop.network1.Manager", "ReconfigureLink", m, "i",
                 static_cast<int>(if_nametoindex(c.interface.c_str())));
        }
    }
    void sshBootPolicy(bool enabled) override {
        const fs::path marker = "/run/mnc-system/ssh-disabled";
        if (enabled)
            durableRemove(marker);
        else
            atomicWrite(marker, "disabled\n");
    }
    void power(PowerAction action) override {
        Bus b;
        Message m;
        call(b, "org.freedesktop.login1", "/org/freedesktop/login1",
             "org.freedesktop.login1.Manager",
             action == PowerAction::reboot ? "Reboot" : "PowerOff", m, "b", 0);
    }
};
} // namespace
std::unique_ptr<Platform> nativePlatform(const Profile &p) { return std::make_unique<Native>(p); }
std::vector<Temperature> readTemperatures(const fs::path &root,
                                          const std::vector<SensorPolicy> &sensors) {
    std::vector<Temperature> result;
    for (const auto &s : sensors)
        result.push_back({s.zone, s.label, {}});
    std::error_code ec;
    fs::directory_iterator devices(root, ec);
    if (ec)
        throw Error({ErrorCode::unavailable, "hardware monitors unavailable"});
    for (const auto &device : devices) {
        fs::directory_iterator entries(device.path(), ec);
        if (ec) {
            ec.clear();
            continue;
        }
        for (const auto &entry : entries) {
            auto name = entry.path().filename().string();
            if (!name.starts_with("temp") || !name.ends_with("_label"))
                continue;
            try {
                const auto label = trim(readFile(entry.path(), 256));
                auto found = std::ranges::find(result, label, &Temperature::label);
                if (found == result.end())
                    continue;
                name.replace(name.size() - 6, 6, "_input");
                auto v = trim(readFile(device.path() / name, 128));
                std::int64_t n = 0;
                auto parsed = std::from_chars(v.data(), v.data() + v.size(), n);
                if (parsed.ec == std::errc{} && parsed.ptr == v.data() + v.size())
                    found->millidegrees_c = n;
            } catch (const Error &) { /* Keep the sensor explicitly unavailable. */
            }
        }
    }
    return result;
}
} // namespace mnc::os::system
