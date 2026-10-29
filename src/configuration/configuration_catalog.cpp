#include "configuration/configuration_catalog.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <type_traits>
#include <utility>

namespace meshtastic::configuration {
namespace {
template <typename Getter, typename Setter>
Field scalar(const char *id, const char *label, const char *help, FieldType type,
             double minimum, double maximum, std::size_t bytes, bool secret,
             Getter get, Setter set, std::vector<Choice> choices = {})
{
    using T = std::decay_t<decltype(get(std::declval<const Config &>(), std::declval<const ModuleConfig &>()))>;
    Field field{id, label, help, type, minimum, maximum, bytes, secret, std::move(choices), {}, {}};
    field.read = [get](const Config &device, const ModuleConfig &module) -> std::string {
        const auto value = get(device, module);
        if constexpr (std::is_same_v<T, std::string>) return value;
        else if constexpr (std::is_same_v<T, bool>) return value ? "true" : "false";
        else if constexpr (std::is_floating_point_v<T>) {
            std::ostringstream stream;
            stream << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
            return stream.str();
        } else return std::to_string(static_cast<std::int64_t>(value));
    };
    field.write = [set](Config &device, ModuleConfig &module, const std::string &value) {
        if constexpr (std::is_same_v<T, std::string>) set(device, module, value);
        else if constexpr (std::is_same_v<T, bool>) set(device, module, value == "true");
        else if constexpr (std::is_floating_point_v<T>) set(device, module, static_cast<T>(std::strtod(value.c_str(), nullptr)));
        else set(device, module, static_cast<T>(std::strtoll(value.c_str(), nullptr, 10)));
    };
    return field;
}
template <typename Valid, typename Name>
std::vector<Choice> choices(int minimum, int maximum, Valid valid, Name name)
{
    std::vector<Choice> result;
    for (int value = minimum; value <= maximum; ++value) {
        if (valid(value)) result.push_back({value, name(value)});
    }
    return result;
}
#define D(TYPE, CATEGORY, FIELD, LABEL, HELP, MIN, MAX, BYTES, SECRET) \
    scalar(#FIELD, LABEL, HELP, FieldType::TYPE, MIN, MAX, BYTES, SECRET, \
        [](const Config &c, const ModuleConfig &) { return c.CATEGORY().FIELD(); }, \
        [](Config &c, ModuleConfig &, auto v) { c.mutable_##CATEGORY()->set_##FIELD(v); })
#define M(TYPE, CATEGORY, FIELD, LABEL, HELP, MIN, MAX, BYTES, SECRET) \
    scalar(#FIELD, LABEL, HELP, FieldType::TYPE, MIN, MAX, BYTES, SECRET, \
        [](const Config &, const ModuleConfig &c) { return c.CATEGORY().FIELD(); }, \
        [](Config &, ModuleConfig &c, auto v) { c.mutable_##CATEGORY()->set_##FIELD(v); })
#define DE(CATEGORY, FIELD, LABEL, CONTAINER, ENUM) \
    scalar(#FIELD, LABEL, "Choose a supported value", FieldType::enumeration, 0, 0, 0, false, \
        [](const Config &c, const ModuleConfig &) { return c.CATEGORY().FIELD(); }, \
        [](Config &c, ModuleConfig &, auto v) { c.mutable_##CATEGORY()->set_##FIELD(v); }, \
        choices(CONTAINER::ENUM##_MIN, CONTAINER::ENUM##_MAX, \
            [](int v) { return CONTAINER::ENUM##_IsValid(v); }, \
            [](int v) { return CONTAINER::ENUM##_Name(v); }))
#define ME(CATEGORY, FIELD, LABEL, CONTAINER, ENUM) \
    scalar(#FIELD, LABEL, "Choose a supported value", FieldType::enumeration, 0, 0, 0, false, \
        [](const Config &, const ModuleConfig &c) { return c.CATEGORY().FIELD(); }, \
        [](Config &, ModuleConfig &c, auto v) { c.mutable_##CATEGORY()->set_##FIELD(v); }, \
        choices(CONTAINER::ENUM##_MIN, CONTAINER::ENUM##_MAX, \
            [](int v) { return CONTAINER::ENUM##_IsValid(v); }, \
            [](int v) { return CONTAINER::ENUM##_Name(v); }))
}  // namespace
const std::vector<Category> &catalog()
{
    // Case and request values follow the pinned official protobuf schemas.
    static const std::vector<Category> categories = {
        {Scope::device, 1, 0, "Device", {
            DE(device, role, "Node role", Config::DeviceConfig, Role),
            DE(device, rebroadcast_mode, "Rebroadcast mode", Config::DeviceConfig, RebroadcastMode),
            D(integer, device, node_info_broadcast_secs, "Node information interval (seconds)", "0 uses the firmware default", 0, 4294967295, 0, false),
            D(text, device, tzdef, "Time zone definition", "POSIX time zone; empty uses device default", 0, 0, 96, false),
            D(boolean, device, double_tap_as_button_press, "Double tap acts as button", "", 0, 1, 0, false),
            D(boolean, device, disable_triple_click, "Disable triple click", "", 0, 1, 0, false),
            D(boolean, device, led_heartbeat_disabled, "Disable heartbeat LED", "", 0, 1, 0, false),
            DE(device, buzzer_mode, "Buzzer mode", Config::DeviceConfig, BuzzerMode),
            D(integer, device, button_gpio, "Button GPIO", "", 0, 255, 0, false),
            D(integer, device, buzzer_gpio, "Buzzer GPIO", "", 0, 255, 0, false),
        }},
        {Scope::device, 2, 1, "Position", {
            DE(position, gps_mode, "GPS mode", Config::PositionConfig, GpsMode),
            D(boolean, position, fixed_position, "Fixed position", "", 0, 1, 0, false),
            D(integer, position, position_broadcast_secs, "Position interval (seconds)", "0 uses the firmware default", 0, 4294967295, 0, false),
            D(boolean, position, position_broadcast_smart_enabled, "Smart position broadcasts", "", 0, 1, 0, false),
            D(integer, position, gps_update_interval, "GPS update interval (seconds)", "", 0, 4294967295, 0, false),
            D(integer, position, broadcast_smart_minimum_distance, "Smart broadcast distance (metres)", "", 0, 4294967295, 0, false),
            D(integer, position, broadcast_smart_minimum_interval_secs, "Smart broadcast minimum interval (seconds)", "", 0, 4294967295, 0, false),
        }},
        {Scope::device, 3, 2, "Power", {
            D(boolean, power, is_power_saving, "Power saving", "", 0, 1, 0, false),
            D(integer, power, on_battery_shutdown_after_secs, "Battery shutdown delay (seconds)", "", 0, 4294967295, 0, false),
            D(decimal, power, adc_multiplier_override, "Battery voltage multiplier", "0 uses the hardware default", 0, 100, 0, false),
            D(integer, power, wait_bluetooth_secs, "Bluetooth wait (seconds)", "", 0, 4294967295, 0, false),
            D(integer, power, sds_secs, "Super deep sleep (seconds)", "", 0, 4294967295, 0, false),
            D(integer, power, ls_secs, "Light sleep (seconds)", "", 0, 4294967295, 0, false),
            D(integer, power, min_wake_secs, "Minimum wake time (seconds)", "", 0, 4294967295, 0, false),
        }},
        {Scope::device, 4, 3, "Network", {
            D(boolean, network, wifi_enabled, "Wi-Fi enabled", "", 0, 1, 0, false),
            D(text, network, wifi_ssid, "Wi-Fi network name", "", 0, 0, 32, false),
            D(text, network, wifi_psk, "Wi-Fi password", "", 0, 0, 64, true),
            D(text, network, ntp_server, "Time server", "", 0, 0, 128, false),
            D(boolean, network, eth_enabled, "Ethernet enabled", "", 0, 1, 0, false),
            DE(network, address_mode, "Address assignment", Config::NetworkConfig, AddressMode),
            D(text, network, rsyslog_server, "Remote log server", "", 0, 0, 128, false),
        }},
        {Scope::device, 5, 4, "Display", {
            D(integer, display, screen_on_secs, "Screen on time (seconds)", "", 0, 4294967295, 0, false),
            D(integer, display, auto_screen_carousel_secs, "Screen carousel interval (seconds)", "", 0, 4294967295, 0, false),
            D(boolean, display, flip_screen, "Rotate display 180 degrees", "", 0, 1, 0, false),
            DE(display, units, "Units", Config::DisplayConfig, DisplayUnits),
            DE(display, displaymode, "Display mode", Config::DisplayConfig, DisplayMode),
            D(boolean, display, heading_bold, "Bold heading", "", 0, 1, 0, false),
            D(boolean, display, wake_on_tap_or_motion, "Wake on tap or motion", "", 0, 1, 0, false),
            D(boolean, display, use_long_node_name, "Show long node names", "", 0, 1, 0, false),
            D(boolean, display, enable_message_bubbles, "Message bubbles", "", 0, 1, 0, false),
        }},
        {Scope::device, 6, 5, "LoRa", {
            DE(lora, region, "LoRa region", Config::LoRaConfig, RegionCode),
            D(boolean, lora, use_preset, "Use modem preset", "", 0, 1, 0, false),
            DE(lora, modem_preset, "Modem preset", Config::LoRaConfig, ModemPreset),
            D(integer, lora, hop_limit, "Hop limit", "", 0, 7, 0, false),
            D(boolean, lora, tx_enabled, "Transmit enabled", "", 0, 1, 0, false),
            D(integer, lora, tx_power, "Transmit power (dBm)", "0 uses firmware default; hardware and region limits apply", -30, 40, 0, false),
            D(integer, lora, channel_num, "Frequency slot", "", 0, 500, 0, false),
            D(boolean, lora, ignore_mqtt, "Ignore MQTT traffic", "", 0, 1, 0, false),
            D(boolean, lora, config_ok_to_mqtt, "Allow configuration reports to MQTT", "", 0, 1, 0, false),
            D(decimal, lora, frequency_offset, "Frequency correction (MHz)", "", -100, 100, 0, false),
        }},
        {Scope::device, 7, 6, "Bluetooth", {
            D(boolean, bluetooth, enabled, "Bluetooth enabled", "", 0, 1, 0, false),
            DE(bluetooth, mode, "Pairing mode", Config::BluetoothConfig, PairingMode),
            D(integer, bluetooth, fixed_pin, "Fixed pairing PIN", "Use a six digit PIN in fixed PIN mode", 0, 999999, 0, true),
        }},
        {Scope::device, 8, 7, "Security", {
            D(boolean, security, is_managed, "Managed by mesh administrator", "", 0, 1, 0, false),
            D(boolean, security, serial_enabled, "Serial API enabled", "", 0, 1, 0, false),
            D(boolean, security, debug_log_api_enabled, "Send debug logs to client", "", 0, 1, 0, false),
            D(boolean, security, admin_channel_enabled, "Legacy admin channel enabled", "Allows control using the legacy admin channel", 0, 1, 0, false),
            DE(security, packet_signature_policy, "Packet signature policy", Config::SecurityConfig, PacketSignaturePolicy),
        }},
        {Scope::device, 10, 9, "Device screen", {
            D(integer, device_ui, screen_brightness, "Display brightness", "", 1, 255, 0, false),
            D(integer, device_ui, screen_timeout, "Screen timeout (seconds)", "", 0, 900, 0, false),
            D(boolean, device_ui, screen_lock, "Screen lock", "", 0, 1, 0, false),
            D(boolean, device_ui, settings_lock, "Settings lock", "", 0, 1, 0, false),
            D(integer, device_ui, pin_code, "Screen PIN", "", 0, 999999, 0, true),
            D(boolean, device_ui, alert_enabled, "Audible alerts", "", 0, 1, 0, false),
            D(boolean, device_ui, banner_enabled, "Notification banners", "", 0, 1, 0, false),
            D(boolean, device_ui, is_clockface_analog, "Analog clock", "", 0, 1, 0, false),
        }},
        {Scope::module, 1, 0, "MQTT", {
            M(boolean, mqtt, enabled, "MQTT enabled", "", 0, 1, 0, false),
            M(text, mqtt, address, "Server address", "", 0, 0, 128, false),
            M(text, mqtt, username, "Username", "", 0, 0, 64, false),
            M(text, mqtt, password, "Password", "", 0, 0, 128, true),
            M(boolean, mqtt, encryption_enabled, "Encrypted payloads", "", 0, 1, 0, false),
            M(boolean, mqtt, tls_enabled, "TLS enabled", "", 0, 1, 0, false),
            M(text, mqtt, root, "Root topic", "", 0, 0, 64, false),
            M(boolean, mqtt, proxy_to_client_enabled, "Use connected client as proxy", "", 0, 1, 0, false),
            M(boolean, mqtt, map_reporting_enabled, "Report node to map", "", 0, 1, 0, false),
        }},
        {Scope::module, 2, 1, "Serial", {
            M(boolean, serial, enabled, "Serial module enabled", "", 0, 1, 0, false),
            M(boolean, serial, echo, "Echo received data", "", 0, 1, 0, false),
            M(integer, serial, rxd, "RX GPIO", "", 0, 255, 0, false),
            M(integer, serial, txd, "TX GPIO", "", 0, 255, 0, false),
            ME(serial, baud, "Baud rate", ModuleConfig::SerialConfig, Serial_Baud),
            ME(serial, mode, "Serial mode", ModuleConfig::SerialConfig, Serial_Mode),
            M(integer, serial, timeout, "Timeout (milliseconds)", "", 0, 4294967295, 0, false),
            M(boolean, serial, override_console_serial_port, "Use console serial port", "", 0, 1, 0, false),
        }},
        {Scope::module, 3, 2, "External notification", {
            M(boolean, external_notification, enabled, "External notifications enabled", "", 0, 1, 0, false),
            M(integer, external_notification, output_ms, "Output duration (milliseconds)", "", 0, 4294967295, 0, false),
            M(integer, external_notification, output, "Output GPIO", "", 0, 255, 0, false),
            M(boolean, external_notification, active, "Active high", "", 0, 1, 0, false),
            M(boolean, external_notification, alert_message, "Alert on messages", "", 0, 1, 0, false),
            M(boolean, external_notification, alert_bell, "Alert on bell", "", 0, 1, 0, false),
            M(boolean, external_notification, use_pwm, "Use PWM buzzer", "", 0, 1, 0, false),
            M(integer, external_notification, nag_timeout, "Repeat alerts (seconds)", "", 0, 4294967295, 0, false),
            M(boolean, external_notification, use_i2s_as_buzzer, "Use speaker as buzzer", "", 0, 1, 0, false),
        }},
        {Scope::module, 4, 3, "Store and forward", {
            M(boolean, store_forward, enabled, "Store and forward enabled", "", 0, 1, 0, false),
            M(boolean, store_forward, is_server, "Act as message server", "", 0, 1, 0, false),
            M(boolean, store_forward, heartbeat, "Send heartbeat", "", 0, 1, 0, false),
            M(integer, store_forward, records, "Record capacity", "", 0, 4294967295, 0, false),
            M(integer, store_forward, history_return_max, "Maximum history messages", "", 0, 4294967295, 0, false),
            M(integer, store_forward, history_return_window, "History window (minutes)", "", 0, 4294967295, 0, false),
        }},
        {Scope::module, 5, 4, "Range test", {
            M(boolean, range_test, enabled, "Range test enabled", "", 0, 1, 0, false),
            M(integer, range_test, sender, "Send interval (seconds)", "0 receives only", 0, 4294967295, 0, false),
            M(boolean, range_test, save, "Save range test log", "", 0, 1, 0, false),
            M(boolean, range_test, clear_on_reboot, "Clear log on reboot", "", 0, 1, 0, false),
        }},
        {Scope::module, 6, 5, "Telemetry", {
            M(boolean, telemetry, device_telemetry_enabled, "Device telemetry enabled", "", 0, 1, 0, false),
            M(integer, telemetry, device_update_interval, "Device metrics interval (seconds)", "", 0, 4294967295, 0, false),
            M(boolean, telemetry, environment_measurement_enabled, "Environment measurements", "", 0, 1, 0, false),
            M(integer, telemetry, environment_update_interval, "Environment interval (seconds)", "", 0, 4294967295, 0, false),
            M(boolean, telemetry, environment_screen_enabled, "Environment display", "", 0, 1, 0, false),
            M(boolean, telemetry, environment_display_fahrenheit, "Show Fahrenheit", "", 0, 1, 0, false),
            M(boolean, telemetry, air_quality_enabled, "Air quality measurements", "", 0, 1, 0, false),
            M(integer, telemetry, air_quality_interval, "Air quality interval (seconds)", "", 0, 4294967295, 0, false),
            M(boolean, telemetry, power_measurement_enabled, "Power measurements", "", 0, 1, 0, false),
            M(integer, telemetry, power_update_interval, "Power interval (seconds)", "", 0, 4294967295, 0, false),
            M(boolean, telemetry, health_measurement_enabled, "Health measurements", "", 0, 1, 0, false),
            M(integer, telemetry, health_update_interval, "Health interval (seconds)", "", 0, 4294967295, 0, false),
        }},
        {Scope::module, 7, 6, "Canned messages", {
            M(boolean, canned_message, rotary1_enabled, "Rotary encoder enabled", "", 0, 1, 0, false),
            M(boolean, canned_message, updown1_enabled, "Up/down input enabled", "", 0, 1, 0, false),
            M(integer, canned_message, inputbroker_pin_a, "Encoder A GPIO", "", 0, 255, 0, false),
            M(integer, canned_message, inputbroker_pin_b, "Encoder B GPIO", "", 0, 255, 0, false),
            M(integer, canned_message, inputbroker_pin_press, "Button GPIO", "", 0, 255, 0, false),
            M(boolean, canned_message, send_bell, "Append alert bell", "", 0, 1, 0, false),
        }},
        {Scope::module, 8, 7, "Audio", {
            M(boolean, audio, codec2_enabled, "Codec2 voice enabled", "", 0, 1, 0, false),
            M(integer, audio, ptt_pin, "Push-to-talk GPIO", "", 0, 255, 0, false),
            ME(audio, bitrate, "Codec bitrate", ModuleConfig::AudioConfig, Audio_Baud),
            M(integer, audio, i2s_ws, "I2S word select GPIO", "", 0, 255, 0, false),
            M(integer, audio, i2s_sd, "I2S input GPIO", "", 0, 255, 0, false),
            M(integer, audio, i2s_din, "I2S output GPIO", "", 0, 255, 0, false),
            M(integer, audio, i2s_sck, "I2S clock GPIO", "", 0, 255, 0, false),
        }},
        {Scope::module, 9, 8, "Remote hardware", {
            M(boolean, remote_hardware, enabled, "Remote hardware enabled", "", 0, 1, 0, false),
            M(boolean, remote_hardware, allow_undefined_pin_access, "Allow access to undefined pins", "Configured pin definitions are preserved", 0, 1, 0, false),
        }},
        {Scope::module, 10, 9, "Neighbor information", {
            M(boolean, neighbor_info, enabled, "Neighbor information enabled", "", 0, 1, 0, false),
            M(integer, neighbor_info, update_interval, "Update interval (seconds)", "0 uses default; custom intervals must be at least 14400", 0, 4294967295, 0, false),
            M(boolean, neighbor_info, transmit_over_lora, "Broadcast over LoRa", "", 0, 1, 0, false),
        }},
        {Scope::module, 11, 10, "Ambient lighting", {
            M(boolean, ambient_lighting, led_state, "LED enabled", "", 0, 1, 0, false),
            M(integer, ambient_lighting, current, "LED current", "", 0, 255, 0, false),
            M(integer, ambient_lighting, red, "Red", "", 0, 255, 0, false),
            M(integer, ambient_lighting, green, "Green", "", 0, 255, 0, false),
            M(integer, ambient_lighting, blue, "Blue", "", 0, 255, 0, false),
        }},
        {Scope::module, 12, 11, "Detection sensor", {
            M(boolean, detection_sensor, enabled, "Detection sensor enabled", "", 0, 1, 0, false),
            M(text, detection_sensor, name, "Sensor name", "", 0, 0, 20, false),
            M(integer, detection_sensor, monitor_pin, "Sensor GPIO", "", 0, 255, 0, false),
            ME(detection_sensor, detection_trigger_type, "Trigger type", ModuleConfig::DetectionSensorConfig, TriggerType),
            M(integer, detection_sensor, minimum_broadcast_secs, "Minimum alert interval (seconds)", "", 0, 4294967295, 0, false),
            M(integer, detection_sensor, state_broadcast_secs, "State heartbeat interval (seconds)", "", 0, 4294967295, 0, false),
            M(boolean, detection_sensor, send_bell, "Append alert bell", "", 0, 1, 0, false),
            M(boolean, detection_sensor, use_pullup, "Enable pull-up resistor", "", 0, 1, 0, false),
        }},
        {Scope::module, 13, 12, "Paxcounter", {
            M(boolean, paxcounter, enabled, "Paxcounter enabled", "", 0, 1, 0, false),
            M(integer, paxcounter, paxcounter_update_interval, "Update interval (seconds)", "", 0, 4294967295, 0, false),
            M(integer, paxcounter, wifi_threshold, "Wi-Fi RSSI threshold (dBm)", "", -120, 0, 0, false),
            M(integer, paxcounter, ble_threshold, "Bluetooth RSSI threshold (dBm)", "", -120, 0, 0, false),
        }},
        {Scope::module, 14, 13, "Status message", {
            M(text, statusmessage, node_status, "Broadcast status message", "", 0, 0, 100, false),
        }},
        {Scope::module, 15, 14, "Traffic management", {
            M(integer, traffic_management, position_min_interval_secs, "Minimum position interval (seconds)", "0 disables position suppression", 0, 4294967295, 0, false),
            M(integer, traffic_management, nodeinfo_direct_response_max_hops, "Node information cache hop limit", "", 0, 7, 0, false),
            M(integer, traffic_management, rate_limit_window_secs, "Rate limit window (seconds)", "0 disables rate limiting", 0, 4294967295, 0, false),
            M(integer, traffic_management, rate_limit_max_packets, "Maximum packets per window", "", 0, 4294967295, 0, false),
            M(integer, traffic_management, unknown_packet_threshold, "Unknown packet threshold", "", 0, 4294967295, 0, false),
        }},
        {Scope::module, 16, 15, "TAK", {
            ME(tak, team, "Team", meshtastic, Team),
            ME(tak, role, "Team role", meshtastic, MemberRole),
        }},
        {Scope::module, 17, 16, "Mesh beacon", {
            scalar("listen", "Receive mesh beacons", "", FieldType::boolean, 0, 1, 0, false,
                [](const Config &, const ModuleConfig &c) { return (c.mesh_beacon().flags() & 1U) != 0; },
                [](Config &, ModuleConfig &c, bool v) { c.mutable_mesh_beacon()->set_flags((c.mesh_beacon().flags() & ~1U) | (v ? 1U : 0U)); }),
            scalar("broadcast", "Broadcast mesh beacons", "", FieldType::boolean, 0, 1, 0, false,
                [](const Config &, const ModuleConfig &c) { return (c.mesh_beacon().flags() & 2U) != 0; },
                [](Config &, ModuleConfig &c, bool v) { c.mutable_mesh_beacon()->set_flags((c.mesh_beacon().flags() & ~2U) | (v ? 2U : 0U)); }),
            scalar("legacy_text", "Send text separately for older devices", "", FieldType::boolean, 0, 1, 0, false,
                [](const Config &, const ModuleConfig &c) { return (c.mesh_beacon().flags() & 4U) != 0; },
                [](Config &, ModuleConfig &c, bool v) { c.mutable_mesh_beacon()->set_flags((c.mesh_beacon().flags() & ~4U) | (v ? 4U : 0U)); }),
            M(text, mesh_beacon, broadcast_message, "Beacon message", "", 0, 0, 100, false),
            M(integer, mesh_beacon, broadcast_interval_secs, "Broadcast interval (seconds)", "", 3600, 4294967295, 0, false),
            M(integer, mesh_beacon, broadcast_send_as_node, "Send as node number", "", 0, 4294967295, 0, false),
        }},
    };
    return categories;
}
#undef D
#undef M
#undef DE
#undef ME
bool validate(const Field &field, const std::string &value, std::string &error)
{
    error.clear();
    if (field.type == FieldType::text) {
        if (value.size() > field.maximum_bytes) error = field.label + " must be at most " + std::to_string(field.maximum_bytes) + " bytes";
        else if (std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; })) error = field.label + " must be a single line of text";
    } else if (field.type == FieldType::boolean) {
        if (value != "true" && value != "false") error = "Choose on or off for " + field.label;
    } else {
        if (value.empty() || std::any_of(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); })) {
            error = "Enter a number for " + field.label;
            return false;
        }
        char *end = nullptr;
        errno = 0;
        const double numeric = field.type == FieldType::decimal ? std::strtod(value.c_str(), &end) : static_cast<double>(std::strtoll(value.c_str(), &end, 10));
        if (errno == ERANGE || end != value.c_str() + value.size() || !std::isfinite(numeric)) {
            error = "Enter a valid " + std::string(field.type == FieldType::decimal ? "number" : "whole number") + " for " + field.label;
        } else if (field.type == FieldType::enumeration) {
            const auto found = std::find_if(field.choices.begin(), field.choices.end(), [&](const Choice &c) { return numeric == c.value; });
            if (found == field.choices.end()) error = "Choose a supported value for " + field.label;
        } else if (numeric < field.minimum || numeric > field.maximum) {
            std::ostringstream range;
            range << field.label << " must be between " << field.minimum << " and " << field.maximum;
            error = range.str();
        } else if (field.id == "update_interval" && numeric != 0 && numeric < 14400) error = "Neighbor update interval must be 0 or at least 14400 seconds";
    }
    return error.empty();
}
bool Draft::load(const Category &category, const Config &config)
{
    if (category.scope != Scope::device || config.payload_variant_case() != category.payload_case) return false;
    category_ = &category;
    device_ = original_device_ = config;
    module_.Clear(); original_module_.Clear();
    return true;
}
bool Draft::load(const Category &category, const ModuleConfig &config)
{
    if (category.scope != Scope::module || config.payload_variant_case() != category.payload_case) return false;
    category_ = &category;
    module_ = original_module_ = config;
    device_.Clear(); original_device_.Clear();
    return true;
}
bool Draft::update(std::size_t index, const std::string &value, std::string &error)
{
    if (!category_ || index >= category_->fields.size()) { error = "Read this configuration from the device before editing"; return false; }
    const auto &field = category_->fields[index];
    if (!validate(field, value, error)) return false;
    field.write(device_, module_, value);
    return true;
}
void Draft::discard() { device_ = original_device_; module_ = original_module_; }
void Draft::mark_saved() { original_device_ = device_; original_module_ = module_; }
bool Draft::loaded() const noexcept { return category_ != nullptr; }
bool Draft::dirty() const
{
    return category_ && (device_.SerializeAsString() != original_device_.SerializeAsString() || module_.SerializeAsString() != original_module_.SerializeAsString());
}
const Category *Draft::category() const noexcept { return category_; }
std::string Draft::value(std::size_t index) const
{
    return category_ && index < category_->fields.size() ? category_->fields[index].read(device_, module_) : std::string{};
}
const Config &Draft::device_config() const noexcept { return device_; }
const ModuleConfig &Draft::module_config() const noexcept { return module_; }
bool Draft::matches(const Config &config) const
{
    if (!category_ || category_->scope != Scope::device || config.payload_variant_case() != category_->payload_case) return false;
    return std::all_of(category_->fields.begin(), category_->fields.end(), [&](const Field &field) { return field.read(config, module_) == field.read(device_, module_); });
}
bool Draft::matches(const ModuleConfig &config) const
{
    if (!category_ || category_->scope != Scope::module || config.payload_variant_case() != category_->payload_case) return false;
    return std::all_of(category_->fields.begin(), category_->fields.end(), [&](const Field &field) { return field.read(device_, config) == field.read(device_, module_); });
}
}  // namespace meshtastic::configuration
