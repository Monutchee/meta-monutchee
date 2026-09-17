// SPDX-License-Identifier: GPL-3.0-only
#include "timezone_catalog.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <map>
#include <sstream>
namespace mnc::os::system {
namespace {
std::optional<double> coordinate(std::string_view text, unsigned degrees) {
  if (text.empty() || (text.front() != '+' && text.front() != '-'))
    return {};
  const bool negative = text.front() == '-';
  text.remove_prefix(1);
  if (text.size() != degrees + 2 && text.size() != degrees + 4)
    return {};
  auto number = [](std::string_view s) -> int {
    int n = 0;
    auto result = std::from_chars(s.data(), s.data() + s.size(), n);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size() ? n
                                                                         : -1;
  };
  const int d = number(text.substr(0, degrees));
  const int m = number(text.substr(degrees, 2));
  const int s =
      text.size() == degrees + 4 ? number(text.substr(degrees + 2)) : 0;
  const int maximum = degrees == 2 ? 90 : 180;
  if (d < 0 || d > maximum || m < 0 || m >= 60 || s < 0 || s >= 60 ||
      (d == maximum && (m || s)))
    return {};
  return (negative ? -1 : 1) * (d + m / 60.0 + s / 3600.0);
}
} // namespace
mnc::system::TimezoneCatalog
readTimezoneCatalog(const std::vector<std::string> &installed,
                    std::chrono::system_clock::time_point instant,
                    const std::filesystem::path &zoneTable) {
  using namespace mnc::system;
  std::map<std::string, TimezoneLocation, std::less<>> locations;
  std::ifstream input(zoneTable);
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    std::istringstream fields(line);
    std::string country, coordinates, id;
    if (!(fields >> country >> coordinates >> id))
      continue;
    const auto split = coordinates.find_first_of("+-", 1);
    if (split == std::string::npos)
      continue;
    const auto latitude =
        coordinate(std::string_view(coordinates).substr(0, split), 2);
    const auto longitude =
        coordinate(std::string_view(coordinates).substr(split), 3);
    if (latitude && longitude)
      locations.emplace(id, TimezoneLocation{*latitude, *longitude});
  }
  TimezoneCatalog result;
  result.generated_at_unix_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          instant.time_since_epoch())
          .count();
  auto names = installed;
  std::ranges::sort(names);
  names.erase(std::unique(names.begin(), names.end()), names.end());
  for (const auto &name : names) {
    Timezone entry;
    entry.id = name;
    try {
      // libstdc++ reads the installed Linux tzdata. No process-wide TZ
      // mutation.
      const auto *zone = std::chrono::locate_zone(name);
      entry.utc_offset_seconds =
          static_cast<std::int32_t>(zone->get_info(instant).offset.count());
      auto location = locations.find(name);
      if (location == locations.end())
        location = locations.find(zone->name());
      if (location != locations.end())
        entry.location = location->second;
    } catch (const std::exception &) {
      // Keep every installed identifier, including entries missing from
      // tzdata.zi. Unknown is not UTC; the caller can still select the
      // installed zone.
    }
    result.zones.push_back(std::move(entry));
  }
  return result;
}
} // namespace mnc::os::system
