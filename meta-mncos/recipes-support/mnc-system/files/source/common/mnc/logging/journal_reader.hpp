// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "mnc/logging/logging.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <vector>

namespace mnc::logging {

struct Cursor {
	std::string value;

	explicit operator bool() const noexcept { return !value.empty(); }
};

enum class JournalErrorCode { unavailable, cursor_expired, read_failed };
class JournalError : public std::runtime_error {
public:
	JournalError(JournalErrorCode code, const char *message)
		: std::runtime_error(message), code_(code) {}
	JournalErrorCode code() const noexcept { return code_; }
private:
	JournalErrorCode code_;
};

struct Entry {
	std::chrono::system_clock::time_point timestamp;
	Cursor cursor;
	Priority priority = Priority::info;
	std::string message;
	std::string component;
	std::string module;
	std::string event;
	std::string audience = "developer"; // Legacy unclassified entries stay private.
	std::string category;
	std::string actor;
	std::string action;
	std::string outcome;
	std::string subject;
	std::vector<std::string> changed_fields;
	std::string request_id;
	std::string configuration_generation;
	std::string unit;
	std::string systemd_unit; // Trusted journald provenance; never caller-supplied UNIT.
	std::string executable;
	std::string source_file;
	std::string source_line;
	std::string source_function;
};

struct Query {
	std::optional<std::string> component;
	std::optional<std::string> module;
	std::optional<Priority> maximum_priority;
	std::optional<std::chrono::system_clock::time_point> since;
	std::optional<Cursor> after;
	std::vector<std::string> components;
	std::vector<std::string> units;
	std::size_t limit = 100;
	std::optional<std::string> audience;
	std::optional<std::string> category;
	std::optional<std::string> event;
	std::optional<std::chrono::system_clock::time_point> until;
	bool newest_first = false;
	bool strict_cursor = false; // Expired cursors fail instead of guessing a position.
};

/*
 * Reader over the system journal. Cursor-based continuation is deliberately
 * part of the public API so a future MCP server can expose bounded, stable
 * pagination without relying on timestamp equality.
 */
class JournalReader {
public:
	using FollowHandler = std::function<bool(const Entry &)>;
	using ContinueHandler = std::function<bool()>;

	JournalReader();
	~JournalReader();
	JournalReader(JournalReader &&) noexcept;
	JournalReader &operator=(JournalReader &&) noexcept;
	JournalReader(const JournalReader &) = delete;
	JournalReader &operator=(const JournalReader &) = delete;

	bool available() const noexcept;
	std::vector<Entry> read(const Query &query);
	void follow(const Query &query, const FollowHandler &handler,
		    std::chrono::milliseconds wake_interval =
			    std::chrono::milliseconds{250},
		    const ContinueHandler &keep_running = {});

private:
	class Implementation;
	std::unique_ptr<Implementation> implementation_;
};

std::string entry_to_json(const Entry &entry);

} // namespace mnc::logging
