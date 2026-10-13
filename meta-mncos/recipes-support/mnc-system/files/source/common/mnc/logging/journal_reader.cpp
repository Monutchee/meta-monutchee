// SPDX-License-Identifier: Apache-2.0
#include "mnc/logging/journal_reader.hpp"
#include "journal_query.hpp"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

#if defined(MNC_LOGGING_HAVE_SYSTEMD)
#include <systemd/sd-journal.h>
#endif

namespace mnc::logging {
namespace {

std::string json_escape(std::string_view value)
{
	std::ostringstream output;
	for (const unsigned char character : value) {
		switch (character) {
		case '"': output << "\\\""; break;
		case '\\': output << "\\\\"; break;
		case '\b': output << "\\b"; break;
		case '\f': output << "\\f"; break;
		case '\n': output << "\\n"; break;
		case '\r': output << "\\r"; break;
		case '\t': output << "\\t"; break;
		default:
			if (character < 0x20)
				output << "\\u" << std::hex << std::setw(4)
				       << std::setfill('0')
				       << static_cast<unsigned>(character);
			else
				output << static_cast<char>(character);
		}
	}
	return output.str();
}

bool listed(const std::vector<std::string> &values, const std::string &value)
{
	return values.empty() ||
		std::find(values.begin(), values.end(), value) != values.end();
}

// The writer only emits bounded identifier arrays, never arbitrary JSON values.
[[maybe_unused]] std::vector<std::string> changed_fields(std::string_view value)
{
	std::vector<std::string> result;
	if (value == "[]") return result;
	if (value.size() > 64 * 132 || !value.starts_with("[\"") || !value.ends_with("\"]")) return {};
	value.remove_prefix(1); value.remove_suffix(1);
	while (!value.empty()) {
		if (value.front() != '\"') return {};
		value.remove_prefix(1);
		const auto end = value.find('\"');
		if (end == std::string_view::npos || end == 0 || end > 128) return {};
		const auto name = value.substr(0, end);
		if (!std::ranges::all_of(name, [](unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
		})) return {};
		result.emplace_back(name);
		if (result.size() > 64) return {};
		value.remove_prefix(end + 1);
		if (value.empty()) break;
		if (!value.starts_with(",\"")) return {};
		value.remove_prefix(1);
	}
	return result;
}

#if defined(MNC_LOGGING_HAVE_SYSTEMD)
std::string field(sd_journal *journal, const char *name)
{
	const void *data = nullptr;
	std::size_t size = 0;
	if (sd_journal_get_data(journal, name, &data, &size) < 0 ||
	    data == nullptr)
		return {};
	const std::string_view value(static_cast<const char *>(data), size);
	const auto separator = value.find('=');
	if (separator == std::string_view::npos)
		return {};
	return std::string(value.substr(separator + 1));
}

Entry current_entry(sd_journal *journal)
{
	Entry result;
	std::uint64_t timestamp = 0;
	if (sd_journal_get_realtime_usec(journal, &timestamp) >= 0)
		result.timestamp = std::chrono::system_clock::time_point{
			std::chrono::microseconds{timestamp}};
	char *cursor = nullptr;
	if (sd_journal_get_cursor(journal, &cursor) >= 0 && cursor != nullptr) {
		result.cursor.value = cursor;
		std::free(cursor);
	}
	result.message = field(journal, "MESSAGE");
	result.component = field(journal, "MNC_COMPONENT");
	result.module = field(journal, "MNC_MODULE");
	result.event = field(journal, "MNC_EVENT");
	result.audience = field(journal, "MNC_LOG_AUDIENCE");
	if (result.audience.empty()) result.audience = "developer";
	result.category = field(journal, "MNC_EVENT_CATEGORY");
	result.actor = field(journal, "MNC_ACTOR");
	result.action = field(journal, "MNC_ACTION");
	result.outcome = field(journal, "MNC_OUTCOME");
	result.subject = field(journal, "MNC_SUBJECT");
	result.changed_fields = changed_fields(field(journal, "MNC_CHANGED_FIELDS"));
	result.request_id = field(journal, "MNC_REQUEST_ID");
	result.configuration_generation =
		field(journal, "MNC_CONFIGURATION_GENERATION");
	/*
	 * PID 1 lifecycle messages carry the affected service in UNIT while
	 * _SYSTEMD_UNIT identifies init.scope. Service stdout/stderr normally
	 * has only _SYSTEMD_UNIT, so prefer UNIT when it is present.
	 */
	result.unit = field(journal, "UNIT");
	if (result.unit.empty())
		result.unit = field(journal, "_SYSTEMD_UNIT");
	result.executable = field(journal, "_EXE");
	result.source_file = field(journal, "CODE_FILE");
	result.source_line = field(journal, "CODE_LINE");
	result.source_function = field(journal, "CODE_FUNC");

	const auto priority = field(journal, "PRIORITY");
	unsigned numeric = static_cast<unsigned>(Priority::info);
	const auto parsed = std::from_chars(
		priority.data(), priority.data() + priority.size(), numeric);
	if (parsed.ec == std::errc{} &&
	    numeric <= static_cast<unsigned>(Priority::debug))
		result.priority = static_cast<Priority>(numeric);
	return result;
}
#endif

} // namespace

namespace detail {

bool matches_query(const Entry &entry, const Query &query) noexcept
{
	if (query.audience && entry.audience != *query.audience) return false;
	if (query.category && entry.category != *query.category) return false;
	if (query.event && entry.event != *query.event) return false;
	if (query.since && entry.timestamp < *query.since) return false;
	if (query.until && entry.timestamp > *query.until) return false;
	if (query.component && entry.component != *query.component)
		return false;
	if (query.module && entry.module != *query.module)
		return false;
	if (query.maximum_priority &&
	    static_cast<unsigned>(entry.priority) >
		    static_cast<unsigned>(*query.maximum_priority))
		return false;
	const bool component_match =
		listed(query.components, entry.component);
	const bool unit_match = listed(query.units, entry.unit);
	if (!query.components.empty() && !query.units.empty())
		return component_match || unit_match;
	return component_match && unit_match;
}

std::vector<Entry> bounded_page(std::span<const Entry> entries,
				const Query &query)
{
	std::vector<Entry> result;
	if (query.limit == 0)
		return result;
	result.reserve(std::min(query.limit, entries.size()));

	bool after_cursor = !query.after || !*query.after;
	if (query.strict_cursor && !after_cursor && std::ranges::none_of(entries, [&](const Entry &entry) {
		return entry.cursor.value == query.after->value;
	})) throw JournalError(JournalErrorCode::cursor_expired, "journal cursor is no longer valid");
	for (std::size_t offset = 0; offset < entries.size(); ++offset) {
		const auto &entry = entries[query.newest_first ? entries.size() - 1 - offset : offset];
		if (!after_cursor) {
			if (entry.cursor.value == query.after->value) after_cursor = true;
			continue;
		}
		if (!matches_query(entry, query)) continue;
		result.push_back(entry);
		if (result.size() == query.limit) break;
	}
	return result;
}

} // namespace detail

class JournalReader::Implementation {
public:
#if defined(MNC_LOGGING_HAVE_SYSTEMD)
	Implementation()
	{
		if (sd_journal_open(&journal_, SD_JOURNAL_LOCAL_ONLY) < 0)
			journal_ = nullptr;
	}
	~Implementation()
	{
		if (journal_ != nullptr)
			sd_journal_close(journal_);
	}

