#include "pages/node_detail/node_detail_view_model.hpp"
#include "components/utf8.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace {

std::string last_heard_text(std::uint32_t timestamp)
{
    if (timestamp == 0) return "Last heard unknown";
    const auto now = std::time(nullptr);
    if (now <= static_cast<std::time_t>(timestamp)) return "Just now";
    const auto seconds = static_cast<std::uint64_t>(now - static_cast<std::time_t>(timestamp));
    if (seconds < 60) return "Just now";
    if (seconds < 3600) return std::to_string(seconds / 60) + " min ago";
    if (seconds < 86400) return std::to_string(seconds / 3600) + " hr ago";
    return std::to_string(seconds / 86400) + " d ago";
}

bool equal(const NodeDetailPresentation &left, const NodeDetailPresentation &right)
{
    return left.found == right.found && left.has_metrics == right.has_metrics &&
           left.has_position == right.has_position && left.initials == right.initials && left.name == right.name &&
           left.presence == right.presence && left.battery == right.battery && left.voltage == right.voltage &&
           left.coordinates == right.coordinates && left.altitude == right.altitude && left.id == right.id &&
           left.number == right.number && left.signal == right.signal && left.role == right.role &&
           left.route == right.route && left.last_heard == right.last_heard &&
           left.telemetry_history == right.telemetry_history && left.route_history == right.route_history &&
           left.position_count == right.position_count && left.position_history_limited == right.position_history_limited &&
           left.latitude == right.latitude && left.longitude == right.longitude;
}

}  // namespace

NodeDetailViewModel::NodeDetailViewModel(meshtastic::MeshtasticClient *client)
    : content_revision_(0, 0), client_(client)
{
}

void NodeDetailViewModel::select_node(std::uint32_t number, const meshtastic::ClientSnapshot &snapshot)
{
    selected_node_ = number;
    sync(snapshot);
}

