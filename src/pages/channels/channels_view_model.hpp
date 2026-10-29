#ifndef LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "configuration/admin_transaction.hpp"
#include "domain/meshtastic_client.hpp"
#include <meshtastic/apponly.pb.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

struct ChannelRowViewModel {
    std::int32_t index = 0;
    std::string name;
    std::string detail;
    std::uint32_t role = 0;
};

class ChannelsViewModel {
public:
    explicit ChannelsViewModel(meshtastic::MeshtasticClient *client = nullptr);
    ChannelsViewModel(const ChannelsViewModel &) = delete;
    ChannelsViewModel &operator=(const ChannelsViewModel &) = delete;
    ChannelsViewModel(ChannelsViewModel &&) = delete;
    ChannelsViewModel &operator=(ChannelsViewModel &&) = delete;

    void set_client(meshtastic::MeshtasticClient *client) noexcept;
    void sync(const meshtastic::ClientSnapshot &snapshot);
    void select_channel(std::int32_t index);
    void select_primary();
    bool edit_channel(std::int32_t index);
    bool begin_new_channel();
    bool refresh_channel(std::int32_t index);
    enum class KeyMode { keep_existing, unencrypted, public_default, custom_base64 };
    bool stage_channel(std::string name, std::int32_t role, KeyMode key_mode, const std::string &key,
                       bool uplink, bool downlink);
    bool save_channel();
    void discard_channel();
    bool preview_import(const std::string &link);
    bool apply_import(bool include_lora);
    bool export_channels();
    const meshtastic::Channel *draft_channel() const noexcept;
    const std::string &share_link() const noexcept;
    const std::string &import_summary() const noexcept;
    bool import_has_lora() const noexcept;
    static bool parse_channel_link(const std::string &link, meshtastic::ChannelSet &set, std::string &error);
    static std::string make_channel_link(const meshtastic::ChannelSet &set);
    static bool decode_key(const std::string &text, std::string &key, std::string &error);
    [[nodiscard]] const std::vector<ChannelRowViewModel> &rows() const noexcept;
    [[nodiscard]] std::int32_t selected_channel() const noexcept;
    [[nodiscard]] const std::string &selected_channel_name() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *editor_content_subject() noexcept;
    [[nodiscard]] lv_subject_t *status_subject() noexcept;
    [[nodiscard]] lv_subject_t *radio_identity_subject() noexcept;

private:
    void publish_content();
    void publish_editor();
    bool writable();
    std::uint64_t channel_revision(int index) const;
    bool queue_channels(const std::vector<meshtastic::Channel> &channels,
                        const std::optional<meshtastic::Config> &lora = std::nullopt);
    meshtastic::MeshtasticClient *client_;
    meshtastic::ClientSnapshot snapshot_;
    IntSubject content_revision_{0, 0};
    IntSubject editor_revision_{0, 0};
    IntSubject radio_identity_{0};
    StringSubject<256> status_{"Select a channel to chat or edit its radio settings"};
    std::vector<ChannelRowViewModel> rows_;
    std::int32_t selected_channel_ = 0;
    std::string selected_channel_name_ = "Primary channel";
    std::optional<meshtastic::Channel> draft_, original_;
    meshtastic::configuration::AdminTransaction transaction_;
    std::optional<meshtastic::ChannelSet> import_;
    std::string share_link_, import_summary_;
    std::uint32_t radio_number_ = 0;
};
#endif
