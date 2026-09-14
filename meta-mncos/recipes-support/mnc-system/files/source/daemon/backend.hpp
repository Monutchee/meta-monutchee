// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mnc/os/system/protocol.hpp"
#include <filesystem>
#include <functional>
#include <memory>
namespace mnc::os::system {
using namespace mnc::system;
struct InterfacePolicy {
    std::string name;
    std::string device_tree_node;
};
struct SensorPolicy {
    std::string zone;
    std::string label;
};
/** Installed, root-owned product policy. Never supplied over IPC. */
struct Profile {
    std::string settings_user;
    std::string control_user;
    std::string state_directory;
    std::string active_settings;
    std::vector<InterfacePolicy> interfaces;
    std::vector<SensorPolicy> sensors;
    Configuration defaults;
    std::string ssh_socket = "sshd.socket";
    std::string ssh_service = "sshd.service";
    std::vector<std::string> ntp_units = {"systemd-timesyncd.service"};
    std::vector<std::string> ptp_units;
    std::vector<std::string> reset_paths;
};
Profile loadProfile(const std::filesystem::path &);
std::string readFile(const std::filesystem::path &, std::size_t limit = 1048576);
void atomicWrite(const std::filesystem::path &, std::string_view);
void durableRemove(const std::filesystem::path &);
std::string fileHash(const std::filesystem::path &);
std::string randomId();
std::uint64_t bootMilliseconds();
std::vector<Temperature> readTemperatures(const std::filesystem::path &,
                                          const std::vector<SensorPolicy> &);
/** Native mechanisms are replaceable in tests; no caller-selected OS paths. */
class Platform {
  public:
    virtual ~Platform() = default;
    virtual Configuration discover() = 0;
    virtual SystemStatus status() = 0;
    virtual TimeStatus time() = 0;
    virtual std::vector<std::string> timezones() = 0;
    virtual std::vector<Temperature> temperatures() = 0;
    virtual void validateConfiguration(const Configuration &) = 0;
    virtual void preferences(const Configuration &) = 0;
    virtual void network(const std::vector<NetworkConfig> &, bool bootstrap) = 0;
    virtual void sshBootPolicy(bool) = 0;
    virtual void power(PowerAction) = 0;
};
std::unique_ptr<Platform> nativePlatform(const Profile &);
struct Pending {
    NetworkTransaction transaction;
    std::vector<NetworkConfig> previous;
    std::string previous_hash;
    std::string candidate_hash;
    std::uint64_t deadline = 0;
};
struct State {
    Configuration configuration;
    bool initialized = false;
    Pending pending;
    Job job;
    bool reset_intent = false;
};
/** Serialized by the sd-bus worker. All durable changes precede their effects. */
class Backend {
  public:
    Backend(Profile profile, Platform &platform,
            std::function<std::uint64_t()> clock = bootMilliseconds);
    void bootstrap();
    void start();
    void tick();
    SystemStatus status();
    TimeStatus time() { return platform_.time(); }
    std::vector<std::string> timezones() { return platform_.timezones(); }
    std::vector<Temperature> temperatures() { return platform_.temperatures(); }
    void apply(const Configuration &);
    NetworkTransaction beginNetwork(const NetworkProposal &);
    NetworkTransaction networkTransaction();
    void prepareNetworkCommit(const std::string &);
    void finishNetwork(const std::string &, bool);
    Job power(PowerAction);
    Job resetDevice(bool);
    Job job() const { return state_.job; }
    const Profile &profile() const { return profile_; }

  private:
    void persist();
    void recoverNetwork(bool bootstrap);
    void settleNetwork(bool commit, bool bootstrap = false);
    void idle();
    bool pending() const;
    void checkId(const std::string &);
    Profile profile_;
    Platform &platform_;
    std::function<std::uint64_t()> clock_;
    State state_;
    bool storage_failed_ = false;
    std::uint64_t job_deadline_ = 0;
};
} // namespace mnc::os::system
