// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mnc/system/system.hpp"
#include <chrono>
#include <stdexcept>
namespace mnc::os::system {
/** One connection per request: safe to use across HTTP worker threads. */
class Client final : public mnc::system::SystemManager {
  public:
    explicit Client(std::chrono::milliseconds timeout = std::chrono::seconds{60})
        : timeout_(timeout) {
        if (timeout.count() <= 0 || timeout > std::chrono::seconds{60})
            throw std::invalid_argument("system manager timeout must be in (0, 60s]");
    }
    [[nodiscard]] std::chrono::milliseconds requestTimeout() const { return timeout_; }
    mnc::system::Result<mnc::system::SystemStatus> status() override;
    mnc::system::Result<mnc::system::TimeStatus> time() override;
    mnc::system::Result<std::vector<std::string>> timezones() override;
    mnc::system::Result<mnc::system::TimezoneCatalog> timezoneCatalog() override;
    mnc::system::Result<std::vector<mnc::system::Temperature>> temperatures() override;
    mnc::system::Result<void> apply(const mnc::system::Configuration &) override;
    mnc::system::Result<mnc::system::NetworkTransaction>
    beginNetwork(const mnc::system::NetworkProposal &) override;
    mnc::system::Result<mnc::system::NetworkTransaction> networkTransaction() override;
    mnc::system::Result<void> prepareNetworkCommit(const std::string &) override;
    mnc::system::Result<void> finishNetwork(const std::string &, bool) override;
    mnc::system::Result<mnc::system::Job> power(mnc::system::PowerAction) override;
    mnc::system::Result<mnc::system::Job> resetDevice(bool) override;
    mnc::system::Result<mnc::system::Job> job() override;
  private:
    std::chrono::milliseconds timeout_;
};
} // namespace mnc::os::system
