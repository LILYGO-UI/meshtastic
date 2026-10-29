#ifndef LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_DEVICE_TOOLS_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "configuration/configuration_catalog.hpp"
#include "configuration/admin_transaction.hpp"
#include "domain/meshtastic_client.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>
#include <string>

class DeviceToolsViewModel {
public:
    explicit DeviceToolsViewModel(meshtastic::MeshtasticClient *client = nullptr);

    DeviceToolsViewModel(const DeviceToolsViewModel &)            = delete;
    DeviceToolsViewModel &operator=(const DeviceToolsViewModel &) = delete;
    DeviceToolsViewModel(DeviceToolsViewModel &&)                 = delete;
    DeviceToolsViewModel &operator=(DeviceToolsViewModel &&)      = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot, bool connection_enabled);
    void set_client(meshtastic::MeshtasticClient *client) noexcept;
    bool request_full_config();
    bool request_owner();
    bool request_session_key();
    bool save_configuration(std::int32_t region, std::int32_t modem_preset, std::int32_t device_role,
                            std::string owner_name);

    const std::vector<meshtastic::configuration::Category> &configuration_categories() const;
    std::size_t selected_category_index() const noexcept;
    const meshtastic::configuration::Draft &selected_draft() const;
    void select_category(std::size_t index);
    void set_editor_active(bool active);
    bool edit_field(std::size_t field_index, const std::string &value);
    std::string field_value(std::size_t field_index) const;
    bool refresh_selected_configuration();
    bool save_selected_configuration();
    void discard_selected_configuration();
    enum class DeviceAction { reboot, shutdown, reset_node_database };
    bool perform_device_action(DeviceAction action);
    [[nodiscard]] lv_subject_t *configuration_content_subject() noexcept;
    [[nodiscard]] lv_subject_t *configuration_status_subject() noexcept;
    [[nodiscard]] lv_subject_t *configuration_ready_subject() noexcept;
    [[nodiscard]] lv_subject_t *radio_identity_subject() noexcept;
    [[nodiscard]] lv_subject_t *legacy_form_subject() noexcept;
    [[nodiscard]] lv_subject_t *legacy_ready_subject() noexcept;

    [[nodiscard]] std::int32_t selected_region() const noexcept;
    [[nodiscard]] std::int32_t selected_modem_preset() const noexcept;
    [[nodiscard]] std::int32_t selected_device_role() const noexcept;

    [[nodiscard]] lv_subject_t *connected_subject() noexcept;
    [[nodiscard]] lv_subject_t *auto_reconnect_subject() noexcept;
    [[nodiscard]] lv_subject_t *config_subject() noexcept;
    [[nodiscard]] lv_subject_t *detail_subject() noexcept;
    [[nodiscard]] lv_subject_t *endpoint_subject() noexcept;
    [[nodiscard]] lv_subject_t *firmware_subject() noexcept;
    [[nodiscard]] lv_subject_t *radio_region_subject() noexcept;
    [[nodiscard]] lv_subject_t *modem_preset_subject() noexcept;
    [[nodiscard]] lv_subject_t *device_role_subject() noexcept;
    [[nodiscard]] lv_subject_t *owner_name_subject() noexcept;
    [[nodiscard]] lv_subject_t *edit_status_subject() noexcept;
    [[nodiscard]] lv_subject_t *state_subject() noexcept;

private:
    struct PendingSave {
        meshtastic::configuration::Draft expected;
        meshtastic::configuration::AdminTransaction transaction;
    };
    struct CategoryState {
        meshtastic::configuration::Draft draft;
        std::optional<PendingSave> pending;
        std::map<std::size_t, std::string> errors;
        std::map<std::size_t, std::string> raw_values;
        std::uint64_t loaded_revision = 0;
        std::uint64_t refresh_base_revision = 0;
        bool refresh_requested = false;
        bool editor_active = false;
        std::string status = "Read this category from the radio before editing";
    };
    bool can_read();
    bool can_write();
    void sync_configurations();
    void publish_configuration_status();
    std::uint64_t category_revision(const meshtastic::configuration::Category &category) const;
    meshtastic::MeshtasticClient *client_ = nullptr;
    std::vector<CategoryState> categories_;
    std::size_t selected_category_ = 0;
    IntSubject configuration_content_{0};
    IntSubject configuration_ready_{0, 0, 1};
    IntSubject radio_identity_{0};
    IntSubject legacy_form_{0};
    IntSubject legacy_ready_{0, 0, 1};
    StringSubject<256> configuration_status_;
    IntSubject connected_;
    StringSubject<16> auto_reconnect_;
    StringSubject<16> config_;
    StringSubject<256> detail_;
    StringSubject<128> endpoint_;
    StringSubject<64> firmware_;
    StringSubject<64> radio_region_;
    StringSubject<64> modem_preset_;
    StringSubject<48> device_role_;
    StringSubject<96> owner_name_;
    StringSubject<128> edit_status_;
    StringSubject<40> state_;
    meshtastic::ClientSnapshot snapshot_;
    std::uint32_t radio_number_ = 0;
    bool legacy_initialized_ = false;
    meshtastic::configuration::AdminTransaction legacy_transaction_;
};

#endif
