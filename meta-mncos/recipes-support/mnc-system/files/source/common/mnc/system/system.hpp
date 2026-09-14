#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include <cstdint>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace mnc::system {
enum class ErrorCode {
    none,
    invalid_argument,
    permission_denied,
    unavailable,
    unsupported,
    busy,
    apply_failed,
    timeout,
    internal_error
};
struct SystemError {
    ErrorCode code = ErrorCode::internal_error;
    std::string message;
};
template <class T> using Result = std::expected<T, SystemError>;
class Error : public std::runtime_error {
  public:
    explicit Error(SystemError error) : std::runtime_error(error.message), code(error.code) {}
    ErrorCode code;
};
template <class T> T require(Result<T> result) {
    if (!result)
        throw Error(result.error());
    if constexpr (!std::is_void_v<T>)
        return std::move(*result);
}
enum class NetworkMode { dhcp, static_ipv4 };
struct NetworkConfig {
    std::string interface;
    NetworkMode mode = NetworkMode::dhcp;
    std::string address;
    std::uint8_t prefix_length = 24;
    std::string gateway;
    std::vector<std::string> dns_servers;
    bool operator==(const NetworkConfig &) const = default;
};
struct Preferences {
    std::string hostname = "mnc";
    bool ssh_enabled = true;
    std::vector<NetworkConfig> network;
    bool operator==(const Preferences &) const = default;
};
struct TimePreferences {
    std::string synchronization = "ntp";
    std::string timezone = "UTC";
    bool operator==(const TimePreferences &) const = default;
};
struct Configuration {
    Preferences system;
    TimePreferences time;
    bool operator==(const Configuration &) const = default;
};
struct TimeStatus {
    std::int64_t unix_time_ms = 0;
    std::string utc_time;
    std::string local_time;
    std::string timezone;
    std::int32_t utc_offset_seconds = 0;
    std::uint64_t uptime_ms = 0;
};
struct Temperature {
    std::string zone;
    std::string label;
    std::optional<std::int64_t> millidegrees_c;
    bool available() const noexcept { return millidegrees_c.has_value(); }
    double celsius() const noexcept { return millidegrees_c.value_or(0) / 1000.0; }
};
struct NetworkStatus {
    std::string interface;
    bool carrier = false;
    std::vector<std::string> addresses;
    std::string gateway;
    std::vector<std::string> dns_servers;
};
struct SystemStatus {
    Configuration configuration;
    std::vector<NetworkStatus> network;
    bool ssh_listening = false;
    std::string kernel_release;
    std::string operating_system;
};
struct NetworkProposal {
    std::vector<NetworkConfig> network;
    std::string previous_settings_hash;
    std::string candidate_settings_hash;
};
struct NetworkTransaction {
    std::string id;
    std::string state = "idle";
    std::uint64_t remaining_ms = 0;
    std::vector<NetworkConfig> network;
    std::string message;
};
struct Job {
    std::string id;
    std::string action;
    std::string state;
};
enum class PowerAction { reboot, shutdown };
/** OS-independent contract. Persistent writes are invoked by the settings authority. */
class SystemManager {
  public:
    virtual ~SystemManager() = default;
    virtual Result<SystemStatus> status() = 0;
    virtual Result<TimeStatus> time() = 0;
    virtual Result<std::vector<std::string>> timezones() = 0;
    virtual Result<std::vector<Temperature>> temperatures() = 0;
    virtual Result<void> apply(const Configuration &) = 0;
    virtual Result<NetworkTransaction> beginNetwork(const NetworkProposal &) = 0;
    virtual Result<NetworkTransaction> networkTransaction() = 0;
    virtual Result<void> prepareNetworkCommit(const std::string &id) = 0;
    virtual Result<void> finishNetwork(const std::string &id, bool commit) = 0;
    virtual Result<Job> power(PowerAction action) = 0;
    virtual Result<Job> resetDevice(bool confirmed) = 0;
    virtual Result<Job> job() = 0;
};
void validate(const Configuration &);
void validateNetwork(const std::vector<NetworkConfig> &);
} // namespace mnc::system
