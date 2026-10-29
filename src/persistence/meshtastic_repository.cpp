#include "persistence/meshtastic_repository.hpp"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <ctime>
#include <stdexcept>

namespace meshtastic {
namespace {
class Statement {
public:
    Statement(sqlite3 *db, const std::string &sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &value_, nullptr) != SQLITE_OK) fail();
    }
    ~Statement() { sqlite3_finalize(value_); }
    void bind(int at, std::int64_t value) { if (sqlite3_bind_int64(value_, at, value) != SQLITE_OK) fail(); }
    void bind(int at, double value) { if (sqlite3_bind_double(value_, at, value) != SQLITE_OK) fail(); }
    void bind(int at, const std::string &value) { if (sqlite3_bind_text(value_, at, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) fail(); }
    void bytes(int at, const std::string &value) { if (sqlite3_bind_blob(value_, at, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT) != SQLITE_OK) fail(); }
    bool next() { const int result = sqlite3_step(value_); if (result == SQLITE_ROW) return true; if (result != SQLITE_DONE) fail(); return false; }
    void reset() { if(sqlite3_reset(value_)!=SQLITE_OK)fail();sqlite3_clear_bindings(value_); }
    std::int64_t integer(int at) const { return sqlite3_column_int64(value_, at); }
    double real(int at) const { return sqlite3_column_double(value_, at); }
    std::string string(int at) const {
        const auto *data = static_cast<const char *>(sqlite3_column_blob(value_, at));
        const int size = sqlite3_column_bytes(value_, at);
        return data ? std::string(data, static_cast<std::size_t>(size)) : std::string{};
    }
private:
    [[noreturn]] void fail() { throw std::runtime_error(sqlite3_errmsg(db_)); }
    sqlite3 *db_;
    sqlite3_stmt *value_ = nullptr;
};
void execute(sqlite3 *db, const char *sql) {
    char *error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db); sqlite3_free(error); throw std::runtime_error(message);
    }
}
std::int64_t bounded(std::size_t limit) { return static_cast<std::int64_t>(std::clamp<std::size_t>(limit, 1, 10000)); }
const char *message_columns = "id,sender,destination,packet,channel,received_at,rssi,snr,text,outgoing,status,detail,attempts,is_read";
Message read_message(const Statement &row) {
    Message value;
    value.local_id = row.integer(0); value.from = static_cast<std::uint32_t>(row.integer(1));
    value.to = static_cast<std::uint32_t>(row.integer(2)); value.id = static_cast<std::uint32_t>(row.integer(3));
    value.channel = static_cast<std::uint32_t>(row.integer(4)); value.received_at = static_cast<std::uint32_t>(row.integer(5));
    value.rssi = static_cast<std::int32_t>(row.integer(6)); value.snr = static_cast<float>(row.real(7));
    value.text = row.string(8); value.outgoing = row.integer(9) != 0; value.status = static_cast<MessageStatus>(row.integer(10));
    value.status_detail = row.string(11); value.attempts = static_cast<std::uint32_t>(row.integer(12)); value.read = row.integer(13) != 0;
    return value;
}
NodeInfo node_info(const Node &node) {
    NodeInfo value; value.set_num(node.number); value.set_last_heard(node.last_heard); value.set_snr(node.snr); value.set_via_mqtt(node.via_mqtt);
    auto *user = value.mutable_user(); user->set_id(node.id); user->set_long_name(node.long_name); user->set_short_name(node.short_name); user->set_role(static_cast<Config::DeviceConfig::Role>(node.role));
    if (node.latitude) value.mutable_position()->set_latitude_i(static_cast<std::int32_t>(*node.latitude * 1e7));
    if (node.longitude) value.mutable_position()->set_longitude_i(static_cast<std::int32_t>(*node.longitude * 1e7));
    if (node.altitude) value.mutable_position()->set_altitude(*node.altitude);
    if (node.battery_level) value.mutable_device_metrics()->set_battery_level(*node.battery_level);
    if (node.voltage) value.mutable_device_metrics()->set_voltage(*node.voltage);
    if (node.uptime_seconds) value.mutable_device_metrics()->set_uptime_seconds(*node.uptime_seconds);
    if (node.hops_away) value.set_hops_away(*node.hops_away);
    return value;
}
Node read_node(const std::string &bytes) {
    FromRadio radio; if(!radio.mutable_node_info()->ParseFromString(bytes))throw std::runtime_error("Corrupt node record"); RadioUpdate update; std::string error;
    const auto raw = radio.SerializeAsString();
    if (decode_from_radio(std::vector<std::uint8_t>(raw.begin(), raw.end()), update, error) && update.node) return *update.node;
    throw std::runtime_error("Corrupt node record");
}
DeliveryRecord read_delivery(const Statement &row) {
    DeliveryRecord value; value.packet_id = static_cast<std::uint32_t>(row.integer(0)); value.status = static_cast<MessageStatus>(row.integer(1));
    value.detail = row.string(2); value.sent_at = static_cast<std::uint32_t>(row.integer(3)); value.attempts = static_cast<std::uint32_t>(row.integer(4)); value.portnum = static_cast<int>(row.integer(5)); return value;
}
std::string fingerprint(const std::string &bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char value : bytes) { hash ^= value; hash *= 1099511628211ULL; }
    return std::to_string(hash);
}
}

