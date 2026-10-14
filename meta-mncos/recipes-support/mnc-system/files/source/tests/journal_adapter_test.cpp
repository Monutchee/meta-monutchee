#include "mnc/logging/journal_reader.hpp"
#include <systemd/sd-journal.h>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
static int position = 0;
static bool cursor_valid = true;
static std::string audience_match;
extern "C" int __wrap_sd_journal_open(sd_journal **journal, int) { *journal = reinterpret_cast<sd_journal *>(1); return 0; }
extern "C" void __wrap_sd_journal_close(sd_journal *) {}
extern "C" void __wrap_sd_journal_flush_matches(sd_journal *) { audience_match.clear(); }
extern "C" int __wrap_sd_journal_add_match(sd_journal *, const void *data, size_t size) { audience_match.assign(static_cast<const char *>(data), size); return 0; }
extern "C" int __wrap_sd_journal_seek_tail(sd_journal *) { position = 2; return 0; }
extern "C" int __wrap_sd_journal_seek_cursor(sd_journal *, const char *) { position = 0; return 0; }
extern "C" int __wrap_sd_journal_next(sd_journal *) { ++position; return position <= 2; }
extern "C" int __wrap_sd_journal_previous(sd_journal *) { --position; return position >= 0; }
extern "C" int __wrap_sd_journal_test_cursor(sd_journal *, const char *) { return cursor_valid; }
extern "C" int __wrap_sd_journal_get_cursor(sd_journal *, char **cursor) { *cursor = strdup(std::to_string(position).c_str()); return 0; }
extern "C" int __wrap_sd_journal_get_realtime_usec(sd_journal *, uint64_t *value) { *value = 10; return 0; }
extern "C" int __wrap_sd_journal_get_data(sd_journal *, const char *key, const void **data, size_t *size) {
	static std::string field;
	std::string value;
	if (std::strcmp(key, "UNIT") == 0) value = "msap1-web-backend.service";
	else if (std::strcmp(key, "_SYSTEMD_UNIT") == 0) value = "untrusted.service";
	else if (std::strcmp(key, "MNC_LOG_AUDIENCE") == 0) value = "user";
	else if (std::strcmp(key, "MNC_EVENT") == 0) value = "authentication_login_succeeded";
	else return -ENOENT;
	field = std::string(key) + "=" + value; *data = field.data(); *size = field.size(); return 0;
}
int main() {
	using namespace mnc::logging;
	JournalReader reader; Query query; query.audience = "user"; query.newest_first = true; query.strict_cursor = true; query.limit = 1;
	auto entries = reader.read(query);
	assert(audience_match == "MNC_LOG_AUDIENCE=user");
	assert(entries.size() == 1 && entries[0].unit == "msap1-web-backend.service");
	assert(entries[0].systemd_unit == "untrusted.service");
	query.after = Cursor{"cursor"}; entries = reader.read(query);
	assert(entries.size() == 1 && entries[0].cursor.value == "0"); // excludes positioned cursor
	cursor_valid = false; bool expired = false;
	try { reader.read(query); } catch (const JournalError &error) { expired = error.code() == JournalErrorCode::cursor_expired; }
	assert(expired);
}
