#ifndef LILYGO_UI_MESHTASTIC_CHAT_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_CHAT_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <cstdint>
#include <string>
#include <map>
#include <vector>

struct ChatMessageViewModel {
    std::uint32_t id = 0;
    std::string sender;
    std::string text;
    bool outgoing = false;
    std::int64_t local_id = 0;
    std::string detail;
    bool retryable = false;
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
    void select_conversation(bool direct, std::uint32_t peer, std::uint32_t channel, std::string name);
    void sync(const meshtastic::ClientSnapshot &snapshot);
    bool send_text(const char *text);
    bool retry_message(std::int64_t local_id);
    bool load_older();
    void set_active(bool active);
    void set_draft(std::string text);
    [[nodiscard]] std::string draft() const;
    [[nodiscard]] bool has_older() const noexcept;

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool has_selection() const noexcept;
    [[nodiscard]] bool selected_direct() const noexcept;
    [[nodiscard]] std::uint32_t selected_peer() const noexcept;
    [[nodiscard]] std::uint32_t selected_channel() const noexcept;
    [[nodiscard]] const std::vector<ChatMessageViewModel> &messages() const noexcept;
    [[nodiscard]] lv_subject_t *channel_subject() noexcept;
    [[nodiscard]] lv_subject_t *connected_subject() noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *node_status_subject() noexcept;
    [[nodiscard]] lv_subject_t *feedback_subject() noexcept;

private:
    void publish_content();

    meshtastic::MeshtasticClient *client_ = nullptr;
    std::uint32_t channel_                = 0;
    std::uint32_t peer_                   = 0;
    bool direct_                          = false;
    bool selection_set_                   = false;
    std::string selection_name_;
    bool active_ = false;
    bool has_older_ = false;
    std::size_t message_limit_ = 50;
    IntSubject connected_;
    IntSubject content_revision_;
    StringSubject<128> channel_name_;
    StringSubject<96> node_status_;
    StringSubject<192> feedback_{""};
    std::vector<ChatMessageViewModel> messages_;
    meshtastic::ClientSnapshot snapshot_;
    std::map<std::int64_t, meshtastic::Message> loaded_history_;
    std::string scope_;
    std::map<std::string, std::string> drafts_;
    [[nodiscard]] std::string conversation_key() const;
};

#endif
