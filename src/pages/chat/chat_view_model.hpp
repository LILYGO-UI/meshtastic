#ifndef LILYGO_UI_MESHTASTIC_CHAT_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_CHAT_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct ChatMessageViewModel {
    std::uint32_t id = 0;
    std::string sender;
    std::string text;
    bool outgoing = false;
};

class ChatViewModel {
public:
    explicit ChatViewModel(meshtastic::MeshtasticClient *client = nullptr);

    ChatViewModel(const ChatViewModel &)            = delete;
    ChatViewModel &operator=(const ChatViewModel &) = delete;
    ChatViewModel(ChatViewModel &&)                 = delete;
    ChatViewModel &operator=(ChatViewModel &&)      = delete;

    void set_client(meshtastic::MeshtasticClient *client) noexcept;
    void select_channel(std::int32_t index, std::string name);
    void sync(const meshtastic::ClientSnapshot &snapshot);
    bool send_text(const char *text);

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] const std::vector<ChatMessageViewModel> &messages() const noexcept;
    [[nodiscard]] lv_subject_t *channel_subject() noexcept;
    [[nodiscard]] lv_subject_t *connected_subject() noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *node_status_subject() noexcept;

private:
    void publish_content();

    meshtastic::MeshtasticClient *client_ = nullptr;
    std::uint32_t channel_                = 0;
    IntSubject connected_;
    IntSubject content_revision_;
    StringSubject<128> channel_name_;
    StringSubject<96> node_status_;
    std::vector<ChatMessageViewModel> messages_;
};

#endif
