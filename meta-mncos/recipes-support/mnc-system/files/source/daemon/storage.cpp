// SPDX-License-Identifier: GPL-3.0-only
#include "backend.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <openssl/evp.h>
#include <sstream>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>
namespace mnc::os::system {
namespace fs = std::filesystem;
namespace {
void io(bool ok, const char *what) {
    if (!ok)
        throw Error({ErrorCode::apply_failed, std::string(what) + ": " + std::strerror(errno)});
}
void syncDirectory(const fs::path &p) {
    int fd = open(p.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    io(fd >= 0, "open directory");
    int r = fsync(fd);
    close(fd);
    io(r == 0, "sync directory");
}
} // namespace
std::string readFile(const fs::path &p, std::size_t limit) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw Error({ErrorCode::unavailable, "cannot read " + p.string()});
    std::string value;
    char b[4096];
    while (f) {
        f.read(b, sizeof b);
        value.append(b, static_cast<std::size_t>(f.gcount()));
        if (value.size() > limit)
            throw Error({ErrorCode::invalid_argument, "file too large"});
    }
    if (!f.eof())
        throw Error({ErrorCode::unavailable, "cannot finish reading " + p.string()});
    return value;
}
void atomicWrite(const fs::path &p, std::string_view value) {
    fs::create_directories(p.parent_path());
    const auto tmp = p.string() + "." + randomId() + ".tmp";
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    io(fd >= 0, "create state file");
    try {
        while (!value.empty()) {
            ssize_t n = write(fd, value.data(), value.size());
            if (n < 0 && errno == EINTR)
                continue;
            io(n > 0, "write state");
            value.remove_prefix(static_cast<std::size_t>(n));
        }
        io(fsync(fd) == 0, "sync state");
        close(fd);
        fd = -1;
        io(rename(tmp.c_str(), p.c_str()) == 0, "replace state");
        syncDirectory(p.parent_path());
    } catch (...) {
        if (fd >= 0)
            close(fd);
        unlink(tmp.c_str());
        throw;
    }
}
void durableRemove(const fs::path &p) {
    fs::remove_all(p);
    if (fs::exists(p.parent_path()))
        syncDirectory(p.parent_path());
}
std::string fileHash(const fs::path &p) {
    if (!fs::exists(p))
        return "";
    auto v = readFile(p);
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned len = 0;
    if (EVP_Digest(v.data(), v.size(), digest, &len, EVP_sha256(), nullptr) != 1)
        throw Error({ErrorCode::internal_error, "SHA256 failed"});
    std::ostringstream out;
    for (unsigned i = 0; i < len; ++i)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(digest[i]);
    return out.str();
}
std::string randomId() {
    unsigned char b[16];
    std::size_t n = 0;
    while (n < sizeof b) {
        ssize_t r = getrandom(b + n, sizeof b - n, 0);
        if (r < 0 && errno == EINTR)
            continue;
        io(r > 0, "random ID");
        n += r;
    }
    std::ostringstream out;
    for (auto c : b)
        out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(c);
    return out.str();
}
std::uint64_t bootMilliseconds() {
    timespec t{};
    io(clock_gettime(CLOCK_BOOTTIME, &t) == 0, "boot clock");
    return static_cast<std::uint64_t>(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}
Profile loadProfile(const fs::path &p) {
    struct stat st{};
    io(lstat(p.c_str(), &st) == 0, "stat profile");
    if (!S_ISREG(st.st_mode) || st.st_uid != 0 || (st.st_mode & 0022))
        throw Error({ErrorCode::permission_denied,
                     "system profile must be a root-owned, non-writable regular file"});
    auto profile = decode<Profile>(readFile(p));
    validate(profile.defaults);
    if (profile.state_directory.empty() || profile.active_settings.empty() ||
        profile.settings_user.empty() || profile.control_user.empty())
        throw Error({ErrorCode::invalid_argument, "incomplete system profile"});
    const auto state = fs::path(profile.state_directory).lexically_normal();
    for (const auto &path : profile.reset_paths) {
        const auto target = fs::path(path).lexically_normal();
        if (!target.is_absolute() || target == target.root_path() || target == "/etc" ||
            target == "/var" || target == "/data" || target == "/usr" || target == "/root" ||
            target == state || state.string().starts_with(target.string() + "/"))
            throw Error({ErrorCode::invalid_argument, "unsafe reset path in product profile"});
    }
    return profile;
}
Backend::Backend(Profile profile, Platform &platform, std::function<std::uint64_t()> clock)
    : profile_(std::move(profile)), platform_(platform), clock_(std::move(clock)) {
    fs::create_directories(profile_.state_directory);
    fs::permissions(profile_.state_directory, fs::perms::owner_all);
    const auto path = fs::path(profile_.state_directory) / "state.json";
    if (fs::exists(path))
        state_ = decode<State>(readFile(path));
    else
        state_.configuration = profile_.defaults;
}
void Backend::persist() {
    if (storage_failed_)
        throw Error({ErrorCode::unavailable, "system journal requires recovery"});
    try {
        atomicWrite(fs::path(profile_.state_directory) / "state.json", encode(state_));
    } catch (...) {
        storage_failed_ = true;
        throw;
    }
}
bool Backend::pending() const {
    return state_.pending.transaction.state == "pending" ||
           state_.pending.transaction.state == "committing";
}
void Backend::idle() {
    if (storage_failed_)
        throw Error({ErrorCode::unavailable, "system journal requires recovery"});
    if (pending() || state_.job.state == "accepted" || state_.reset_intent)
        throw Error({ErrorCode::busy, "another system transaction is in progress"});
}
void Backend::checkId(const std::string &id) {
    if (!pending() || id != state_.pending.transaction.id)
        throw Error({ErrorCode::invalid_argument, "network transaction is no longer pending"});
}
void Backend::settleNetwork(bool commit, bool bootstrap) {
    auto next = commit ? state_.pending.transaction.network : state_.pending.previous;
    // Retain the journal until the runtime apply succeeds; failures are retryable.
    platform_.network(next, bootstrap);
    state_.configuration.system.network = next;
    state_.pending.transaction.state = commit ? "committed" : "rolled_back";
    state_.pending.transaction.remaining_ms = 0;
    persist();
}
void Backend::recoverNetwork(bool bootstrap) {
    if (!pending())
        return;
    const bool committed = state_.pending.transaction.state == "committing" &&
                           fileHash(profile_.active_settings) == state_.pending.candidate_hash;
    settleNetwork(committed, bootstrap);
}
void Backend::bootstrap() {
    if (state_.reset_intent) {
        // Intent survives every deletion and is cleared only after all work succeeds.
        for (const auto &path : profile_.reset_paths)
            durableRemove(path);
        state_.configuration = profile_.defaults;
        state_.initialized = true;
        state_.pending = {};
        platform_.network(state_.configuration.system.network, true);
        platform_.sshBootPolicy(state_.configuration.system.ssh_enabled);
        state_.reset_intent = false;
        state_.job.state = "completed";
        persist();
    } else if (state_.initialized) {
        recoverNetwork(true);
        platform_.network(state_.configuration.system.network, true);
        platform_.sshBootPolicy(state_.configuration.system.ssh_enabled);
    }
}
void Backend::start() {
    if (state_.reset_intent) {
        job_deadline_ = clock_() + 5000;
        return;
    }
    if (!state_.initialized) {
        state_.configuration = platform_.discover();
        validate(state_.configuration);
        state_.initialized = true;
        persist();
    }
    recoverNetwork(false);
    // The settings authority reapplies non-network preferences after it loads active.json.
    if (state_.job.state == "accepted") {
        state_.job.state = "interrupted";
        persist();
    }
}
SystemStatus Backend::status() {
    auto result = platform_.status();
    result.configuration = state_.configuration;
    return result;
}
void Backend::apply(const Configuration &config) {
    idle();
    validate(config);
    platform_.validateConfiguration(config);
    if (state_.initialized && config.system.network != state_.configuration.system.network)
        throw Error(
            {ErrorCode::invalid_argument, "network changes require a confirmation transaction"});
    auto previous = state_.configuration;
    try {
        platform_.preferences(config);
        platform_.sshBootPolicy(config.system.ssh_enabled);
        state_.configuration = config;
        state_.initialized = true;
        persist();
    } catch (...) {
        state_.configuration = previous;
        try {
            platform_.preferences(previous);
            platform_.sshBootPolicy(previous.system.ssh_enabled);
            persist();
        } catch (...) {
            throw Error(
                {ErrorCode::apply_failed, "preferences failed and rollback needs recovery"});
        }
        throw;
    }
}
NetworkTransaction Backend::beginNetwork(const NetworkProposal &p) {
    idle();
    auto config = state_.configuration;
    config.system.network = p.network;
    validate(config);
    platform_.validateConfiguration(config);
    const auto validHash = [](const std::string &s) {
        return s.size() == 64 && std::ranges::all_of(s, [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    };
    if (!validHash(p.previous_settings_hash) || !validHash(p.candidate_settings_hash) ||
        p.previous_settings_hash == p.candidate_settings_hash ||
        fileHash(profile_.active_settings) != p.previous_settings_hash)
        throw Error({ErrorCode::invalid_argument, "settings changed before network proposal"});
    Pending next;
    next.transaction = {randomId(), "pending", 120000, p.network,
                        "Confirm within 120 seconds to retain these addresses"};
    next.previous = state_.configuration.system.network;
    next.previous_hash = p.previous_settings_hash;
    next.candidate_hash = p.candidate_settings_hash;
    next.deadline = clock_() + 120000;
    state_.pending = std::move(next);
    persist();
    try {
        platform_.network(p.network, false);
    } catch (...) {
        settleNetwork(false);
        throw;
    }
    return networkTransaction();
}
NetworkTransaction Backend::networkTransaction() {
    tick();
    auto t = state_.pending.transaction;
    t.remaining_ms =
        pending() && state_.pending.deadline > clock_() ? state_.pending.deadline - clock_() : 0;
    return t;
}
void Backend::prepareNetworkCommit(const std::string &id) {
    tick();
    checkId(id);
    if (state_.pending.transaction.state == "committing")
        return;
    if (fileHash(profile_.active_settings) != state_.pending.previous_hash)
        throw Error({ErrorCode::invalid_argument, "settings changed during network proposal"});
    state_.pending.transaction.state = "committing";
    state_.pending.deadline = clock_() + 30000;
    persist();
}
void Backend::finishNetwork(const std::string &id, bool commit) {
    if (id == state_.pending.transaction.id &&
        state_.pending.transaction.state == (commit ? "committed" : "rolled_back"))
        return;
    checkId(id);
    if (commit && (state_.pending.transaction.state != "committing" ||
                   fileHash(profile_.active_settings) != state_.pending.candidate_hash))
        throw Error({ErrorCode::invalid_argument, "candidate settings have not been persisted"});
    // A durable settings commit cannot subsequently be cancelled by a stale caller.
    if (!commit && state_.pending.transaction.state == "committing" &&
        fileHash(profile_.active_settings) == state_.pending.candidate_hash)
        throw Error({ErrorCode::busy, "network settings already committed"});
    settleNetwork(commit);
}
Job Backend::power(PowerAction action) {
    idle();
    if (action != PowerAction::reboot && action != PowerAction::shutdown)
        throw Error({ErrorCode::invalid_argument, "invalid power action"});
    state_.job = {randomId(), action == PowerAction::reboot ? "reboot" : "shutdown", "accepted"};
    persist();
    job_deadline_ = clock_() + 5000;
    return state_.job;
}
Job Backend::resetDevice(bool confirmed) {
    if (!confirmed)
        throw Error(
            {ErrorCode::invalid_argument, "whole-device reset requires explicit confirmation"});
    idle();
    state_.job = {randomId(), "device_reset", "accepted"};
    state_.reset_intent = true;
    persist();
    job_deadline_ = clock_() + 5000;
    return state_.job;
}
void Backend::tick() {
    if (storage_failed_)
        throw Error({ErrorCode::unavailable, "system journal requires recovery"});
    if (pending() && clock_() >= state_.pending.deadline)
        recoverNetwork(false);
    if (job_deadline_ && clock_() >= job_deadline_) {
        job_deadline_ = 0;
        try {
            platform_.power(state_.job.action == "shutdown" ? PowerAction::shutdown
                                                            : PowerAction::reboot);
        } catch (...) {
            state_.job.state = "failed";
            persist();
            throw;
        }
    }
}
} // namespace mnc::os::system
