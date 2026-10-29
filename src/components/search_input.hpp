#ifndef LILYGO_UI_MESHTASTIC_SEARCH_INPUT_HPP
#define LILYGO_UI_MESHTASTIC_SEARCH_INPUT_HPP

#include <lvgl.h>
#include <functional>
#include <string>

// Owns a search editor and its touch keyboard. Destroy before its page parent.
class SearchInput {
public:
    using Changed = std::function<void(std::string)>;
    ~SearchInput() { destroy(); }
    SearchInput() = default;
    SearchInput(const SearchInput &) = delete;
    SearchInput &operator=(const SearchInput &) = delete;
    lv_obj_t *create(lv_obj_t *parent, const char *name, const char *placeholder,
                     const std::string &value, Changed changed);
    void destroy() noexcept;
private:
    static void input_event(lv_event_t *event);
    static void keyboard_event(lv_event_t *event);
    lv_obj_t *input_ = nullptr;
    lv_obj_t *keyboard_ = nullptr;
    Changed changed_;
};
#endif
