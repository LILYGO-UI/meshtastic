#ifndef LILYGO_UI_MESHTASTIC_NODE_DETAIL_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_NODE_DETAIL_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct NodeDetailPresentation {
    bool found        = false;
    bool has_metrics  = false;
    bool has_position = false;
    std::string initials;
    std::string name;
    std::string presence;
    std::string battery;
    std::string voltage;
    std::string coordinates;
    std::string altitude;
    std::string id;
    std::string number;
    std::string signal;
    std::string role;
    std::string route;
    std::string last_heard;
    std::vector<std::string> telemetry_history;
    std::vector<std::string> route_history;
    std::size_t position_count = 0;
    bool position_history_limited = false;
    double latitude = 0.0;
    double longitude = 0.0;
};

class NodeDetailViewModel {
public:
    explicit NodeDetailViewModel(meshtastic::MeshtasticClient *client = nullptr);

    NodeDetailViewModel(const NodeDetailViewModel &)            = delete;
    NodeDetailViewModel &operator=(const NodeDetailViewModel &) = delete;
    NodeDetailViewModel(NodeDetailViewModel &&)                 = delete;
    NodeDetailViewModel &operator=(NodeDetailViewModel &&)      = delete;

    void select_node(std::uint32_t number, const meshtastic::ClientSnapshot &snapshot);
    void sync(const meshtastic::ClientSnapshot &snapshot);
    bool request_telemetry();
    bool request_traceroute();
    bool share_waypoint(std::string name, std::string description, double latitude, double longitude,
                        std::uint32_t lifetime_seconds, std::uint32_t channel);
    [[nodiscard]] const std::vector<meshtastic::ChannelInfo> &channels() const noexcept;
    [[nodiscard]] lv_subject_t *connected_subject() noexcept;
    [[nodiscard]] lv_subject_t *action_status_subject() noexcept;

    [[nodiscard]] std::uint32_t selected_node() const noexcept;
    [[nodiscard]] const NodeDetailPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;

private:
    void publish_content();

    IntSubject content_revision_;
    std::uint32_t selected_node_ = 0;
    NodeDetailPresentation presentation_;
    meshtastic::MeshtasticClient *client_ = nullptr;
    meshtastic::ClientSnapshot snapshot_;
    std::uint32_t history_node_ = 0, history_radio_ = 0;
    std::int64_t latest_position_id_ = 0, latest_telemetry_id_ = 0, latest_route_id_ = 0;
    std::vector<meshtastic::PositionRecord> position_history_;
    std::vector<meshtastic::TelemetryRecord> telemetry_history_;
    std::vector<meshtastic::RouteRecord> route_history_;
    IntSubject connected_{0, 0, 1};
    StringSubject<192> action_status_{""};
};

#endif
