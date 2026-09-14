// SPDX-License-Identifier: GPL-3.0-only
#include "mnc/os/system/protocol.hpp"
#include "network_config.hpp"
#include "timezone_catalog.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace mnc::os::system;
using namespace mnc::system;
void check(bool ok) {
  if (!ok)
    throw std::runtime_error("catalog/network check failed");
}
int main() {
  using namespace std::chrono;
  const auto path = std::filesystem::temp_directory_path() /
                    ("mnc-zones-" + std::to_string(getpid()));
  std::ofstream(path) << "# comment\nCA +4339-07923 America/Toronto\nNP "
                         "+2743+08519 Asia/Kathmandu\nXX invalid UTC\n";
  const std::vector<std::string> names{
      "UTC", "America/Toronto", "Asia/Kathmandu", "Not/Installed", "UTC"};
  const auto january =
      readTimezoneCatalog(names, sys_days{2026y / January / 15}, path);
  const auto july =
      readTimezoneCatalog(names, sys_days{2026y / July / 15}, path);
  check(january.zones.size() == 4 && july.zones.size() == 4);
  check(january.zones[0].id == "America/Toronto" &&
        january.zones[0].utc_offset_seconds == -18000);
  check(july.zones[0].utc_offset_seconds == -14400);
  check(january.zones[0].location &&
        january.zones[0].location->longitude < -79.38);
  check(january.zones[1].utc_offset_seconds == 20700);
  check(!january.zones[2].utc_offset_seconds && !january.zones[2].location);
  check(january.zones[3].utc_offset_seconds == 0 && !january.zones[3].location);
  check(readTimezoneCatalog(names, sys_days{2026y / July / 15},
                            path.string() + "missing")
            .zones.size() == 4);
  std::filesystem::remove(path);
  // Large catalogs cross the old 64KiB reply limit but requests remain bounded.
  TimezoneCatalog large;
  for (int i = 0; i < 700; ++i)
    large.zones.push_back({"America/Example_Long_Name_" + std::to_string(i),
                           -14400, TimezoneLocation{43, -79}});
  auto encoded = encode(large);
  check(encoded.size() > max_message_size);
  check(decode<TimezoneCatalog>(encoded, 512 * 1024).zones.size() == 700);
  bool rejected = false;
  try {
    (void)decode<TimezoneCatalog>(encoded);
  } catch (const Error &) {
    rejected = true;
  }
  check(rejected);
  NetworkConfig n{"end0", NetworkMode::dhcp, "", 24, "", {}};
  check(renderNetwork(n, false).find("RouteMetric") == std::string::npos);
  n.preferred_default = true;
  auto preferred = renderNetwork(n, true);
  check(preferred.find("RouteMetric=100") != std::string::npos);
  check(preferred.find("[IPv6AcceptRA]\nUseDNS=yes\nRouteMetric=100") !=
        std::string::npos);
  n.preferred_default = false;
  check(renderNetwork(n, true).find("RouteMetric=600") != std::string::npos);
  n.mode = NetworkMode::static_ipv4;
  n.address = "10.0.0.2";
  n.gateway = "10.0.0.1";
  check(renderNetwork(n, true).find("[Route]\nGateway=10.0.0.1\nMetric=600") !=
        std::string::npos);
  auto other = n;
  other.interface = "end1";
  other.preferred_default = true;
  n.preferred_default = true;
  rejected = false;
  try {
    validateNetwork({n, other});
  } catch (const Error &) {
    rejected = true;
  }
  check(rejected);
  n.gateway.clear();
  rejected = false;
  try {
    validateNetwork({n});
  } catch (const Error &) {
    rejected = true;
  }
  check(rejected);
}