void NodeDetailViewModel::sync(const meshtastic::ClientSnapshot &snapshot)
{
    snapshot_ = snapshot;
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);
    std::int64_t newest_position = 0, newest_telemetry = 0, newest_route = 0;
    for (const auto &record : snapshot.positions) if (record.node_number == selected_node_)
        newest_position = std::max(newest_position, record.local_id);
    for (const auto &record : snapshot.telemetry) if (record.node_number == selected_node_)
        newest_telemetry = std::max(newest_telemetry, record.local_id);
    for (const auto &record : snapshot.routes) if (record.from == selected_node_ || record.to == selected_node_)
        newest_route = std::max(newest_route, record.local_id);
    const bool new_history_scope = history_node_ != selected_node_ || history_radio_ != snapshot.my_node_number;
    if (new_history_scope || newest_position != latest_position_id_) {
        position_history_ = client_ ? client_->query_positions(selected_node_, 0, 10000) : snapshot.positions;
        if (position_history_.empty()) position_history_ = snapshot.positions;
        latest_position_id_ = newest_position;
    }
    if (new_history_scope || newest_telemetry != latest_telemetry_id_) {
        telemetry_history_ = client_ ? client_->query_telemetry(selected_node_, 0, 10000) : snapshot.telemetry;
        if (telemetry_history_.empty()) telemetry_history_ = snapshot.telemetry;
        latest_telemetry_id_ = newest_telemetry;
    }
    if (new_history_scope || newest_route != latest_route_id_) {
        route_history_ = client_ ? client_->query_routes(selected_node_, 10000) : snapshot.routes;
        if (route_history_.empty()) route_history_ = snapshot.routes;
        latest_route_id_ = newest_route;
    }
    history_node_ = selected_node_;
    history_radio_ = snapshot.my_node_number;
    NodeDetailPresentation next;
    const auto node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(),
                                   [this](const meshtastic::Node &item) { return item.number == selected_node_; });
    if (node != snapshot.nodes.end()) {
        next.found        = true;
        next.initials     = utf8_prefix_characters(!node->short_name.empty() ? node->short_name : node->long_name, 2);
        next.name         = node->long_name.empty() ? node->id : node->long_name;
        next.presence     = last_heard_text(node->last_heard);
        next.last_heard   = last_heard_text(node->last_heard);
        next.has_metrics  = node->battery_level.has_value() || node->voltage.has_value();
        next.has_position = node->latitude.has_value() && node->longitude.has_value();
        next.id           = node->id;
        if (node->hops_away) next.presence += " | " + std::to_string(*node->hops_away) + " hops";
        if (node->battery_level) next.battery = std::to_string(*node->battery_level) + "% battery";
        if (node->voltage) {
            char voltage[32]{};
            std::snprintf(voltage, sizeof(voltage), "%.2f V", *node->voltage);
            next.voltage = voltage;
        }
        if (next.has_position) {
            next.latitude = *node->latitude;
            next.longitude = *node->longitude;
            char coordinates[64]{};
            std::snprintf(coordinates, sizeof(coordinates), "%.5f, %.5f", *node->latitude, *node->longitude);
            next.coordinates = coordinates;
        }
        if (node->altitude) next.altitude = "Altitude " + std::to_string(*node->altitude) + " m";
        char number[11]{};
        std::snprintf(number, sizeof(number), "0x%08x", node->number);
        next.number = number;
        char signal[32]{};
        std::snprintf(signal, sizeof(signal), "SNR %.1f dB", node->snr);
        next.signal = signal;
        next.role = meshtastic::Config_DeviceConfig_Role_Name(
            static_cast<meshtastic::Config_DeviceConfig_Role>(node->role));
        next.route = node->via_mqtt ? "Via MQTT" : "Direct mesh";
    }

    for (const auto &position : position_history_) if (position.node_number == selected_node_) ++next.position_count;
    next.position_history_limited = client_ && next.position_count >= 10000;
    auto stamp = [](std::uint32_t value) {
        if (!value) return std::string("Time unknown");
        const auto timestamp = static_cast<std::time_t>(value);
        char text[32]{};
        std::strftime(text, sizeof(text), "%m-%d %H:%M", std::localtime(&timestamp));
        return std::string(text);
    };
    for (auto it = telemetry_history_.rbegin(); it != telemetry_history_.rend() && next.telemetry_history.size() < 12; ++it) {
        if (it->node_number != selected_node_) continue;
        std::ostringstream text;
        text << stamp(it->received_at) << "  " << std::fixed << std::setprecision(1);
        const auto &metrics = it->telemetry;
        if (metrics.has_device_metrics()) {
            const auto &m = metrics.device_metrics();
            if (m.has_battery_level()) text << "Battery " << m.battery_level() << "%  ";
            if (m.has_voltage()) text << m.voltage() << " V  ";
            if (m.has_channel_utilization()) text << "Channel " << m.channel_utilization() << "%  ";
            if (m.has_air_util_tx()) text << "TX " << m.air_util_tx() << "%  ";
        } else if (metrics.has_environment_metrics()) {
            const auto &m = metrics.environment_metrics();
            if (m.has_temperature()) text << m.temperature() << " °C  ";
            if (m.has_relative_humidity()) text << m.relative_humidity() << "% RH  ";
            if (m.has_barometric_pressure()) text << m.barometric_pressure() << " hPa  ";
            if (m.has_lux()) text << m.lux() << " lux  ";
            if (m.has_wind_speed()) text << "Wind " << m.wind_speed() << " m/s  ";
            if (m.has_soil_moisture()) text << "Soil " << m.soil_moisture() << "%  ";
        } else if (metrics.has_air_quality_metrics()) {
            const auto &m = metrics.air_quality_metrics();
            if (m.has_pm25_standard()) text << "PM2.5 " << m.pm25_standard() << " µg/m³  ";
            if (m.has_co2()) text << "CO₂ " << m.co2() << " ppm  ";
        } else if (metrics.has_power_metrics()) {
            const auto &m = metrics.power_metrics();
            if (m.has_ch1_voltage()) text << "CH1 " << m.ch1_voltage() << " V  ";
            if (m.has_ch1_current()) text << m.ch1_current() << " mA  ";
            if (m.has_ch2_voltage()) text << "CH2 " << m.ch2_voltage() << " V  ";
        } else if (metrics.has_health_metrics()) {
            const auto &m = metrics.health_metrics();
            if (m.has_heart_bpm()) text << "Heart " << m.heart_bpm() << " bpm  ";
            if (m.has_spo2()) text << "SpO2 " << m.spo2() << "%  ";
            if (m.has_temperature()) text << m.temperature() << " °C  ";
        } else if (metrics.has_host_metrics()) {
            const auto &m = metrics.host_metrics();
            text << "Uptime " << m.uptime_seconds() << " s  Free memory "
                 << m.freemem_bytes() / 1024 / 1024 << " MiB  Load " << m.load1() / 100.0;
            if (m.has_user_string()) text << "  " << m.user_string();
        } else if (metrics.has_local_stats()) {
            const auto &m = metrics.local_stats();
            text << "TX " << m.num_packets_tx() << "  RX " << m.num_packets_rx()
                 << "  Channel " << m.channel_utilization() << "%";
        } else if (metrics.has_traffic_management_stats()) {
            const auto &m = metrics.traffic_management_stats();
            text << "Inspected " << m.packets_inspected() << "  Rate-limited " << m.rate_limit_drops();
        } else {
            text << "Telemetry sample without measurements";
        }
        next.telemetry_history.push_back(text.str());
    }
    auto route_name = [&snapshot](std::uint32_t number) {
        for (const auto &node : snapshot.nodes) if (node.number == number && !node.long_name.empty()) return node.long_name;
        char text[16]{}; std::snprintf(text, sizeof(text), "!%08x", number); return std::string(text);
    };
    for (auto it = route_history_.rbegin(); it != route_history_.rend() && next.route_history.size() < 8; ++it) {
        if (it->from != selected_node_ && it->to != selected_node_) continue;
        if (it->outgoing) continue;
        std::string text = stamp(it->received_at) + "  " + route_name(it->to);
        for (int index = 0; index < it->route.route_size(); ++index) {
            text += " → " + route_name(it->route.route(index));
            if (index < it->route.snr_towards_size()) {
                text += " (" + std::to_string(it->route.snr_towards(index) / 4.0) + " dB)";
            }
        }
        text += " → " + route_name(it->from);
        if (it->route.route_back_size()) {
            text += " | Return";
            for (auto number : it->route.route_back()) text += " → " + route_name(number);
        }
        next.route_history.push_back(std::move(text));
    }
    if (equal(next, presentation_)) return;
    presentation_ = std::move(next);
    publish_content();
}

