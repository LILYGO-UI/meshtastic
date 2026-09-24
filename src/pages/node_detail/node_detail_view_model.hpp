#ifndef LILYGO_UI_MESHTASTIC_NODE_DETAIL_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_NODE_DETAIL_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <cstdint>
#include <string>

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
    std::string last_heard;
};

class NodeDetailViewModel {
public:
    NodeDetailViewModel();

    NodeDetailViewModel(const NodeDetailViewModel &)            = delete;
    NodeDetailViewModel &operator=(const NodeDetailViewModel &) = delete;
    NodeDetailViewModel(NodeDetailViewModel &&)                 = delete;
    NodeDetailViewModel &operator=(NodeDetailViewModel &&)      = delete;

    void select_node(std::uint32_t number, const meshtastic::ClientSnapshot &snapshot);
    void sync(const meshtastic::ClientSnapshot &snapshot);

    [[nodiscard]] std::uint32_t selected_node() const noexcept;
    [[nodiscard]] const NodeDetailPresentation &presentation() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;

private:
    void publish_content();

    IntSubject content_revision_;
    std::uint32_t selected_node_ = 0;
    NodeDetailPresentation presentation_;
};

#endif
