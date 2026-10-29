#include "components/search_input.hpp"

#include <cassert>
#include <cstring>
#include <vector>

int main()
{
    lv_init();
    auto *display = lv_display_create(568, 1232);
    assert(display);
    std::vector<lv_color32_t> buffer(568U * 80U);
    lv_display_set_buffers(display, buffer.data(), nullptr,
                           static_cast<std::uint32_t>(buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_obj_t *root = lv_screen_active();
    auto *page = lv_obj_create(root);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    SearchInput search;
    bool changed = false;
    auto *input = search.create(page, "search_test_input", "Search", "", [&](std::string value) {
        changed = value == "abc";
    });
    assert(input);
    auto *keyboard = lv_obj_find_by_name(root, "search_keyboard");
    assert(keyboard);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    lv_textarea_set_text(input, "abc");
    assert(changed);
    lv_obj_send_event(input, LV_EVENT_DEFOCUSED, nullptr);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    std::uint32_t key = LV_KEY_ENTER; lv_obj_send_event(input, LV_EVENT_KEY, &key);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
    assert(!lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(keyboard, LV_EVENT_CANCEL, nullptr);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    auto *group = lv_group_create();
    lv_group_add_obj(group, input);
    lv_group_focus_obj(input);
    lv_obj_send_event(input, LV_EVENT_KEY, &key);
    assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
    assert(lv_group_get_focused(group) == input);
    assert(lv_obj_has_state(input, LV_STATE_FOCUSED));
    lv_group_delete(group);
    lv_obj_delete(keyboard);
    lv_obj_send_event(input, LV_EVENT_FOCUSED, nullptr);
    search.destroy();
    assert(!lv_obj_find_by_name(page, "search_test_input"));
    assert(!lv_obj_find_by_name(root, "search_keyboard"));
    lv_obj_delete(page);
    lv_display_delete(display);
    lv_deinit();
    return 0;
}
