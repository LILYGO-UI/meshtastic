#include "pages/device_tools/device_tools_view_model.hpp"

#include <algorithm>
#include <cstdio>

DeviceToolsViewModel::DeviceToolsViewModel(meshtastic::MeshtasticClient *client)
    : client_(client),
      connected_(0, 0, 1),
      auto_reconnect_("Off"),
      config_("Pending"),
      detail_("Waiting for meshtasticd"),
      endpoint_("Not configured"),
      firmware_("Unavailable"),
      radio_region_("Unavailable"),
      modem_preset_("Unavailable"),
      device_role_("Unavailable"),
      owner_name_(""),
      edit_status_("No pending changes"),
      state_("Stopped")
{
    categories_.resize(meshtastic::configuration::catalog().size());
    publish_configuration_status();
}

void DeviceToolsViewModel::set_client(meshtastic::MeshtasticClient *client) noexcept
{
    if (client_ != client) {
        legacy_transaction_.reset();
        for (auto &category : categories_) category.pending.reset();
    }
    client_ = client;
}

bool DeviceToolsViewModel::request_full_config()
{
    return client_ && client_->request_full_config();
}

bool DeviceToolsViewModel::request_owner()
{
    return client_ && client_->request_owner();
}

bool DeviceToolsViewModel::request_session_key()
{
    if (!client_) return false;
    const bool queued = client_->request_session_key();
    edit_status_.set(queued ? "Admin key requested; wait for the radio response"
                            : "Unable to request an admin key");
    return queued;
}