std::uint32_t NodeDetailViewModel::selected_node() const noexcept
{
    return selected_node_;
}

const NodeDetailPresentation &NodeDetailViewModel::presentation() const noexcept
{
    return presentation_;
}

lv_subject_t *NodeDetailViewModel::content_subject() noexcept
{
    return content_revision_.get();
}

void NodeDetailViewModel::publish_content()
{
    const auto current = content_revision_.value();
    content_revision_.set(current == INT32_MAX ? 0 : current + 1);
}

bool NodeDetailViewModel::request_telemetry()
{
    const bool queued = client_ && connected_.value() && client_->request_telemetry(selected_node_);
    action_status_.set(queued ? "Telemetry request queued; waiting for radio response" : "Connect to request telemetry");
    return queued;
}

bool NodeDetailViewModel::request_traceroute()
{
    const bool queued = client_ && connected_.value() && selected_node_ != snapshot_.my_node_number &&
                        client_->request_traceroute(selected_node_);
    action_status_.set(queued ? "Route request queued; waiting for radio response" : "Connect and choose a remote node to trace");
    return queued;
}

bool NodeDetailViewModel::share_waypoint(std::string name, std::string description, double latitude,
                                         double longitude, std::uint32_t lifetime, std::uint32_t channel)
{
    if (name.empty() || name.size() > 30 || description.size() > 100 || !std::isfinite(latitude) ||
        !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180 ||
        lifetime == 0 || lifetime > 30U * 86400U) {
        action_status_.set("Check waypoint name (1–30 bytes), description (100 bytes), coordinates and expiry");
        return false;
    }
    meshtastic::Waypoint waypoint;
    waypoint.set_name(std::move(name));
    waypoint.set_description(std::move(description));
    waypoint.set_latitude_i(static_cast<std::int32_t>(std::llround(latitude * 1e7)));
    waypoint.set_longitude_i(static_cast<std::int32_t>(std::llround(longitude * 1e7)));
    waypoint.set_expire(static_cast<std::uint32_t>(std::time(nullptr)) + lifetime);
    const bool queued = client_ && connected_.value() && client_->send_waypoint(waypoint, meshtastic::kBroadcastNode, channel);
    action_status_.set(queued ? "Waypoint queued for sharing on the selected channel" : "Connect to share a waypoint");
    return queued;
}

const std::vector<meshtastic::ChannelInfo> &NodeDetailViewModel::channels() const noexcept { return snapshot_.channels; }
lv_subject_t *NodeDetailViewModel::connected_subject() noexcept { return connected_.get(); }
lv_subject_t *NodeDetailViewModel::action_status_subject() noexcept { return action_status_.get(); }
