#ifndef LILYGO_UI_MESHTASTIC_CONNECTION_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_CONNECTION_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

class ConnectionViewModel {
public:
    explicit ConnectionViewModel(meshtastic::MeshtasticClient *client = nullptr);

    ConnectionViewModel(const ConnectionViewModel &)            = delete;
    ConnectionViewModel &operator=(const ConnectionViewModel &) = delete;
    ConnectionViewModel(ConnectionViewModel &&)                 = delete;
    ConnectionViewModel &operator=(ConnectionViewModel &&)      = delete;

    void set_client(meshtastic::MeshtasticClient *client) noexcept;
    void sync(const meshtastic::ClientSnapshot &snapshot, bool connection_enabled);
    bool toggle_connection();

    [[nodiscard]] int brightness() const;
    [[nodiscard]] bool connected() const;

    [[nodiscard]] lv_subject_t *brightness_subject() noexcept;
    [[nodiscard]] lv_subject_t *connected_subject() noexcept;
    [[nodiscard]] lv_subject_t *config_complete_subject() noexcept;
    [[nodiscard]] lv_subject_t *has_activity_subject() noexcept;
    [[nodiscard]] lv_subject_t *message_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *node_count_subject() noexcept;
    [[nodiscard]] lv_subject_t *action_subject() noexcept;
    [[nodiscard]] lv_subject_t *auto_reconnect_subject() noexcept;
    [[nodiscard]] lv_subject_t *config_status_subject() noexcept;
    [[nodiscard]] lv_subject_t *device_detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *device_name_subject() noexcept;
    [[nodiscard]] lv_subject_t *device_role_subject() noexcept;
    [[nodiscard]] lv_subject_t *firmware_subject() noexcept;
    [[nodiscard]] lv_subject_t *endpoint_subject() noexcept;
    [[nodiscard]] lv_subject_t *last_activity_detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *last_activity_title_subject() noexcept;
    [[nodiscard]] lv_subject_t *primary_channel_subject() noexcept;
    [[nodiscard]] lv_subject_t *status_subject() noexcept;
    [[nodiscard]] lv_subject_t *tcp_state_subject() noexcept;
    [[nodiscard]] lv_subject_t *uptime_subject() noexcept;
    [[nodiscard]] lv_subject_t *online_nodes_subject() noexcept;
    [[nodiscard]] lv_subject_t *lora_band_subject() noexcept;
    [[nodiscard]] lv_subject_t *node_id_subject() noexcept;

private:
    meshtastic::MeshtasticClient *client_ = nullptr;
    IntSubject brightness_;
    IntSubject connected_;
    IntSubject config_complete_;
    IntSubject has_activity_;
    IntSubject message_count_;
    IntSubject node_count_;
    StringSubject<24> action_;
    StringSubject<32> auto_reconnect_;
    StringSubject<32> config_status_;
    StringSubject<256> device_detail_;
    StringSubject<96> device_name_;
    StringSubject<48> device_role_;
    StringSubject<64> firmware_;
    StringSubject<128> endpoint_;
    StringSubject<160> last_activity_detail_;
    StringSubject<128> last_activity_title_;
    StringSubject<96> primary_channel_;
    StringSubject<192> status_;
    StringSubject<40> tcp_state_;
    StringSubject<48> uptime_;
    StringSubject<32> online_nodes_;
    StringSubject<96> lora_band_;
    StringSubject<32> node_id_;
};

#endif