Repository::Repository(const std::filesystem::path &path) {
    if (path != ":memory:" && path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    if (sqlite3_open_v2(path.string().c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr) != SQLITE_OK) {
        const std::string error = db_ ? sqlite3_errmsg(db_) : "unable to open database"; if (db_) sqlite3_close(db_); db_ = nullptr; throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(db_, 3000);
    try {
        if(path!=":memory:")std::filesystem::permissions(path,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,std::filesystem::perm_options::replace);
        // Channel PSKs and admin session keys are stored here. DELETE
        // journaling avoids readable WAL/SHM sidecars whose permissions would
        // otherwise depend on the process umask.
        execute(db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=DELETE;"); migrate();
    } catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Repository::~Repository() { if (db_) sqlite3_close(db_); }
std::filesystem::path Repository::default_path() {
    if (const auto *value = std::getenv("LILYGO_MESHTASTIC_DB_PATH"); value && *value) return value;
    if (const auto *value = std::getenv("LILYGO_MESHTASTIC_DATA_DIR"); value && *value) return std::filesystem::path(value) / "meshtastic.sqlite3";
    if (const auto *value = std::getenv("XDG_DATA_HOME"); value && *value) return std::filesystem::path(value) / "lilygo-ui-meshtastic/meshtastic.sqlite3";
    if (const auto *value = std::getenv("HOME"); value && *value) return std::filesystem::path(value) / ".local/share/lilygo-ui-meshtastic/meshtastic.sqlite3";
    return std::filesystem::temp_directory_path() / "lilygo-ui-meshtastic.sqlite3";
}
void Repository::migrate() {
    int current = 0;
    { Statement version(db_, "PRAGMA user_version"); if (version.next()) current = static_cast<int>(version.integer(0)); }
    if (current > 4) throw std::runtime_error("database schema is newer than this application");
    execute(db_, "BEGIN IMMEDIATE");
    try {
        if (current < 1) execute(db_, R"SQL(
CREATE TABLE sessions(session TEXT PRIMARY KEY, endpoint TEXT NOT NULL, radio INTEGER NOT NULL, used INTEGER NOT NULL);
CREATE INDEX sessions_endpoint ON sessions(endpoint,used DESC);
CREATE TABLE nodes(session TEXT NOT NULL,number INTEGER NOT NULL,data BLOB NOT NULL,PRIMARY KEY(session,number));
CREATE TABLE channels(session TEXT NOT NULL,number INTEGER NOT NULL,data BLOB NOT NULL,PRIMARY KEY(session,number));
CREATE TABLE configurations(session TEXT NOT NULL,kind INTEGER NOT NULL,category INTEGER NOT NULL,data BLOB NOT NULL,PRIMARY KEY(session,kind,category));
CREATE TABLE attributes(session TEXT NOT NULL,name TEXT NOT NULL,data BLOB NOT NULL,PRIMARY KEY(session,name));
CREATE TABLE messages(id INTEGER PRIMARY KEY AUTOINCREMENT,session TEXT NOT NULL,sender INTEGER NOT NULL,destination INTEGER NOT NULL,packet INTEGER NOT NULL,channel INTEGER NOT NULL,received_at INTEGER NOT NULL,rssi INTEGER NOT NULL,snr REAL NOT NULL,text BLOB NOT NULL,outgoing INTEGER NOT NULL,status INTEGER NOT NULL,detail TEXT NOT NULL,attempts INTEGER NOT NULL,is_read INTEGER NOT NULL,direct INTEGER NOT NULL,peer INTEGER NOT NULL);
CREATE UNIQUE INDEX message_dedup ON messages(session,sender,packet,outgoing) WHERE packet<>0;
CREATE INDEX conversation_history ON messages(session,direct,peer,channel,id DESC);
CREATE TABLE outbound(session TEXT NOT NULL,packet INTEGER NOT NULL,status INTEGER NOT NULL,detail TEXT NOT NULL,sent_at INTEGER NOT NULL,attempts INTEGER NOT NULL,port INTEGER NOT NULL,data BLOB NOT NULL,PRIMARY KEY(session,packet));
PRAGMA user_version=1;
)SQL");
        if (current < 2) execute(db_, R"SQL(
CREATE TABLE events(id INTEGER PRIMARY KEY AUTOINCREMENT,session TEXT NOT NULL,kind INTEGER NOT NULL,node INTEGER NOT NULL,peer INTEGER NOT NULL,packet INTEGER NOT NULL,request INTEGER NOT NULL,received_at INTEGER NOT NULL,outgoing INTEGER NOT NULL,data BLOB NOT NULL,identity TEXT NOT NULL,UNIQUE(session,kind,identity));
CREATE INDEX event_history ON events(session,kind,node,id DESC);
PRAGMA user_version=2;
)SQL");
        if(current<3)execute(db_,"CREATE INDEX IF NOT EXISTS delivery_lookup ON messages(session,packet,outgoing);CREATE INDEX IF NOT EXISTS unread_lookup ON messages(session,direct,peer,channel,is_read,outgoing);PRAGMA user_version=3;");
        if(current<4)execute(db_,R"SQL(
DROP INDEX IF EXISTS message_dedup;
ALTER TABLE messages ADD COLUMN observed_at INTEGER NOT NULL DEFAULT 0;
CREATE INDEX message_dedup_window ON messages(session,sender,packet,outgoing,destination,channel,observed_at);
CREATE TABLE events_v4(id INTEGER PRIMARY KEY AUTOINCREMENT,session TEXT NOT NULL,kind INTEGER NOT NULL,node INTEGER NOT NULL,peer INTEGER NOT NULL,packet INTEGER NOT NULL,request INTEGER NOT NULL,received_at INTEGER NOT NULL,outgoing INTEGER NOT NULL,data BLOB NOT NULL,identity TEXT NOT NULL,observed_at INTEGER NOT NULL DEFAULT 0);
INSERT INTO events_v4(id,session,kind,node,peer,packet,request,received_at,outgoing,data,identity)
  SELECT id,session,kind,node,peer,packet,request,received_at,outgoing,data,identity FROM events;
DROP TABLE events;
ALTER TABLE events_v4 RENAME TO events;
CREATE INDEX event_history ON events(session,kind,node,id DESC);
CREATE INDEX event_dedup_window ON events(session,kind,node,packet,outgoing,peer,request,observed_at);
PRAGMA user_version=4;
)SQL");
        execute(db_, "COMMIT");
    } catch (...) { execute(db_, "ROLLBACK"); throw; }
}
std::string Repository::activate(const std::string &endpoint, std::uint32_t radio) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!radio) { Statement query(db_, "SELECT radio FROM sessions WHERE endpoint=? ORDER BY used DESC,rowid DESC LIMIT 1"); query.bind(1,endpoint); if (query.next()) radio = static_cast<std::uint32_t>(query.integer(0)); }
    const auto key = endpoint + "#" + std::to_string(radio);
    Statement write(db_, "INSERT INTO sessions(session,endpoint,radio,used) VALUES(?,?,?,?) ON CONFLICT(session) DO UPDATE SET used=excluded.used");
    write.bind(1,key); write.bind(2,endpoint); write.bind(3,static_cast<std::int64_t>(radio)); write.bind(4,std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); write.next(); return key;
}
RepositoryState Repository::load_state(const std::string &session) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_); RepositoryState state;
    Statement identity(db_, "SELECT radio FROM sessions WHERE session=?"); identity.bind(1,session); if (identity.next()) state.radio_number = static_cast<std::uint32_t>(identity.integer(0));
    Statement nodes(db_, "SELECT data FROM nodes WHERE session=? ORDER BY number"); nodes.bind(1,session); while (nodes.next()) state.nodes.push_back(read_node(nodes.string(0)));
    Statement channels(db_, "SELECT data FROM channels WHERE session=? ORDER BY number"); channels.bind(1,session);
    while (channels.next()) {
        Channel raw; if (!raw.ParseFromString(channels.string(0))) throw std::runtime_error("Corrupt channel configuration"); ChannelInfo value; value.index=raw.index(); value.role=raw.role(); value.name=raw.settings().name();
        const auto &key=raw.settings().psk(); value.encrypted=!key.empty() && !(key.size()==1 && key.front()==0); if(raw.role()!=Channel::DISABLED)state.channels.push_back(value); state.channel_configs[value.index]=raw;
    }
    Statement configs(db_, "SELECT kind,category,data FROM configurations WHERE session=?"); configs.bind(1,session);
    while (configs.next()) {
        const auto category=static_cast<int>(configs.integer(1));
        if (configs.integer(0)==0) { Config raw; if (!raw.ParseFromString(configs.string(2)))throw std::runtime_error("Corrupt device configuration"); state.device_configs[category]=raw; }
        else { ModuleConfig raw; if (!raw.ParseFromString(configs.string(2)))throw std::runtime_error("Corrupt module configuration"); state.module_configs[category]=raw; }
    }
    Statement attrs(db_, "SELECT name,data FROM attributes WHERE session=?"); attrs.bind(1,session);
    while (attrs.next()) {
        if (attrs.string(0)=="owner") { User value; if(!value.ParseFromString(attrs.string(1)))throw std::runtime_error("Corrupt owner record"); state.owner=value; }
        if (attrs.string(0)=="metadata") { DeviceMetadata value; if(!value.ParseFromString(attrs.string(1)))throw std::runtime_error("Corrupt metadata record"); state.metadata=value; }
    }
    return state;
}
void Repository::store_node(const std::string &session,const Node &node) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); Statement write(db_,"INSERT INTO nodes VALUES(?,?,?) ON CONFLICT(session,number) DO UPDATE SET data=excluded.data");
    write.bind(1,session); write.bind(2,static_cast<std::int64_t>(node.number)); write.bytes(3,node_info(node).SerializeAsString()); write.next();
}
void Repository::store_channel(const std::string &session,const ChannelInfo &value) {
    Channel raw; raw.set_index(value.index); raw.set_role(static_cast<Channel::Role>(value.role)); raw.mutable_settings()->set_name(value.name); if(value.encrypted) raw.mutable_settings()->set_psk("present"); store_channel_config(session,raw);
}
void Repository::store_channel_config(const std::string &session,const Channel &raw) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); Statement write(db_,"INSERT INTO channels VALUES(?,?,?) ON CONFLICT(session,number) DO UPDATE SET data=excluded.data");
    write.bind(1,session); write.bind(2,static_cast<std::int64_t>(raw.index())); write.bytes(3,raw.SerializeAsString()); write.next();
}
void Repository::store_config(const std::string &session,const Config &value) {
    if(!value.payload_variant_case()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Statement write(db_,"INSERT INTO configurations VALUES(?,0,?,?) ON CONFLICT(session,kind,category) DO UPDATE SET data=excluded.data");
    write.bind(1,session); write.bind(2,static_cast<std::int64_t>(value.payload_variant_case())); write.bytes(3,value.SerializeAsString()); write.next();
}
void Repository::store_module_config(const std::string &session,const ModuleConfig &value) {
    if(!value.payload_variant_case()) return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Statement write(db_,"INSERT INTO configurations VALUES(?,1,?,?) ON CONFLICT(session,kind,category) DO UPDATE SET data=excluded.data");
    write.bind(1,session); write.bind(2,static_cast<std::int64_t>(value.payload_variant_case())); write.bytes(3,value.SerializeAsString()); write.next();
}
void Repository::store_attribute(const std::string &session,const char *name,const std::string &bytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_); Statement write(db_,"INSERT INTO attributes VALUES(?,?,?) ON CONFLICT(session,name) DO UPDATE SET data=excluded.data"); write.bind(1,session); write.bind(2,std::string(name)); write.bytes(3,bytes); write.next();
}
void Repository::store_owner(const std::string &session,const User &value) { store_attribute(session,"owner",value.SerializeAsString()); }
void Repository::store_metadata(const std::string &session,const DeviceMetadata &value) { store_attribute(session,"metadata",value.SerializeAsString()); }
Message Repository::store_message(const std::string &session,Message message) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const bool direct=message.to!=0 && message.to!=kBroadcastNode; const auto peer=direct?(message.outgoing?message.to:message.from):0;
    const auto observed_at=static_cast<std::int64_t>(std::time(nullptr));
    Statement write(db_,R"SQL(
INSERT INTO messages(session,sender,destination,packet,channel,received_at,rssi,snr,text,outgoing,status,detail,attempts,is_read,direct,peer,observed_at)
SELECT ?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17
WHERE ?4=0 OR NOT EXISTS(
  SELECT 1 FROM messages
  WHERE session=?1 AND sender=?2 AND packet=?4 AND outgoing=?10
    AND (?10<>0 OR (destination=?3 AND channel=?5 AND text=?9
                    AND observed_at BETWEEN ?17-60 AND ?17+60))
)
)SQL");
    write.bind(1,session); write.bind(2,static_cast<std::int64_t>(message.from)); write.bind(3,static_cast<std::int64_t>(message.to)); write.bind(4,static_cast<std::int64_t>(message.id)); write.bind(5,static_cast<std::int64_t>(message.channel)); write.bind(6,static_cast<std::int64_t>(message.received_at)); write.bind(7,static_cast<std::int64_t>(message.rssi)); write.bind(8,static_cast<double>(message.snr)); write.bytes(9,message.text); write.bind(10,static_cast<std::int64_t>(message.outgoing)); write.bind(11,static_cast<std::int64_t>(message.status)); write.bind(12,message.status_detail); write.bind(13,static_cast<std::int64_t>(message.attempts)); write.bind(14,static_cast<std::int64_t>(message.outgoing||message.read)); write.bind(15,static_cast<std::int64_t>(direct)); write.bind(16,static_cast<std::int64_t>(peer)); write.bind(17,observed_at); write.next();
    if(sqlite3_changes(db_)) message.local_id=sqlite3_last_insert_rowid(db_);
    else {
        Statement query(db_,std::string("SELECT ")+message_columns+R"SQL( FROM messages
WHERE session=?1 AND sender=?2 AND packet=?3 AND outgoing=?4
  AND (?4<>0 OR (destination=?5 AND channel=?6 AND text=?7
                 AND observed_at BETWEEN ?8-60 AND ?8+60))
ORDER BY id DESC LIMIT 1)SQL");
        query.bind(1,session);query.bind(2,static_cast<std::int64_t>(message.from));query.bind(3,static_cast<std::int64_t>(message.id));query.bind(4,static_cast<std::int64_t>(message.outgoing));query.bind(5,static_cast<std::int64_t>(message.to));query.bind(6,static_cast<std::int64_t>(message.channel));query.bytes(7,message.text);query.bind(8,observed_at);if(query.next()) message=read_message(query);
    }
    return message;
}
std::vector<Message> Repository::recent_messages(const std::string &session,std::size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_); Statement query(db_,std::string("SELECT ")+message_columns+" FROM messages WHERE session=? ORDER BY id DESC LIMIT ?");query.bind(1,session);query.bind(2,bounded(limit));
    std::vector<Message> result;while(query.next())result.push_back(read_message(query));std::reverse(result.begin(),result.end());return result;
}
std::vector<Message> Repository::query_messages(const std::string &session,bool direct,std::uint32_t peer,std::uint32_t channel,std::int64_t before,std::size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Statement query(db_,std::string("SELECT ")+message_columns+" FROM messages WHERE session=? AND direct=? AND peer=? AND channel=? AND (?=0 OR id<?) ORDER BY id DESC LIMIT ?");
    query.bind(1,session);query.bind(2,static_cast<std::int64_t>(direct));query.bind(3,static_cast<std::int64_t>(direct?peer:0));query.bind(4,static_cast<std::int64_t>(channel));query.bind(5,before);query.bind(6,before);query.bind(7,bounded(limit));
    std::vector<Message> result;while(query.next())result.push_back(read_message(query));std::reverse(result.begin(),result.end());return result;
}
std::vector<Conversation> Repository::conversations(const std::string &session) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    Statement query(db_,"SELECT id,sender,destination,packet,channel,received_at,rssi,snr,text,outgoing,status,detail,attempts,is_read,direct,peer,(SELECT count(*) FROM messages u WHERE u.session=m.session AND u.direct=m.direct AND u.peer=m.peer AND u.channel=m.channel AND u.is_read=0 AND u.outgoing=0) FROM messages m WHERE session=? AND NOT EXISTS(SELECT 1 FROM messages n WHERE n.session=m.session AND n.direct=m.direct AND n.peer=m.peer AND n.channel=m.channel AND n.id>m.id) ORDER BY id DESC");
    query.bind(1,session);std::vector<Conversation> result;
    while(query.next()){ Conversation value;value.latest=read_message(query);value.direct=query.integer(14)!=0;value.peer=static_cast<std::uint32_t>(query.integer(15));value.channel=value.latest.channel;value.unread_count=static_cast<std::size_t>(query.integer(16));result.push_back(value); }return result;
}
bool Repository::mark_read(const std::string &session,bool direct,std::uint32_t peer,std::uint32_t channel) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement write(db_,"UPDATE messages SET is_read=1 WHERE session=? AND direct=? AND peer=? AND channel=? AND is_read=0");
    write.bind(1,session);write.bind(2,static_cast<std::int64_t>(direct));write.bind(3,static_cast<std::int64_t>(direct?peer:0));write.bind(4,static_cast<std::int64_t>(channel));write.next();return sqlite3_changes(db_)>0;
}
void Repository::store_outbound(const std::string &session,const MeshPacket &packet,const DeliveryRecord &delivery) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement write(db_,"INSERT INTO outbound VALUES(?,?,?,?,?,?,?,?) ON CONFLICT(session,packet) DO UPDATE SET status=excluded.status,detail=excluded.detail,sent_at=excluded.sent_at,attempts=excluded.attempts,data=excluded.data");
    write.bind(1,session);write.bind(2,static_cast<std::int64_t>(packet.id()));write.bind(3,static_cast<std::int64_t>(delivery.status));write.bind(4,delivery.detail);write.bind(5,static_cast<std::int64_t>(delivery.sent_at));write.bind(6,static_cast<std::int64_t>(delivery.attempts));write.bind(7,static_cast<std::int64_t>(delivery.portnum));write.bytes(8,packet.SerializeAsString());write.next();
}
std::optional<Message> Repository::queue_packet(const std::string &session,const MeshPacket &packet,const DeliveryRecord &delivery) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);execute(db_,"BEGIN IMMEDIATE");
    try {
        store_outbound(session,packet,delivery);
        std::optional<Message> text;
        const auto epoch=static_cast<std::uint32_t>(std::time(nullptr));
        if(packet.has_decoded()&&packet.decoded().portnum()==TEXT_MESSAGE_APP) {
            Message value;value.from=packet.from();value.to=packet.to();value.id=packet.id();value.channel=packet.channel();value.received_at=epoch;value.text=packet.decoded().payload();value.outgoing=true;value.read=true;value.status=delivery.status;value.status_detail=delivery.detail;value.attempts=delivery.attempts;
            text=store_message(session,std::move(value));text->status=delivery.status;text->status_detail=delivery.detail;text->attempts=delivery.attempts;
            Statement update(db_,"UPDATE messages SET status=?,detail=?,attempts=? WHERE session=? AND id=?");update.bind(1,static_cast<std::int64_t>(delivery.status));update.bind(2,delivery.detail);update.bind(3,static_cast<std::int64_t>(delivery.attempts));update.bind(4,session);update.bind(5,text->local_id);update.next();
        }
        if(packet.has_decoded()&&packet.decoded().portnum()==WAYPOINT_APP) {
            WaypointRecord value;value.sender=packet.from();value.packet_id=packet.id();value.received_at=epoch;value.outgoing=true;
            if(!value.waypoint.ParseFromString(packet.decoded().payload()))throw std::runtime_error("Invalid outgoing waypoint");
            store_waypoint(session,value);
        }
        if(packet.has_decoded()&&packet.decoded().portnum()==TRACEROUTE_APP) {
            RouteRecord value;value.from=packet.from();value.to=packet.to();value.packet_id=packet.id();value.received_at=epoch;value.outgoing=true;
            if(!value.route.ParseFromString(packet.decoded().payload()))throw std::runtime_error("Invalid outgoing traceroute");
            store_route(session,value);
        }
        execute(db_,"COMMIT");return text;
    }catch(...){execute(db_,"ROLLBACK");throw;}
}
void Repository::update_delivery(const std::string &session,const DeliveryRecord &delivery) { update_deliveries(session,{delivery}); }
void Repository::update_deliveries(const std::string &session,const std::vector<DeliveryRecord> &deliveries) {
    if(deliveries.empty())return;
    std::lock_guard<std::recursive_mutex> lock(mutex_);execute(db_,"BEGIN IMMEDIATE");
    try {
        Statement write(db_,"UPDATE outbound SET status=?,detail=?,sent_at=?,attempts=? WHERE session=? AND packet=?");
        Statement message(db_,"UPDATE messages SET status=?,detail=?,attempts=? WHERE session=? AND packet=? AND outgoing=1");
        for(const auto &delivery:deliveries) {
            write.bind(1,static_cast<std::int64_t>(delivery.status));write.bind(2,delivery.detail);write.bind(3,static_cast<std::int64_t>(delivery.sent_at));write.bind(4,static_cast<std::int64_t>(delivery.attempts));write.bind(5,session);write.bind(6,static_cast<std::int64_t>(delivery.packet_id));write.next();write.reset();
            message.bind(1,static_cast<std::int64_t>(delivery.status));message.bind(2,delivery.detail);message.bind(3,static_cast<std::int64_t>(delivery.attempts));message.bind(4,session);message.bind(5,static_cast<std::int64_t>(delivery.packet_id));message.next();message.reset();
        }
        execute(db_,"COMMIT");
    }catch(...){execute(db_,"ROLLBACK");throw;}
}

