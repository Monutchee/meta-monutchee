// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mnc/system/system.hpp"
namespace mnc::os::system {
/** One connection per request: safe to use across HTTP worker threads. */
class Client final : public mnc::system::SystemManager {
  public:
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
};
} // namespace mnc::os::system