bool DeviceToolsViewModel::save_configuration(std::int32_t region, std::int32_t modem_preset,
                                               std::int32_t device_role, std::string owner_name)
{
    if (!legacy_initialized_) {
        edit_status_.set("Read this radio's LoRa, device and owner settings before saving");
        return false;
    }
    if (!can_write()) {
        edit_status_.set(lv_subject_get_string(configuration_status_subject()));
        return false;
    }
    if (!meshtastic::Config::LoRaConfig::RegionCode_IsValid(region) ||
        !meshtastic::Config::LoRaConfig::ModemPreset_IsValid(modem_preset) ||
        !meshtastic::Config::DeviceConfig::Role_IsValid(device_role)) {
        edit_status_.set("Invalid configuration selection");
        return false;
    }
    if (owner_name.empty() || owner_name.size() > 24) {
        edit_status_.set("Node name must contain 1-24 bytes");
        return false;
    }
    if (snapshot_.session_passkey.empty()) {
        request_session_key();
        edit_status_.set("Admin key required; request it, then save again");
        return false;
    }

    // Config uses a protobuf oneof: LoRa and Device settings must be sent as
    // separate AdminMessage.set_config requests or one would replace the
    // other in the serialized message.
    meshtastic::Config lora_config;
    const auto lora = snapshot_.device_configs.find(meshtastic::Config::kLora);
    if (lora != snapshot_.device_configs.end()) lora_config = lora->second;
    else if (snapshot_.lora_config) lora_config.mutable_lora()->CopyFrom(*snapshot_.lora_config);
    else if (snapshot_.config && snapshot_.config->has_lora()) lora_config = *snapshot_.config;
    if (!lora_config.has_lora()) {
        client_->request_config(meshtastic::AdminMessage::LORA_CONFIG);
        edit_status_.set("Read LoRa configuration before saving");
        return false;
    }
    lora_config.mutable_lora()->set_region(static_cast<meshtastic::Config::LoRaConfig::RegionCode>(region));
    lora_config.mutable_lora()->set_modem_preset(
        static_cast<meshtastic::Config::LoRaConfig::ModemPreset>(modem_preset));

    meshtastic::Config device_config;
    const auto device = snapshot_.device_configs.find(meshtastic::Config::kDevice);
    if (device != snapshot_.device_configs.end()) device_config = device->second;
    else if (snapshot_.device_config) device_config.mutable_device()->CopyFrom(*snapshot_.device_config);
    else if (snapshot_.config && snapshot_.config->has_device()) device_config = *snapshot_.config;
    if (!device_config.has_device() || !snapshot_.owner) {
        client_->request_config(meshtastic::AdminMessage::DEVICE_CONFIG);
        client_->request_owner();
        edit_status_.set("Read device configuration and owner before saving");
        return false;
    }
    device_config.mutable_device()->set_role(static_cast<meshtastic::Config::DeviceConfig::Role>(device_role));

    meshtastic::User owner;
    if (snapshot_.owner) owner = *snapshot_.owner;
    if (owner.id().empty() && snapshot_.my_node_number != 0) {
        char id[10]{};
        std::snprintf(id, sizeof(id), "!%08x", snapshot_.my_node_number);
        owner.set_id(id);
    }
    owner.set_long_name(std::move(owner_name));
    owner.set_role(static_cast<meshtastic::Config::DeviceConfig::Role>(device_role));
    meshtastic::AdminMessage begin, lora_write, device_write, owner_write, commit;
    begin.set_begin_edit_settings(true);
    lora_write.mutable_set_config()->CopyFrom(lora_config);
    device_write.mutable_set_config()->CopyFrom(device_config);
    owner_write.mutable_set_owner()->CopyFrom(owner);
    commit.set_commit_edit_settings(true);
    meshtastic::AdminMessage lora_get, device_get, owner_get;
    lora_get.set_get_config_request(meshtastic::AdminMessage::LORA_CONFIG);
    device_get.set_get_config_request(meshtastic::AdminMessage::DEVICE_CONFIG);
    owner_get.set_get_owner_request(true);
    meshtastic::AdminMessage lora_expected, device_expected, owner_expected;
    lora_expected.mutable_get_config_response()->CopyFrom(lora_config);
    device_expected.mutable_get_config_response()->CopyFrom(device_config);
    owner_expected.mutable_get_owner_response()->CopyFrom(owner);
    const auto lora_matches = [](const meshtastic::AdminMessage &expected, const meshtastic::AdminMessage &actual) {
        return actual.has_get_config_response() && actual.get_config_response().has_lora() &&
               actual.get_config_response().lora().region() == expected.get_config_response().lora().region() &&
               actual.get_config_response().lora().modem_preset() == expected.get_config_response().lora().modem_preset();
    };
    const auto device_matches = [](const meshtastic::AdminMessage &expected, const meshtastic::AdminMessage &actual) {
        return actual.has_get_config_response() && actual.get_config_response().has_device() &&
               actual.get_config_response().device().role() == expected.get_config_response().device().role();
    };
    const auto owner_matches = [](const meshtastic::AdminMessage &expected, const meshtastic::AdminMessage &actual) {
        return actual.has_get_owner_response() && actual.get_owner_response().long_name() == expected.get_owner_response().long_name() &&
               actual.get_owner_response().role() == expected.get_owner_response().role();
    };
    std::string error;
    const bool queued = legacy_transaction_.start(*client_, {begin, lora_write, device_write, owner_write, commit},
        {{lora_get, lora_expected, lora_matches}, {device_get, device_expected, device_matches},
         {owner_get, owner_expected, owner_matches}}, error, snapshot_.my_node_number);
    edit_status_.set(queued ? "Update queued; waiting for acknowledgements and device readback" : error.c_str());
    return queued;
}

std::int32_t DeviceToolsViewModel::selected_region() const noexcept
{
    if (snapshot_.lora_config) return snapshot_.lora_config->region();
    if (snapshot_.config && snapshot_.config->has_lora()) return snapshot_.config->lora().region();
    return meshtastic::Config::LoRaConfig::UNSET;
}

std::int32_t DeviceToolsViewModel::selected_modem_preset() const noexcept
{
    if (snapshot_.lora_config) return snapshot_.lora_config->modem_preset();
    if (snapshot_.config && snapshot_.config->has_lora()) return snapshot_.config->lora().modem_preset();
    return meshtastic::Config::LoRaConfig::LONG_FAST;
}