	sd_journal *journal_ = nullptr;
#else
	Implementation() = default;
#endif
};

JournalReader::JournalReader()
	: implementation_(std::make_unique<Implementation>())
{
}

JournalReader::~JournalReader() = default;
JournalReader::JournalReader(JournalReader &&) noexcept = default;
JournalReader &JournalReader::operator=(JournalReader &&) noexcept = default;

bool JournalReader::available() const noexcept
{
#if defined(MNC_LOGGING_HAVE_SYSTEMD)
	return implementation_ && implementation_->journal_ != nullptr;
#else
	return false;
#endif
}

std::vector<Entry> JournalReader::read(const Query &query)
{
	if (query.limit == 0) return {};
#if !defined(MNC_LOGGING_HAVE_SYSTEMD)
	(void)query;
	throw JournalError(JournalErrorCode::unavailable, "journald support is unavailable in this build");
#else
	if (!available())
		throw JournalError(JournalErrorCode::unavailable, "failed to open the system journal");
	auto *journal = implementation_->journal_;
	sd_journal_flush_matches(journal);
	// Native audience match bounds traversal of the public stream; all further
	// policy filters also apply to each decoded entry.
	if (query.audience && *query.audience == "user") {
		constexpr char match[] = "MNC_LOG_AUDIENCE=user";
		if (sd_journal_add_match(journal, match, sizeof(match) - 1) < 0)
			throw JournalError(JournalErrorCode::read_failed, "failed to filter the system journal");
	}
	std::vector<Entry> result;
	result.reserve(query.limit);
	auto step = [&] { return query.newest_first ? sd_journal_previous(journal) : sd_journal_next(journal); };
	auto read_error = [] {
		throw JournalError(JournalErrorCode::read_failed, "failed to read the system journal");
	};
	if (query.after && *query.after) {
		if (sd_journal_seek_cursor(journal, query.after->value.c_str()) < 0)
			throw JournalError(JournalErrorCode::cursor_expired, "journal cursor is no longer valid");
		const auto positioned = sd_journal_next(journal);
		if (positioned < 0) read_error();
		if (positioned == 0 || sd_journal_test_cursor(journal, query.after->value.c_str()) != 1) {
			if (query.strict_cursor)
				throw JournalError(JournalErrorCode::cursor_expired, "journal cursor is no longer valid");
			// Legacy continuation tolerates vacuumed cursors at the nearest position.
			if (positioned > 0 && !query.newest_first) {
				auto entry = current_entry(journal);
				if (detail::matches_query(entry, query)) result.push_back(std::move(entry));
			}
		}
	} else if (query.newest_first || !query.since) {
		if (sd_journal_seek_tail(journal) < 0) read_error();
		// Legacy unbounded reads retrieve a recent page and return it oldest first.
		if (!query.newest_first) {
			int state;
			while (result.size() < query.limit && (state = sd_journal_previous(journal)) > 0) {
				auto entry = current_entry(journal);
				if (detail::matches_query(entry, query)) result.push_back(std::move(entry));
			}
			if (result.size() < query.limit && state < 0) read_error();
			std::reverse(result.begin(), result.end());
			return result;
		}
	} else {
		const auto usec = std::chrono::duration_cast<std::chrono::microseconds>(query.since->time_since_epoch());
		if (sd_journal_seek_realtime_usec(journal, std::max<std::int64_t>(0, usec.count())) < 0) read_error();
	}
	int state = 0;
	while (result.size() < query.limit && (state = step()) > 0) {
		auto entry = current_entry(journal);
		if (detail::matches_query(entry, query)) result.push_back(std::move(entry));
	}
	if (state < 0) read_error();
	return result;
#endif
}

void JournalReader::follow(const Query &query, const FollowHandler &handler,
			   std::chrono::milliseconds wake_interval,
			   const ContinueHandler &keep_running)
{
#if !defined(MNC_LOGGING_HAVE_SYSTEMD)
	(void)query;
	(void)handler;
	(void)wake_interval;
	(void)keep_running;
	throw JournalError(JournalErrorCode::unavailable, "journald support is unavailable in this build");
#else
	if (!available())
		throw std::runtime_error("failed to open the system journal");
	if (query.newest_first)
		throw std::invalid_argument("journal follow requires oldest-first order");
	Query continuation = query;
	auto entries = read(continuation);
	for (const auto &entry : entries) {
		if (!handler(entry))
			return;
		continuation.after = entry.cursor;
		continuation.since =
			entry.timestamp;
	}
	if (!continuation.after) {
		if (sd_journal_seek_tail(implementation_->journal_) < 0)
			throw std::runtime_error("failed to follow the system journal");
	}
	for (;;) {
		if (keep_running && !keep_running())
			return;
		/*
		 * Drain an initial --since backlog in bounded pages before waiting.
		 * Once the cursor reaches the tail, read() returns an empty page and
		 * the journal wait below becomes the only blocking operation.
		 */
		continuation.limit = query.limit;
		entries = read(continuation);
		if (!entries.empty()) {
			for (const auto &entry : entries) {
				if (!handler(entry))
					return;
				continuation.after = entry.cursor;
				continuation.since =
					entry.timestamp;
			}
			continue;
		}

		const auto timeout = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::microseconds>(
				wake_interval)
				.count());
		const int state = sd_journal_wait(
			implementation_->journal_, timeout);
		if (state < 0)
			throw std::runtime_error("failed while following the journal");
		if (keep_running && !keep_running())
			return;
		if (state == SD_JOURNAL_INVALIDATE && continuation.after) {
			if (sd_journal_seek_cursor(
				    implementation_->journal_,
				    continuation.after->value.c_str()) < 0)
				continuation.after.reset();
		}
	}
#endif
}