std::vector<StoredOutbound> Repository::outbound(const std::string &session) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement query(db_,"SELECT packet,status,detail,sent_at,attempts,port,data FROM outbound WHERE session=? ORDER BY rowid DESC LIMIT 1000");query.bind(1,session);
    std::vector<StoredOutbound> result;while(query.next()){ StoredOutbound value;value.delivery=read_delivery(query);if(!value.packet.ParseFromString(query.string(6)))throw std::runtime_error("Corrupt outbound record");value.delivery.source=value.packet.from();value.delivery.destination=value.packet.to();value.delivery.channel=value.packet.channel();result.push_back(value); }return result;
}
std::optional<StoredOutbound> Repository::find_outbound(const std::string &session,std::uint32_t id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement query(db_,"SELECT packet,status,detail,sent_at,attempts,port,data FROM outbound WHERE session=? AND packet=?");query.bind(1,session);query.bind(2,static_cast<std::int64_t>(id));
    if(!query.next())return std::nullopt;
    StoredOutbound value;value.delivery=read_delivery(query);
    if(!value.packet.ParseFromString(query.string(6)))throw std::runtime_error("Corrupt outbound record");
    value.delivery.source=value.packet.from();value.delivery.destination=value.packet.to();value.delivery.channel=value.packet.channel();
    return value;
}
std::optional<std::uint32_t> Repository::message_packet_id(const std::string &session,std::int64_t id) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement query(db_,"SELECT packet FROM messages WHERE session=? AND id=? AND outgoing=1");query.bind(1,session);query.bind(2,id);if(query.next())return static_cast<std::uint32_t>(query.integer(0));return std::nullopt;
}
void Repository::recover_interrupted(const std::string &session) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    execute(db_,"BEGIN IMMEDIATE");
    try {
        Statement outbound(db_,"UPDATE outbound SET status=?,detail=? WHERE session=? AND status IN (?,?)");
        outbound.bind(1,static_cast<std::int64_t>(MessageStatus::failed));outbound.bind(2,std::string("Application stopped before confirmation; delivery is unknown. Retry explicitly."));outbound.bind(3,session);outbound.bind(4,static_cast<std::int64_t>(MessageStatus::queued));outbound.bind(5,static_cast<std::int64_t>(MessageStatus::sent));outbound.next();
        Statement messages(db_,"UPDATE messages SET status=?,detail=? WHERE session=? AND outgoing=1 AND status IN (?,?)");
        messages.bind(1,static_cast<std::int64_t>(MessageStatus::failed));messages.bind(2,std::string("Application stopped before confirmation; delivery is unknown. Retry explicitly."));messages.bind(3,session);messages.bind(4,static_cast<std::int64_t>(MessageStatus::queued));messages.bind(5,static_cast<std::int64_t>(MessageStatus::sent));messages.next();
        execute(db_,"COMMIT");
    } catch(...) {execute(db_,"ROLLBACK");throw;}
}

