// SPDX-License-Identifier: Apache-2.0
#include "mnc/logging/logging.hpp"
#include "mnc/logging/journal_query.hpp"
#include <cassert>
#include <mutex>
#include <thread>
#include <array>
using namespace mnc::logging;
struct RecordingSink : Sink {
 std::mutex mutex;
 std::vector<std::vector<Field>> records;
 bool fail = false;
 bool write(std::span<const Field> fields) noexcept override {
  std::lock_guard lock(mutex);
  if (fail) return false;
  records.emplace_back(fields.begin(), fields.end()); return true;
 }
};
std::string value(const std::vector<Field>& record, std::string_view key) {
 std::string found; unsigned count=0;
 for (const auto& f:record) if(f.name==key) { found=f.value; ++count; }
 assert(count<=1); return found;
}
int main() {
 auto sink=std::make_shared<RecordingSink>();
 Logger developer("test", "module", sink); UserLog user("test", "module", sink);
 std::array<Field,5> spoof{{{"MNC_LOG_AUDIENCE","user"},{"MNC_EVENT","spoof"},{"MESSAGE","secret"},{"CODE_FILE","fake"},{"MNC_ACTOR","admin"}}};
 assert(developer.write(Priority::debug,"real","developer_event",spoof));
 assert(value(sink->records.back(),"MNC_LOG_AUDIENCE")=="developer");
 assert(value(sink->records.back(),"MESSAGE")=="real");
 assert(value(sink->records.back(),"MNC_EVENT")=="developer_event");
 assert(value(sink->records.back(),"MNC_ACTOR").empty());
 assert(value(sink->records.back(),"CODE_FILE")!="fake");
 UserEvent event{.code="settings_saved",.category="settings",.message="Settings saved",.actor="uid:1000",.action="save",.outcome="completed",.subject="settings",.changed_fields={"password","network.address"},.request_id="r1"};
 assert(user.write(Priority::info,event));
 assert(value(sink->records.back(),"MNC_LOG_AUDIENCE")=="user");
 assert(value(sink->records.back(),"CODE_FILE").empty());
 assert(value(sink->records.back(),"MNC_CHANGED_FIELDS")=="[\"password\",\"network.address\"]");
 auto invalid=event; invalid.message="bad\nvalue"; assert(!user.write(Priority::info,invalid));
 invalid=event; invalid.actor=std::string(257,'a'); assert(!user.write(Priority::info,invalid));
 invalid=event; invalid.changed_fields.resize(65,"safe"); assert(!user.write(Priority::info,invalid));
 invalid=event; invalid.changed_fields={"password=value"}; assert(!user.write(Priority::info,invalid));
 invalid=event; invalid.category="internal"; assert(!user.write(Priority::info,invalid));
 sink->fail=true; assert(!user.write(Priority::info,event)); sink->fail=false;
 const auto before=sink->records.size();
 std::vector<std::thread> threads;
 for(unsigned i=0;i<8;++i) threads.emplace_back([&]{for(unsigned j=0;j<100;++j) assert(user.write(Priority::info,event));});
 for(auto& t:threads) t.join();
 assert(sink->records.size()==before+800);
 std::vector<Entry> entries(5);
 for(unsigned i=0;i<entries.size();++i){entries[i].cursor.value=std::to_string(i);entries[i].audience="user"; entries[i].category="settings";entries[i].timestamp=std::chrono::system_clock::time_point{std::chrono::seconds{10}};}
 entries[2].audience="developer";
 Query query;query.audience="user";query.newest_first=true;query.strict_cursor=true;query.limit=2;
 auto page=detail::bounded_page(entries,query);assert(page.size()==2&&page[0].cursor.value=="4"&&page[1].cursor.value=="3");
 query.after=page.back().cursor;page=detail::bounded_page(entries,query);assert(page.size()==2&&page[0].cursor.value=="1"&&page[1].cursor.value=="0");
 query.after=Cursor{"missing"};bool expired=false;try{detail::bounded_page(entries,query);}catch(const JournalError& e){expired=e.code()==JournalErrorCode::cursor_expired;}assert(expired);
 query.after.reset();query.since=entries[0].timestamp;query.until=entries[0].timestamp;assert(detail::bounded_page(entries,query).size()==2);
 query.until=*query.until-std::chrono::seconds{1};assert(detail::bounded_page(entries,query).empty());
}
