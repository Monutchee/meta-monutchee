#pragma once
#include "mnc/system/system.hpp"
#include <algorithm>
namespace mnc::system {
/** Forward identity only across an explicitly configured native authority boundary. */
inline AuditContext trustedAuditContext(const AuditContext &context, std::uint32_t peer_uid,
                                       std::optional<std::uint32_t> trusted_uid)
{
    const auto safe = [](std::string_view value, std::size_t bound) {
        return value.size() <= bound && std::ranges::none_of(value,
            [](unsigned char c) { return c < 32 || c == 127; });
    };
    if (!trusted_uid || peer_uid != *trusted_uid)
        return {"uid:" + std::to_string(peer_uid), {}};
    return {context.actor.empty() || !safe(context.actor, 256) ? "unknown" : context.actor,
            safe(context.request_id, 128) ? context.request_id : std::string{}};
}
} // namespace mnc::system