std::int32_t DeviceToolsViewModel::selected_device_role() const noexcept
{
    if (snapshot_.device_config) return snapshot_.device_config->role();
    if (snapshot_.config && snapshot_.config->has_device()) return snapshot_.config->device().role();
    if (snapshot_.owner) return snapshot_.owner->role();
    return meshtastic::Config::DeviceConfig::CLIENT;
}

void DeviceToolsViewModel::sync(const meshtastic::ClientSnapshot &snapshot, bool connection_enabled)
{
    const bool changed_radio = snapshot.my_node_number != 0 && snapshot.my_node_number != radio_number_;
    if (changed_radio) {
        radio_number_ = snapshot.my_node_number;
        categories_.clear();
        categories_.resize(meshtastic::configuration::catalog().size());
        legacy_transaction_.reset();
        legacy_initialized_ = false;
        edit_status_.set("Radio changed; read its settings before saving");
    }
    snapshot_ = snapshot;
    connected_.set(snapshot.state == meshtastic::ConnectionState::connected ? 1 : 0);
    auto_reconnect_.set(connection_enabled ? "On" : "Off");
    config_.set(snapshot.config_complete ? "Complete" : "Pending");
    detail_.set(snapshot.error.empty() ? snapshot.endpoint.c_str() : snapshot.error.c_str());
    endpoint_.set(snapshot.endpoint.c_str());
    firmware_.set(snapshot.metadata && !snapshot.metadata->firmware_version().empty()
                      ? snapshot.metadata->firmware_version().c_str()
                      : "Unavailable");
    if (snapshot.lora_config) {
        radio_region_.set(meshtastic::Config_LoRaConfig_RegionCode_Name(snapshot.lora_config->region()).c_str());
        modem_preset_.set(meshtastic::Config_LoRaConfig_ModemPreset_Name(snapshot.lora_config->modem_preset()).c_str());
    } else if (snapshot.config && snapshot.config->has_lora()) {
        radio_region_.set(meshtastic::Config_LoRaConfig_RegionCode_Name(snapshot.config->lora().region()).c_str());
        modem_preset_.set(meshtastic::Config_LoRaConfig_ModemPreset_Name(snapshot.config->lora().modem_preset()).c_str());
    } else {
        radio_region_.set("Unavailable");
        modem_preset_.set("Unavailable");
    }
    const auto local_node = std::find_if(snapshot.nodes.begin(), snapshot.nodes.end(), [&](const meshtastic::Node &node) {
        return node.number == snapshot.my_node_number;
    });
    if (local_node != snapshot.nodes.end() && local_node->role != 0) {
        device_role_.set(meshtastic::Config_DeviceConfig_Role_Name(
                              static_cast<meshtastic::Config_DeviceConfig_Role>(local_node->role))
                              .c_str());
    } else if (snapshot.device_config) {
        device_role_.set(meshtastic::Config_DeviceConfig_Role_Name(snapshot.device_config->role()).c_str());
    } else if (snapshot.config && snapshot.config->has_device()) {
        device_role_.set(meshtastic::Config_DeviceConfig_Role_Name(snapshot.config->device().role()).c_str());
    } else if (snapshot.owner) {
        device_role_.set(meshtastic::Config_DeviceConfig_Role_Name(snapshot.owner->role()).c_str());
    } else {
        device_role_.set("Unavailable");
    }
    if (snapshot.owner && !snapshot.owner->long_name().empty()) {
        owner_name_.set(snapshot.owner->long_name().c_str());
    } else {
        owner_name_.set("");
    }
    state_.set(meshtastic::connection_state_name(snapshot.state));
    if (changed_radio) {
        radio_identity_.set(radio_identity_.value() + 1);
        configuration_content_.set(configuration_content_.value() + 1);
        legacy_form_.set(legacy_form_.value() + 1);
    }
    if (!legacy_initialized_ && snapshot.my_node_number != 0 && snapshot.config_complete &&
        snapshot.lora_config && snapshot.device_config && snapshot.owner) {
        legacy_initialized_ = true;
        legacy_form_.set(legacy_form_.value() + 1);
    }
    legacy_ready_.set(legacy_initialized_ && snapshot.my_node_number == radio_number_ &&
                      snapshot.state == meshtastic::ConnectionState::connected && snapshot.config_complete ? 1 : 0);
    if (client_ && legacy_transaction_.state() == meshtastic::configuration::AdminTransaction::State::waiting) {
        std::string error;
        const auto result = legacy_transaction_.advance(*client_, snapshot, error);
        if (result == meshtastic::configuration::AdminTransaction::State::confirmed) edit_status_.set("Saved and confirmed by the radio");
        else if (result == meshtastic::configuration::AdminTransaction::State::failed) edit_status_.set(error.c_str());
    }
    sync_configurations();
}