std::string entry_to_json(const Entry &entry)
{
	const auto usec = std::chrono::duration_cast<std::chrono::microseconds>(
		entry.timestamp.time_since_epoch());
	std::ostringstream output;
	output << "{\"timestamp_usec\":" << usec.count()
	       << ",\"cursor\":\"" << json_escape(entry.cursor.value)
	       << "\",\"priority\":\"" << priority_name(entry.priority)
	       << "\",\"message\":\"" << json_escape(entry.message)
	       << "\",\"component\":\"" << json_escape(entry.component)
	       << "\",\"module\":\"" << json_escape(entry.module)
	       << "\",\"event\":\"" << json_escape(entry.event)
	       << "\",\"request_id\":\"" << json_escape(entry.request_id)
	       << "\",\"configuration_generation\":\""
	       << json_escape(entry.configuration_generation)
	       << "\",\"unit\":\"" << json_escape(entry.unit)
	       << "\",\"executable\":\"" << json_escape(entry.executable)
	       << "\",\"source_file\":\"" << json_escape(entry.source_file)
	       << "\",\"source_line\":\"" << json_escape(entry.source_line)
	       << "\",\"source_function\":\""
	       << json_escape(entry.source_function)
		   << "\",\"audience\":\"" << json_escape(entry.audience)
		   << "\",\"category\":\"" << json_escape(entry.category)
		   << "\",\"actor\":\"" << json_escape(entry.actor)
		   << "\",\"action\":\"" << json_escape(entry.action)
		   << "\",\"outcome\":\"" << json_escape(entry.outcome)
		   << "\",\"subject\":\"" << json_escape(entry.subject)
		   << "\",\"changed_fields\":[";
	for (std::size_t index = 0; index < entry.changed_fields.size(); ++index) {
		if (index) output << ',';
		output << '\"' << json_escape(entry.changed_fields[index]) << '\"';
	}
	output << "]}";
	return output.str();
}

} // namespace mnc::logging
