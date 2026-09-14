// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mnc/system/json.hpp"
#include <glaze/glaze.hpp>
namespace mnc::os::system {
inline constexpr auto bus_name = "com.monutchee.MNCOS.System";
inline constexpr auto object_path = "/com/monutchee/MNCOS/System";
inline constexpr auto interface_name = "com.monutchee.MNCOS.System1";
inline constexpr std::size_t max_message_size = 65536;
struct Reply {
    mnc::system::ErrorCode code = mnc::system::ErrorCode::none;
    std::string message;
    std::string json;
};
struct Empty {};
struct Id {
    std::string id;
};
struct Finish {
    std::string id;
    bool commit = false;
};
struct Power {
    mnc::system::PowerAction action = mnc::system::PowerAction::reboot;
};
struct Reset {
    bool confirmed = false;
};
template <class T> T decode(std::string_view text, std::size_t limit = max_message_size) {
    T result{};
    if (text.size() > limit)
        throw mnc::system::Error({mnc::system::ErrorCode::invalid_argument, "message too large"});
    if (auto e = glz::read_json(result, text))
        throw mnc::system::Error({mnc::system::ErrorCode::invalid_argument,
                                  "invalid system request: " + glz::format_error(e, text)});
    return result;
}
template <class T> std::string encode(const T &value) {
    auto result = glz::write_json(value);
    if (!result)
        throw mnc::system::Error(
            {mnc::system::ErrorCode::internal_error, "cannot encode system message"});
    return *result;
}
} // namespace mnc::os::system