#define SUBJECT_GETTER(name)                                      \
    lv_subject_t *DeviceToolsViewModel::name##_subject() noexcept \
    {                                                             \
        return name##_.get();                                     \
    }

SUBJECT_GETTER(connected)
SUBJECT_GETTER(auto_reconnect)
SUBJECT_GETTER(config)
SUBJECT_GETTER(detail)
SUBJECT_GETTER(endpoint)
SUBJECT_GETTER(firmware)
SUBJECT_GETTER(radio_region)
SUBJECT_GETTER(modem_preset)
SUBJECT_GETTER(device_role)
SUBJECT_GETTER(owner_name)
SUBJECT_GETTER(edit_status)
SUBJECT_GETTER(state)

#undef SUBJECT_GETTER

const std::vector<meshtastic::configuration::Category> &DeviceToolsViewModel::configuration_categories() const
{
    return meshtastic::configuration::catalog();
}
std::size_t DeviceToolsViewModel::selected_category_index() const noexcept { return selected_category_; }
const meshtastic::configuration::Draft &DeviceToolsViewModel::selected_draft() const
{
    return categories_[selected_category_].draft;
}
void DeviceToolsViewModel::select_category(std::size_t index)
{
    if (index >= categories_.size() || index == selected_category_) return;
    categories_[selected_category_].editor_active = false;
    selected_category_ = index;
    sync_configurations();
    configuration_content_.set(configuration_content_.value() + 1);
    publish_configuration_status();
}
void DeviceToolsViewModel::set_editor_active(bool active) { categories_[selected_category_].editor_active = active; }
std::uint64_t DeviceToolsViewModel::category_revision(const meshtastic::configuration::Category &category) const
{
    const auto &revisions = category.scope == meshtastic::configuration::Scope::device
                                ? snapshot_.device_config_revisions : snapshot_.module_config_revisions;
    const auto found = revisions.find(category.payload_case);
    return found != revisions.end() ? found->second : 0;
}
void DeviceToolsViewModel::publish_configuration_status()
{
    const auto &state = categories_[selected_category_];
    configuration_status_.set(state.status.c_str());
    configuration_ready_.set(state.draft.loaded() && snapshot_.my_node_number != 0 &&
                             snapshot_.my_node_number == radio_number_ && snapshot_.state == meshtastic::ConnectionState::connected &&
                             snapshot_.config_complete ? 1 : 0);
}
bool DeviceToolsViewModel::can_read()
{
    if (!client_ || snapshot_.my_node_number == 0 || snapshot_.my_node_number != radio_number_ ||
        snapshot_.state != meshtastic::ConnectionState::connected || !snapshot_.config_complete) {
        categories_[selected_category_].status = "Connect and finish synchronization before requesting settings";
        publish_configuration_status();
        return false;
    }
    return true;
}
bool DeviceToolsViewModel::can_write()
{
    if (!can_read()) return false;
    if (snapshot_.session_passkey.empty()) {
        client_->request_session_key();
        categories_[selected_category_].status = "Admin key requested; wait for the radio response, then save again";
        publish_configuration_status();
        return false;
    }
    return true;
}
bool DeviceToolsViewModel::edit_field(std::size_t index, const std::string &value)
{
    auto &state = categories_[selected_category_];
    std::string error;
    const bool valid = state.draft.update(index, value, error);
    state.raw_values[index] = value;
    if (valid) {
        state.errors.erase(index);
        state.status = state.draft.dirty() ? "Unsaved changes; save this category to apply" : "Matches the last radio response";
    } else {
        state.errors[index] = error;
        state.status = error;
    }
    publish_configuration_status();
    return valid;
}
std::string DeviceToolsViewModel::field_value(std::size_t index) const
{
    const auto &state = categories_[selected_category_];
    const auto raw = state.raw_values.find(index);
    return raw == state.raw_values.end() ? state.draft.value(index) : raw->second;
}
bool DeviceToolsViewModel::refresh_selected_configuration()
{
    if (!can_read()) return false;
    const auto &category = configuration_categories()[selected_category_];
    auto &state = categories_[selected_category_];
    const bool queued = category.scope == meshtastic::configuration::Scope::device
                            ? client_->request_config(static_cast<meshtastic::AdminMessage::ConfigType>(category.admin_type))
                            : client_->request_module_config(static_cast<meshtastic::AdminMessage::ModuleConfigType>(category.admin_type));
    state.refresh_requested = queued;
    state.refresh_base_revision = category_revision(category);
    state.status = queued ? (state.draft.dirty() || state.editor_active
                                ? "Refresh requested; your draft will be retained"
                                : "Refresh requested; waiting for the radio response")
                          : "Unable to queue configuration request";
    publish_configuration_status();
    return queued;
}
bool DeviceToolsViewModel::save_selected_configuration()
{
    auto &state = categories_[selected_category_];
    if (!state.draft.loaded()) {
        state.status = "Read this category from the radio before saving";
        publish_configuration_status();
        return false;
    }
    if (!state.errors.empty()) {
        state.status = state.errors.begin()->second;
        publish_configuration_status();
        return false;
    }
    if (!can_write()) return false;
    if (state.pending) {
        state.status = "A save is awaiting device verification; refresh this category";
        publish_configuration_status();
        return false;
    }
    const auto &category = configuration_categories()[selected_category_];
    meshtastic::AdminMessage write, request, expected;
    if (category.scope == meshtastic::configuration::Scope::device) {
        write.mutable_set_config()->CopyFrom(state.draft.device_config());
        request.set_get_config_request(static_cast<meshtastic::AdminMessage::ConfigType>(category.admin_type));
        expected.mutable_get_config_response()->CopyFrom(state.draft.device_config());
    } else {
        write.mutable_set_module_config()->CopyFrom(state.draft.module_config());
        request.set_get_module_config_request(static_cast<meshtastic::AdminMessage::ModuleConfigType>(category.admin_type));
        expected.mutable_get_module_config_response()->CopyFrom(state.draft.module_config());
    }
    const auto expected_draft = state.draft;
    const auto matches = [expected_draft](const meshtastic::AdminMessage &, const meshtastic::AdminMessage &actual) {
        return actual.has_get_config_response() ? expected_draft.matches(actual.get_config_response())
             : actual.has_get_module_config_response() && expected_draft.matches(actual.get_module_config_response());
    };
    state.pending.emplace();
    state.pending->expected = expected_draft;
    std::string error;
    const bool queued = state.pending->transaction.start(*client_, {write}, {{request, expected, matches}}, error,
                                                         snapshot_.my_node_number);
    if (!queued) state.pending.reset();
    state.status = queued ? "Save queued; waiting for acknowledgement and device readback" : error;
    publish_configuration_status();
    return queued;
}
void DeviceToolsViewModel::discard_selected_configuration()
{
    auto &state = categories_[selected_category_];
    state.draft.discard();
    state.errors.clear();
    state.raw_values.clear();
    state.editor_active = false;
    state.status = state.pending ? "Draft discarded; queued save still awaits device verification" : "Draft discarded";
    configuration_content_.set(configuration_content_.value() + 1);
    publish_configuration_status();
}
void DeviceToolsViewModel::sync_configurations()
{
    using meshtastic::configuration::Scope;
    for (std::size_t index = 0; index < categories_.size(); ++index) {
        const auto &category = configuration_categories()[index];
        auto &state = categories_[index];
        const auto device = snapshot_.device_configs.find(category.payload_case);
        const auto module = snapshot_.module_configs.find(category.payload_case);
        const auto revision = category_revision(category);
        const bool available = category.scope == Scope::device ? device != snapshot_.device_configs.end()
                                                               : module != snapshot_.module_configs.end();
        bool changed = false;
        const auto load = [&] {
            if (category.scope == Scope::device) state.draft.load(category, device->second);
            else state.draft.load(category, module->second);
            state.loaded_revision = revision;
            state.errors.clear();
            state.raw_values.clear();
            changed = true;
        };
        if (state.pending) {
            std::string error;
            const auto outcome = state.pending->transaction.advance(*client_, snapshot_, error);
            if (outcome == meshtastic::configuration::AdminTransaction::State::waiting) continue;
            if (outcome == meshtastic::configuration::AdminTransaction::State::confirmed) {
                const bool newer_draft = !state.errors.empty() || (category.scope == Scope::device
                    ? state.draft.device_config().SerializeAsString() != state.pending->expected.device_config().SerializeAsString()
                    : state.draft.module_config().SerializeAsString() != state.pending->expected.module_config().SerializeAsString());
                if (!newer_draft && !state.editor_active && available) load();
                else if (!newer_draft) {
                    state.draft.mark_saved();
                    state.loaded_revision = revision;
                }
                state.status = newer_draft ? "Saved and confirmed; your newer draft is retained" : "Saved and confirmed by the device";
                state.pending.reset();
            } else {
                state.status = error + "; your draft is retained";
                state.pending.reset();
            }
        } else if (!available) {
            continue;
        } else if (!state.draft.loaded()) {
            load();
            state.status = "Loaded from the radio; edit and save this category";
        } else if (revision > state.loaded_revision && !state.pending) {
            if (!state.draft.dirty() && !state.editor_active && state.errors.empty()) {
                load();
                state.status = "Refreshed from the radio";
            } else if (state.refresh_requested && revision > state.refresh_base_revision) {
                state.status = "Radio refreshed; your unsaved draft is retained";
            }
        }
        if (state.refresh_requested && revision > state.refresh_base_revision) state.refresh_requested = false;
        if (changed && index == selected_category_) configuration_content_.set(configuration_content_.value() + 1);
    }
    publish_configuration_status();
}
bool DeviceToolsViewModel::perform_device_action(DeviceAction action)
{
    if (!can_write()) return false;
    meshtastic::AdminMessage request;
    const char *name = "Command";
    switch (action) {
        case DeviceAction::reboot: request.set_reboot_seconds(2); name = "Reboot"; break;
        case DeviceAction::shutdown: request.set_shutdown_seconds(2); name = "Shutdown"; break;
        case DeviceAction::reset_node_database: request.set_nodedb_reset(true); name = "Node database reset"; break;
    }
    request.set_session_passkey(snapshot_.session_passkey);
    const bool queued = client_->send_admin(request);
    const auto status = std::string(name) + (queued ? " queued; awaiting device response" : " could not be queued");
    categories_[selected_category_].status = status;
    edit_status_.set(status.c_str());
    publish_configuration_status();
    return queued;
}
lv_subject_t *DeviceToolsViewModel::configuration_content_subject() noexcept { return configuration_content_.get(); }
lv_subject_t *DeviceToolsViewModel::configuration_status_subject() noexcept { return configuration_status_.get(); }
lv_subject_t *DeviceToolsViewModel::configuration_ready_subject() noexcept { return configuration_ready_.get(); }
lv_subject_t *DeviceToolsViewModel::radio_identity_subject() noexcept { return radio_identity_.get(); }
lv_subject_t *DeviceToolsViewModel::legacy_form_subject() noexcept { return legacy_form_.get(); }
lv_subject_t *DeviceToolsViewModel::legacy_ready_subject() noexcept { return legacy_ready_.get(); }
