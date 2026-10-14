// SPDX-License-Identifier: Apache-2.0
#include "mnc/logging/logging.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sys/uio.h>
#include <vector>

#if defined(MNC_LOGGING_HAVE_SYSTEMD)
#define SD_JOURNAL_SUPPRESS_LOCATION
#include <systemd/sd-journal.h>
#endif

namespace mnc::logging {
namespace {

[[maybe_unused]] std::string journal_field(std::string_view name, std::string_view value)
{
	std::string result;
	result.reserve(name.size() + value.size() + 1);
	result.append(name);
	result.push_back('=');
	result.append(value);
	return result;
}

} // namespace

const char *priority_name(Priority priority) noexcept
{
	static constexpr std::array<const char *, 8> names{
		"emergency", "alert", "critical", "error",
		"warning", "notice", "info", "debug"};
	const auto index = static_cast<std::size_t>(priority);
	return index < names.size() ? names[index] : "info";
}

bool parse_priority(std::string_view name, Priority &priority) noexcept
{
	std::string normalized(name);
	std::transform(normalized.begin(), normalized.end(), normalized.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		});
	static constexpr std::array<std::pair<std::string_view, Priority>, 10>
		values{{
			{"emergency", Priority::emergency},
			{"emerg", Priority::emergency},
			{"alert", Priority::alert},
			{"critical", Priority::critical},
			{"crit", Priority::critical},
			{"error", Priority::error},
			{"err", Priority::error},
			{"warning", Priority::warning},
			{"notice", Priority::notice},
			{"info", Priority::info},
		}};
	for (const auto &[candidate, value] : values) {
		if (normalized == candidate) {
			priority = value;
			return true;
		}
	}
	if (normalized == "debug") {
		priority = Priority::debug;
		return true;
	}
	return false;
}

bool valid_field_name(std::string_view name) noexcept
{
	if (name.empty() || name.front() == '_')
		return false;
	return std::all_of(name.begin(), name.end(), [](unsigned char character) {
		return (character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') ||
			character == '_';
	});
}

namespace {
bool reserved(std::string_view name) noexcept
{
	return name == "MESSAGE" || name == "PRIORITY" || name == "MNC_COMPONENT" ||
		name == "MNC_MODULE" || name == "MNC_EVENT" || name == "MNC_LOG_AUDIENCE" ||
		name == "MNC_EVENT_CATEGORY" || name == "MNC_ACTOR" || name == "MNC_ACTION" ||
		name == "MNC_OUTCOME" || name == "MNC_SUBJECT" || name == "MNC_CHANGED_FIELDS" ||
		name.starts_with("CODE_");
}
bool safe_text(std::string_view value, std::size_t maximum) noexcept
{
	return value.size() <= maximum && std::ranges::none_of(value, [](unsigned char c) {
		return c < 0x20 || c == 0x7f;
	});
}
bool identifier(std::string_view value) noexcept
{
	return !value.empty() && value.size() <= 128 &&
		std::ranges::all_of(value, [](unsigned char c) {
			return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
		});
}
std::string changed_json(const std::vector<std::string> &names)
{
	std::string result = "[";
	for (const auto &name : names) {
		if (result.size() > 1) result += ',';
		result += '\"'; result += name; result += '\"';
	}
	return result + ']';
}
} // namespace

bool JournalSink::write(std::span<const Field> fields) noexcept
{
	try {
#if defined(MNC_LOGGING_HAVE_SYSTEMD)
		std::vector<std::string> values;
		values.reserve(fields.size());
		for (const auto &field : fields)
			values.push_back(journal_field(field.name, field.value));
		std::vector<iovec> vectors;
		vectors.reserve(values.size());
		for (auto &value : values)
			vectors.push_back({value.data(), value.size()});
		return sd_journal_sendv(vectors.data(), static_cast<int>(vectors.size())) >= 0;
#else
		(void)fields;
		return false;
#endif
	} catch (...) { return false; }
}

LogBase::LogBase(std::string component, std::string module, std::shared_ptr<Sink> sink)
	: component_(std::move(component)), module_(std::move(module)),
	  sink_(sink ? std::move(sink) : std::make_shared<JournalSink>())
{
	if (component_.empty())
		throw std::invalid_argument("logging component must not be empty");
}

bool LogBase::emit(Priority priority, std::string_view message, std::string_view event,
				  std::span<const Field> fields,
				  const std::source_location *source) const noexcept
{
	try {
		std::vector<Field> record{{"MESSAGE", std::string(message)},
			{"PRIORITY", std::to_string(static_cast<unsigned>(priority))},
			{"MNC_COMPONENT", component_},
			{"MNC_LOG_AUDIENCE", audience() == Audience::user ? "user" : "developer"}};
		if (!module_.empty()) record.push_back({"MNC_MODULE", module_});
		if (!event.empty()) record.push_back({"MNC_EVENT", std::string(event)});
		if (source) {
			record.push_back({"CODE_FILE", source->file_name()});
			record.push_back({"CODE_LINE", std::to_string(source->line())});
			record.push_back({"CODE_FUNC", source->function_name()});
		}
		for (const auto &field : fields) {
			if (!valid_field_name(field.name)) continue;
			// Public metadata is generated by UserLog only. Developer extra fields
			// cannot opt themselves into the public stream or replace core fields.
			if (reserved(field.name) && audience() == Audience::developer) continue;
			record.push_back(field);
		}
		return sink_->write(record);
	} catch (...) { return false; }
}
DeveloperLog::DeveloperLog(std::string component, std::string module, std::shared_ptr<Sink> sink)
	: LogBase(std::move(component), std::move(module), std::move(sink)) {}
bool DeveloperLog::write(Priority priority, std::string_view message, std::string_view event,
						 std::span<const Field> fields,
						 const std::source_location &source) const noexcept
{
	return emit(priority, message, event, fields, &source);
}
UserLog::UserLog(std::string component, std::string module, std::shared_ptr<Sink> sink)
	: LogBase(std::move(component), std::move(module), std::move(sink)) {}
bool valid_user_event(const UserEvent &event) noexcept
{
	constexpr std::array<std::string_view, 7> categories{
		"network", "mqtt", "delivery", "settings", "authentication", "administration", "availability"};
	return identifier(event.code) && identifier(event.action) && identifier(event.outcome) &&
		std::ranges::find(categories, event.category) != categories.end() &&
		!event.message.empty() && safe_text(event.message, 512) &&
		safe_text(event.actor, 256) && safe_text(event.subject, 256) &&
		safe_text(event.request_id, 128) && event.changed_fields.size() <= 64 &&
		std::ranges::all_of(event.changed_fields, [](const auto &name) { return identifier(name); });
}
bool UserLog::write(Priority priority, const UserEvent &event) const noexcept
{
	if (!valid_user_event(event)) return false;
	try {
		const std::array<Field, 7> fields{{
			{"MNC_EVENT_CATEGORY", event.category}, {"MNC_ACTOR", event.actor},
			{"MNC_ACTION", event.action}, {"MNC_OUTCOME", event.outcome},
			{"MNC_SUBJECT", event.subject}, {"MNC_CHANGED_FIELDS", changed_json(event.changed_fields)},
			{"MNC_REQUEST_ID", event.request_id}}};
		return emit(priority, event.message, event.code, fields, nullptr);
	} catch (...) { return false; }
}
} // namespace mnc::logging
