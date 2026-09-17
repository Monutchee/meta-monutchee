#pragma once
// SPDX-License-Identifier: GPL-3.0-only
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mnc::xilinx::sysmon {
struct Reading {
  // Device-tree identity plus IIO channel attribute, independent of
  // iio:deviceN.
  std::string id;
  std::string device;
  std::string source_channel;
  std::string label;
  // Physical rail/sensor domain, not the ADC sampling bank. Unknown is
  // explicit.
  std::string domain = "unknown";
  std::string kind;
  std::string unit;
  std::optional<double> value;
  std::int64_t sampled_at_unix_ms = 0;
  std::string status = "unavailable";
  std::string error;
};
struct Snapshot {
  std::string status = "unavailable";
  std::string error;
  std::int64_t sampled_at_unix_ms = 0;
  std::vector<Reading> readings;
};
struct Selector {
  std::string compatible = "xlnx,zynqmp-ams";
  // Optional board-selected node basename; never an IIO enumeration index.
  std::string device_tree_node;
};
class Monitor {
public:
  virtual ~Monitor() = default;
  virtual Snapshot readSnapshot() const = 0;
};
/** Read-only Linux AMS provider. No settings, shell, direct registers or
 * daemon. */
class IioMonitor final : public Monitor {
public:
  explicit IioMonitor(Selector selector = {},
                      std::filesystem::path root = "/sys/bus/iio/devices");
  Snapshot readSnapshot() const override;

private:
  Selector selector_;
  std::filesystem::path root_;
};
} // namespace mnc::xilinx::sysmon
