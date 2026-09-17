// SPDX-License-Identifier: GPL-3.0-only
#include "mnc/xilinx/sysmon/sysmon.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace fs = std::filesystem;
using namespace mnc::xilinx::sysmon;
void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
void write(const fs::path &path, const std::string &value) {
  fs::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << value;
}
const Reading &reading(const Snapshot &s, const std::string &channel) {
  const auto i =
      std::ranges::find(s.readings, channel, &Reading::source_channel);
  if (i == s.readings.end())
    throw std::runtime_error("missing channel");
  return *i;
}
int main() {
  char pattern[] = "/tmp/mnc-sysmon-test-XXXXXX";
  const char *directory = mkdtemp(pattern);
  if (!directory)
    return 1;
  const fs::path root(directory);
  try {
    const auto device = root / "iio/iio:device7";
    const auto node = root / "firmware/devicetree/base/axi/ams@ffa50000";
    write(node / "compatible", std::string("xlnx,zynqmp-ams\0", 16));
    write(device / "name", "xilinx-ams\n");
    fs::create_directory_symlink(node, device / "of_node");
    write(root / "iio/iio:device0/name", "meter-acquisition\n");
    write(root / "iio/iio:device0/in_voltage0_raw", "12345");
    auto rail = [&](const std::string &channel, const std::string &label,
                    const std::string &raw) {
      write(device / (channel + "_label"), label + "\n");
      write(device / (channel + "_raw"), raw + "\n");
      write(device / (channel + "_scale"), "0.5\n");
    };
    rail("in_voltage2", "VCCAUX", "3600");
    rail("in_voltage3", "VCC_PSINTLP", "1692");
    rail("in_voltage4", "VCCBRAM", "1722");
    rail("in_voltage5", "VCC_PSDDR", "2402");
    rail("in_voltage6", "VCCINT", "1442");
    rail("in_voltage16", "VCCINT", "1440");
    rail("in_voltage42", "FutureRail", "0");
    write(device / "in_temp0_label", "Temp_LPD");
    write(device / "in_temp0_raw", "100");
    write(device / "in_temp0_offset", "-110");
    write(device / "in_temp0_scale", "500");
    write(device / "in_temp1_label", "Temp_PL");
    write(device / "in_temp1_input", "41717");
    // Prefer engineering input over raw; do not apply the scale twice.
    write(device / "in_temp1_raw", "9999");
    write(device / "in_temp1_scale", "9999");
    IioMonitor monitor({"xlnx,zynqmp-ams", "ams@ffa50000"}, root / "iio");
    auto snapshot = monitor.readSnapshot();
    check(snapshot.status == "ok" && snapshot.readings.size() == 9,
          "enumerate only AMS channels");
    check(std::abs(*reading(snapshot, "in_voltage2").value - 1.8) < 1e-9,
          "mV to volts");
    check(std::abs(*reading(snapshot, "in_voltage6").value - 0.721) < 1e-9,
          "VCCINT scaling");
    check(*reading(snapshot, "in_voltage42").value == 0,
          "zero is a valid reading");
    check(reading(snapshot, "in_voltage42").domain == "unknown",
          "unknown rail retained");
    check(*reading(snapshot, "in_temp0").value == -5,
          "temperature offset precedes scale");
    check(std::abs(*reading(snapshot, "in_temp1").value - 41.717) < 1e-9,
          "processed input conversion");
    check(reading(snapshot, "in_voltage3").domain == "PS",
          "physical rail domain");
    check(reading(snapshot, "in_voltage6").id !=
              reading(snapshot, "in_voltage16").id,
          "duplicate rail labels preserved");
    const auto originalId = reading(snapshot, "in_voltage6").id;
    fs::rename(device, root / "iio/iio:device19");
    snapshot = monitor.readSnapshot();
    check(reading(snapshot, "in_voltage6").id == originalId,
          "IIO enumeration cannot change identity");
    const auto moved = root / "iio/iio:device19";
    fs::remove(moved / "in_voltage6_scale");
    snapshot = monitor.readSnapshot();
    check(snapshot.status == "partial" &&
              !reading(snapshot, "in_voltage6").value,
          "missing scale not guessed");
    check(reading(snapshot, "in_voltage2").value.has_value(),
          "one failure cannot hide healthy rails");
    write(moved / "in_voltage_scale", "0.5");
    check(monitor.readSnapshot().status == "ok", "shared scale supported");
    for (const auto *invalid : {"nan", "inf", "1junk", "1e999"}) {
      write(moved / "in_voltage6_raw", invalid);
      check(!reading(monitor.readSnapshot(), "in_voltage6").value,
            "reject malformed/nonfinite raw");
    }
    write(moved / "in_voltage6_raw", "1442");
    write(moved / "in_voltage6_offset", "broken");
    check(!reading(monitor.readSnapshot(), "in_voltage6").value,
          "invalid offset not silently zero");
    fs::remove(moved / "in_voltage6_offset");
    write(moved / "in_voltage6_scale", "-1");
    check(!reading(monitor.readSnapshot(), "in_voltage6").value,
          "negative scale rejected");
    fs::remove(moved / "in_voltage6_scale");
    fs::remove(moved / "in_voltage6_raw");
    check(!reading(monitor.readSnapshot(), "in_voltage6").value,
          "label-only removed channel retained");
    check(IioMonitor({"xlnx,zynqmp-ams", "another-node"}, root / "iio")
              .readSnapshot()
              .readings.empty(),
          "target selector respected");
    check(IioMonitor({"unknown", ""}, root / "iio").readSnapshot().status ==
              "unsupported",
          "unsupported silicon explicit");
    check(IioMonitor({}, root / "missing").readSnapshot().status ==
              "unavailable",
          "missing hardware explicit");
    fs::remove_all(moved);
    check(monitor.readSnapshot().status == "unavailable",
          "device disappearance not stale success");
    fs::remove_all(root);
    std::cout
        << "SYSMON discovery, conversion, identity and failure tests passed\n";
  } catch (const std::exception &error) {
    fs::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
