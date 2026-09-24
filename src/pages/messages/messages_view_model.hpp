#ifndef LILYGO_UI_MESHTASTIC_MESSAGES_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_MESSAGES_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <string>
#include <vector>

struct MessageRowViewModel {
    std::string sender;
    std::string text;
    bool outgoing = false;
};

class MessagesViewModel {
public:
    MessagesViewModel();

    MessagesViewModel(const MessagesViewModel &)            = delete;
    MessagesViewModel &operator=(const MessagesViewModel &) = delete;
    MessagesViewModel(MessagesViewModel &&)                 = delete;
    MessagesViewModel &operator=(MessagesViewModel &&)      = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot);

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] const std::vector<MessageRowViewModel> &rows() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *footer_subject() noexcept;

private:
    void publish_content();

    IntSubject connected_;
    IntSubject content_revision_;
    StringSubject<96> footer_;
    std::vector<MessageRowViewModel> rows_;
};

#endif
