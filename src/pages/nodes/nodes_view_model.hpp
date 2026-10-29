#ifndef LILYGO_UI_MESHTASTIC_NODES_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_NODES_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct NodeRowViewModel {
    std::uint32_t number = 0;
    std::uint32_t last_heard = 0;
    std::string name;
    std::string detail;
    bool local = false;
};

class NodesViewModel {
public:
    NodesViewModel();

    NodesViewModel(const NodesViewModel &)            = delete;
    NodesViewModel &operator=(const NodesViewModel &) = delete;
    NodesViewModel(NodesViewModel &&)                 = delete;
    NodesViewModel &operator=(NodesViewModel &&)      = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot);
    void select_node(std::uint32_t number) noexcept;
    void set_query(std::string query);
    void set_filter(int filter);
    [[nodiscard]] const std::string &query() const noexcept;
    [[nodiscard]] int filter() const noexcept;

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] std::uint32_t selected_node() const noexcept;
    [[nodiscard]] const std::vector<NodeRowViewModel> &rows() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *count_subject() noexcept;
    [[nodiscard]] lv_subject_t *footer_subject() noexcept;

private:
    void publish_content();

    IntSubject connected_;
    IntSubject content_revision_;
    StringSubject<96> count_;
    StringSubject<96> footer_;
    std::uint32_t selected_node_ = 0;
    std::vector<NodeRowViewModel> rows_;
    meshtastic::ClientSnapshot snapshot_;
    std::string query_;
    int filter_ = 0;
};

#endif
