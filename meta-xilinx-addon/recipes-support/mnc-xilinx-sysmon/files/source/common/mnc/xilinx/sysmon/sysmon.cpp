// SPDX-License-Identifier: GPL-3.0-only
#include "sysmon.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>

namespace mnc::xilinx::sysmon {
namespace {
namespace fs = std::filesystem;
std::int64_t now() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
std::string read(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("attribute unreadable");
  std::string value(4097, '\0');
  stream.read(value.data(), static_cast<std::streamsize>(value.size()));
  if (stream.bad() || value.size() == static_cast<std::size_t>(stream.gcount()))
    throw std::runtime_error("attribute invalid or too large");
  value.resize(static_cast<std::size_t>(stream.gcount()));
  while (!value.empty() &&
         (value.back() == '\n' || value.back() == '\r' || value.back() == ' '))
    value.pop_back();
  return value;
}
double number(const fs::path &path) {
  const auto text = read(path);
  double value = 0;
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() ||
      !std::isfinite(value))
    throw std::runtime_error("invalid numeric attribute");
  return value;
}
bool hasAttribute(const fs::path &path) {
  std::error_code error;
  const bool found = fs::exists(path, error);
  if (error)
    throw std::runtime_error("attribute inaccessible");
  return found;
}
fs::path attribute(const fs::path &device, const std::string &channel,
                   const std::string &kind, const std::string &suffix) {
  const auto separate = device / (channel + suffix);
  return hasAttribute(separate) ? separate : device / ("in_" + kind + suffix);
}
bool compatible(const fs::path &node, const std::string &expected) {
  const auto values = read(node / "compatible");
  for (std::size_t start = 0; start < values.size();) {
    const auto end = values.find('\0', start);
    if (values.substr(start, end == std::string::npos ? end : end - start) ==
        expected)
      return true;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return false;
}
std::string domain(const std::string &label) {
  if (label.starts_with("VCC_PS") || label.starts_with("PS_") ||
      label == "Temp_LPD" || label == "Temp_FPD")
    return "PS";
  if (label == "VCCINT" || label == "VCCAUX" || label == "VCCBRAM" ||
      label == "VCCAMS" || label == "Temp_PL")
    return "PL";
  return "unknown";
}
std::string channelKind(const std::string &channel) {
  for (const std::string kind : {"voltage", "temp"}) {
    const auto prefix = "in_" + kind;
    if (!channel.starts_with(prefix))
      continue;
    const auto index = channel.substr(prefix.size());
    if (!index.empty() &&
        std::ranges::all_of(index, [](char c) { return c >= '0' && c <= '9'; }))
      return kind;
  }
  return {};
}
void sampleDevice(Snapshot &snapshot, const fs::path &device,
                  const std::string &identity) {
  // Include label-only channels so removal/read failures cannot become a
  // numeric zero.
  std::map<std::string, std::string> channels;
  for (const auto &entry : fs::directory_iterator(device)) {
    const auto name = entry.path().filename().string();
    for (const std::string suffix : {"_raw", "_input", "_label"}) {
      if (!name.ends_with(suffix))
        continue;
      const auto base = name.substr(0, name.size() - suffix.size());
      auto kind = channelKind(base);
      if (!kind.empty())
        channels.emplace(base, std::move(kind));
    }
    if (channels.size() > 256)
      throw std::runtime_error("too many AMS channels");
  }
  for (const auto &[channel, kind] : channels) {
    Reading reading;
    reading.id = identity + "/" + channel;
    reading.device = identity;
    reading.source_channel = channel;
    reading.label = channel;
    reading.kind = kind == "temp" ? "temperature" : "voltage";
    reading.unit = kind == "temp" ? "°C" : "V";
    try {
      const auto label = device / (channel + "_label");
      if (hasAttribute(label)) {
        const auto value = read(label);
        if (!value.empty())
          reading.label = value;
      }
      reading.domain = domain(reading.label);
      // IIO voltage and temperature engineering values are mV and millidegrees
      // C.
      double value;
      if (hasAttribute(device / (channel + "_input"))) {
        value = number(device / (channel + "_input"));
      } else {
        const double raw = number(device / (channel + "_raw"));
        const double scale = number(attribute(device, channel, kind, "_scale"));
        const auto offsetFile = attribute(device, channel, kind, "_offset");
        const double offset = hasAttribute(offsetFile) ? number(offsetFile) : 0;
        if (scale <= 0)
          throw std::runtime_error("invalid channel scale");
        value = (raw + offset) * scale;
      }
      value /= 1000.0;
      if (!std::isfinite(value))
        throw std::runtime_error("converted value overflow");
      reading.value = value;
      reading.status = "ok";
    } catch (const std::exception &error) {
      reading.error = error.what();
    }
    reading.sampled_at_unix_ms = now();
    snapshot.readings.push_back(std::move(reading));
  }
}
} // namespace
IioMonitor::IioMonitor(Selector selector, std::filesystem::path root)
    : selector_(std::move(selector)), root_(std::move(root)) {}
Snapshot IioMonitor::readSnapshot() const {
  Snapshot snapshot;
  snapshot.sampled_at_unix_ms = now();
  try {
    if (selector_.compatible != "xlnx,zynqmp-ams") {
      snapshot.status = "unsupported";
      snapshot.error = "unsupported SYSMON compatible";
      return snapshot;
    }
    std::vector<fs::path> devices;
    for (const auto &entry : fs::directory_iterator(root_)) {
      if (entry.path().filename().string().starts_with("iio:device"))
        devices.push_back(entry.path());
      if (devices.size() > 64)
        throw std::runtime_error("too many IIO devices");
    }
    std::ranges::sort(devices);
    for (const auto &device : devices) {
      // Other IIO devices include acquisition devices: never read their
      // channels.
      std::string name;
      try {
        name = read(device / "name");
      } catch (...) {
        continue;
      }
      if (name != "xilinx-ams")
        continue;
      auto node = device / "of_node";
      if (!hasAttribute(node))
        node = device / "device/of_node";
      if (!compatible(node, selector_.compatible))
        continue;
      const auto canonical = fs::canonical(node);
      if (!selector_.device_tree_node.empty() &&
          canonical.filename() != selector_.device_tree_node)
        continue;
      // Keep the full OF identity (without host sysfs prefixes) for multiple
      // AMS devices.
      auto identity = canonical.generic_string();
      const auto base = identity.find("/base/");
      identity = base == std::string::npos ? canonical.filename().string()
                                           : identity.substr(base + 6);
      sampleDevice(snapshot, device, identity);
    }
    std::ranges::sort(snapshot.readings, {}, &Reading::id);
    if (snapshot.readings.empty()) {
      snapshot.error = "no matching AMS channels exposed";
    } else {
      snapshot.status =
          std::ranges::all_of(snapshot.readings,
                              [](const auto &r) { return r.value.has_value(); })
              ? "ok"
              : "partial";
    }
  } catch (const std::exception &) {
    snapshot.status = snapshot.readings.empty() ? "unavailable" : "partial";
    snapshot.error = "AMS discovery or channel enumeration failed";
  }
  return snapshot;
}
} // namespace mnc::xilinx::sysmon