void Repository::store_event(const std::string &session,int kind,std::uint32_t node,std::uint32_t peer,std::uint32_t packet,std::uint32_t request,std::uint32_t time,bool outgoing,const std::string &bytes) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    const auto identity=std::to_string(node)+":"+std::to_string(packet)+":"+std::to_string(outgoing)+":"+
                        (packet ? std::string{} : std::to_string(time)+":"+fingerprint(bytes));
    const auto observed_at=static_cast<std::int64_t>(std::time(nullptr));
    Statement write(db_,R"SQL(
INSERT INTO events(session,kind,node,peer,packet,request,received_at,outgoing,data,identity,observed_at)
SELECT ?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11
WHERE NOT EXISTS(
  SELECT 1 FROM events
  WHERE session=?1 AND kind=?2 AND node=?3 AND peer=?4 AND packet=?5 AND request=?6
    AND outgoing=?8 AND data=?9 AND (?5<>0 OR received_at=?7)
    AND observed_at BETWEEN ?11-60 AND ?11+60
)
)SQL");
    write.bind(1,session);write.bind(2,static_cast<std::int64_t>(kind));write.bind(3,static_cast<std::int64_t>(node));write.bind(4,static_cast<std::int64_t>(peer));write.bind(5,static_cast<std::int64_t>(packet));write.bind(6,static_cast<std::int64_t>(request));write.bind(7,static_cast<std::int64_t>(time));write.bind(8,static_cast<std::int64_t>(outgoing));write.bytes(9,bytes);write.bind(10,identity);write.bind(11,observed_at);write.next();
}
void Repository::store_position(const std::string &session,const PositionRecord &value){store_event(session,1,value.node_number,0,value.packet_id,0,value.received_at,false,value.position.SerializeAsString());}
void Repository::store_telemetry(const std::string &session,const TelemetryRecord &value){store_event(session,2,value.node_number,0,value.packet_id,0,value.received_at,false,value.telemetry.SerializeAsString());}
void Repository::store_waypoint(const std::string &session,const WaypointRecord &value){store_event(session,3,value.sender,0,value.packet_id,0,value.received_at,value.outgoing,value.waypoint.SerializeAsString());}
void Repository::store_route(const std::string &session,const RouteRecord &value){store_event(session,4,value.from,value.to,value.packet_id,value.request_id,value.received_at,value.outgoing,value.route.SerializeAsString());}
std::vector<PositionRecord> Repository::positions(const std::string &session,std::uint32_t node,std::int64_t before,std::size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement query(db_,"SELECT id,node,packet,received_at,data FROM events WHERE session=? AND kind=1 AND (?=0 OR node=?) AND (?=0 OR id<?) ORDER BY id DESC LIMIT ?");
    query.bind(1,session);query.bind(2,static_cast<std::int64_t>(node));query.bind(3,static_cast<std::int64_t>(node));query.bind(4,before);query.bind(5,before);query.bind(6,bounded(limit));std::vector<PositionRecord> result;
    while(query.next()){PositionRecord value;value.local_id=query.integer(0);value.node_number=static_cast<std::uint32_t>(query.integer(1));value.packet_id=static_cast<std::uint32_t>(query.integer(2));value.received_at=static_cast<std::uint32_t>(query.integer(3));if(!value.position.ParseFromString(query.string(4)))throw std::runtime_error("Corrupt position record");result.push_back(value);}std::reverse(result.begin(),result.end());return result;
}
std::vector<TelemetryRecord> Repository::telemetry(const std::string &session,std::uint32_t node,std::int64_t before,std::size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement query(db_,"SELECT id,node,packet,received_at,data FROM events WHERE session=? AND kind=2 AND (?=0 OR node=?) AND (?=0 OR id<?) ORDER BY id DESC LIMIT ?");
    query.bind(1,session);query.bind(2,static_cast<std::int64_t>(node));query.bind(3,static_cast<std::int64_t>(node));query.bind(4,before);query.bind(5,before);query.bind(6,bounded(limit));std::vector<TelemetryRecord> result;
    while(query.next()){TelemetryRecord value;value.local_id=query.integer(0);value.node_number=static_cast<std::uint32_t>(query.integer(1));value.packet_id=static_cast<std::uint32_t>(query.integer(2));value.received_at=static_cast<std::uint32_t>(query.integer(3));if(!value.telemetry.ParseFromString(query.string(4)))throw std::runtime_error("Corrupt telemetry record");result.push_back(value);}std::reverse(result.begin(),result.end());return result;
}
std::vector<WaypointRecord> Repository::waypoints(const std::string &session,std::size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement query(db_,"SELECT id,node,packet,received_at,data,outgoing FROM events WHERE session=? AND kind=3 ORDER BY id DESC LIMIT ?");query.bind(1,session);query.bind(2,bounded(limit));std::vector<WaypointRecord> result;
    while(query.next()){WaypointRecord value;value.local_id=query.integer(0);value.sender=static_cast<std::uint32_t>(query.integer(1));value.packet_id=static_cast<std::uint32_t>(query.integer(2));value.received_at=static_cast<std::uint32_t>(query.integer(3));value.outgoing=query.integer(5)!=0;if(!value.waypoint.ParseFromString(query.string(4)))throw std::runtime_error("Corrupt waypoint record");result.push_back(value);}std::reverse(result.begin(),result.end());return result;
}
std::vector<RouteRecord> Repository::routes(const std::string &session,std::uint32_t node,std::size_t limit) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);Statement query(db_,"SELECT id,node,peer,packet,request,received_at,data,outgoing FROM events WHERE session=? AND kind=4 AND (?=0 OR node=? OR peer=?) ORDER BY id DESC LIMIT ?");
    query.bind(1,session);query.bind(2,static_cast<std::int64_t>(node));query.bind(3,static_cast<std::int64_t>(node));query.bind(4,static_cast<std::int64_t>(node));query.bind(5,bounded(limit));std::vector<RouteRecord> result;
    while(query.next()){RouteRecord value;value.local_id=query.integer(0);value.from=static_cast<std::uint32_t>(query.integer(1));value.to=static_cast<std::uint32_t>(query.integer(2));value.packet_id=static_cast<std::uint32_t>(query.integer(3));value.request_id=static_cast<std::uint32_t>(query.integer(4));value.received_at=static_cast<std::uint32_t>(query.integer(5));value.outgoing=query.integer(7)!=0;if(!value.route.ParseFromString(query.string(6)))throw std::runtime_error("Corrupt route record");result.push_back(value);}std::reverse(result.begin(),result.end());return result;
}
}
