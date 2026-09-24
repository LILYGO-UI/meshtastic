#ifndef LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_CHANNELS_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <cstdint>
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
    ChannelsViewModel();

    ChannelsViewModel(const ChannelsViewModel &)            = delete;
    ChannelsViewModel &operator=(const ChannelsViewModel &) = delete;
    ChannelsViewModel(ChannelsViewModel &&)                 = delete;
    ChannelsViewModel &operator=(ChannelsViewModel &&)      = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot);
    void select_channel(std::int32_t index);
    void select_primary();

    [[nodiscard]] const std::vector<ChannelRowViewModel> &rows() const noexcept;
    [[nodiscard]] std::int32_t selected_channel() const noexcept;
    [[nodiscard]] const std::string &selected_channel_name() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;

private:
    void publish_content();

    IntSubject content_revision_;
    std::vector<ChannelRowViewModel> rows_;
    std::int32_t selected_channel_     = 0;
    std::string selected_channel_name_ = "Primary channel";
};

#endif
