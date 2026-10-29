#ifndef LILYGO_UI_MESHTASTIC_MESSAGES_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_MESSAGES_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <string>
#include <vector>

struct MessageRowViewModel {
    std::uint32_t id = 0;
    std::string sender;
    std::string conversation;
    std::string text;
    std::string detail;
    // The conversation key is kept with each row so the view can open the
    // exact channel or peer represented by the latest message.
    std::uint32_t peer = 0;
    std::uint32_t channel = 0;
    bool outgoing = false;
    bool direct = false;
    std::size_t unread_count = 0;
};

class MessagesViewModel {
public:
    MessagesViewModel();

    MessagesViewModel(const MessagesViewModel &)            = delete;
    MessagesViewModel &operator=(const MessagesViewModel &) = delete;
    MessagesViewModel(MessagesViewModel &&)                 = delete;
    MessagesViewModel &operator=(MessagesViewModel &&)      = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot);
    void select_conversation(bool direct, std::uint32_t peer, std::uint32_t channel, std::string name);
    void clear_selection() noexcept;
    void set_query(std::string query);
    [[nodiscard]] const std::string &query() const noexcept;

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] const std::vector<MessageRowViewModel> &rows() const noexcept;
    [[nodiscard]] bool has_selected_conversation() const noexcept;
    [[nodiscard]] bool selected_direct() const noexcept;
    [[nodiscard]] std::uint32_t selected_peer() const noexcept;
    [[nodiscard]] std::uint32_t selected_channel() const noexcept;
    [[nodiscard]] const std::string &selected_name() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *footer_subject() noexcept;

private:
    void publish_content();

    IntSubject connected_;
    IntSubject content_revision_;
    StringSubject<96> footer_;
    std::vector<MessageRowViewModel> rows_;
    bool selected_ = false;
    bool selected_direct_ = false;
    std::uint32_t selected_peer_ = 0;
    std::uint32_t selected_channel_ = 0;
    std::string selected_name_;
    std::string query_;
    meshtastic::ClientSnapshot snapshot_;
};

#endif
