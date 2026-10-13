// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <memory>
#include <source_location>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mnc::logging {

enum class Priority : std::uint8_t {
	emergency = 0,
	alert = 1,
	critical = 2,
	error = 3,
	warning = 4,
	notice = 5,
	info = 6,
	debug = 7,
};
enum class Audience { developer, user };
struct Field {
	std::string name;
	std::string value;
};
const char *priority_name(Priority priority) noexcept;
bool parse_priority(std::string_view name, Priority &priority) noexcept;
bool valid_field_name(std::string_view name) noexcept;

/** Injectable transport. Implementations must be thread-safe and never throw. */
class Sink {
public:
	virtual ~Sink() = default;
	virtual bool write(std::span<const Field> fields) noexcept = 0;
};
class JournalSink final : public Sink {
public:
	bool write(std::span<const Field> fields) noexcept override;
};

/** Template-method common record serialization, with an injected sink strategy. */
class LogBase {
public:
	virtual ~LogBase() = default;
	const std::string &component() const noexcept { return component_; }
	const std::string &module() const noexcept { return module_; }
	virtual Audience audience() const noexcept = 0;
protected:
	explicit LogBase(std::string component, std::string module = {},
					 std::shared_ptr<Sink> sink = {});
	bool emit(Priority priority, std::string_view message, std::string_view event,
			  std::span<const Field> fields,
			  const std::source_location *source) const noexcept;
private:
	std::string component_;
	std::string module_;
	std::shared_ptr<Sink> sink_;
};
class DeveloperLog final : public LogBase {
public:
	explicit DeveloperLog(std::string component, std::string module = {},
						  std::shared_ptr<Sink> sink = {});
	Audience audience() const noexcept override { return Audience::developer; }
	bool write(Priority priority, std::string_view message,
			   std::string_view event = {}, std::span<const Field> fields = {},
			   const std::source_location &source =
				   std::source_location::current()) const noexcept;
};
// Existing callers retain developer logging and source metadata.
using Logger = DeveloperLog;

/** Safe public record. Identifiers/field names only: never credentials or values.
 * Message <=512 bytes; identifiers <=128 (actor/subject <=256), <=64 changed
 * field names. Control characters and invalid metadata are rejected, not cut.
 */
struct UserEvent {
	std::string code;
	std::string category;
	std::string message;
	std::string actor;
	std::string action;
	std::string outcome;
	std::string subject;
	std::vector<std::string> changed_fields;
	std::string request_id;
};
bool valid_user_event(const UserEvent &event) noexcept;
class UserLog final : public LogBase {
public:
	explicit UserLog(std::string component, std::string module = {},
					 std::shared_ptr<Sink> sink = {});
	Audience audience() const noexcept override { return Audience::user; }
	bool write(Priority priority, const UserEvent &event) const noexcept;
};
} // namespace mnc::logging
