#include "pages/device_tools/device_tools_view.hpp"
#include "pages/channels/channels_view.hpp"
#include <cm0/typography.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::vector<lv_color32_t> framebuffer;
int display_width;
void flush(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    auto *source = reinterpret_cast<lv_color32_t *>(pixels);
    const int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y)
        std::copy_n(source + static_cast<std::size_t>(y - area->y1) * width, width,
                    framebuffer.begin() + static_cast<std::size_t>(y) * display_width + area->x1);
    lv_display_flush_ready(display);
}
void pump()
{
    for (int index = 0; index < 20; ++index) { lv_tick_inc(5); lv_timer_handler(); }
    lv_obj_update_layout(lv_screen_active());
}
void capture(lv_display_t *display, const std::string &path)
{
    pump(); lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    std::ofstream file(path, std::ios::binary); assert(file);
    file << "P6\n" << display_width << ' ' << lv_display_get_vertical_resolution(display) << "\n255\n";
    for (auto pixel : framebuffer) { file.put(pixel.red); file.put(pixel.green); file.put(pixel.blue); }
}
lv_obj_t *find(lv_obj_t *root, const char *name) { auto *object = lv_obj_find_by_name(root, name); assert(object); return object; }
lv_obj_t *button(lv_obj_t *root, const std::string &text)
{
    if (lv_obj_has_class(root, &lv_label_class) && lv_label_get_text(root) == text) return lv_obj_get_parent(root);
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(root); ++index)
        if (auto *found = button(lv_obj_get_child(root, index), text)) return found;
    return nullptr;
}
void click(lv_obj_t *object) { assert(object); lv_obj_send_event(object, LV_EVENT_CLICKED, nullptr); pump(); }
void assert_editor_visible(lv_obj_t *input, lv_obj_t *keyboard)
{
    assert(lv_obj_is_visible(input));
    assert(lv_obj_is_visible(keyboard));
    lv_area_t input_area{}, keyboard_area{};
    lv_obj_get_coords(input, &input_area); lv_obj_get_coords(keyboard, &keyboard_area);
    assert(input_area.x1 >= 0 && input_area.x2 < lv_display_get_horizontal_resolution(nullptr));
    assert(input_area.y1 >= 0 && input_area.y2 < keyboard_area.y1);
}
void assert_label_widths(lv_obj_t *root)
{
    if (lv_obj_is_visible(root) && lv_obj_has_class(root, &lv_label_class)) {
        lv_area_t area{}, parent{}; lv_obj_get_coords(root, &area); lv_obj_get_content_coords(lv_obj_get_parent(root), &parent);
        assert(area.x1 >= parent.x1 - 1 && area.x2 <= parent.x2 + 1);
    }
    for (std::uint32_t index = 0; index < lv_obj_get_child_count(root); ++index) assert_label_widths(lv_obj_get_child(root, index));
}
void append_varint(std::string &bytes, unsigned value)
{
    do { const auto byte = value & 127U; value >>= 7U; bytes += static_cast<char>(byte | (value ? 128U : 0U)); } while (value);
}
meshtastic::ClientSnapshot snapshot()
{
    meshtastic::ClientSnapshot result;
    result.my_node_number = 101;
    result.state = meshtastic::ConnectionState::connected;
    result.config_complete = true;
    result.endpoint = "very-long-radio-endpoint.local:4403";
    result.owner.emplace(); result.owner->set_long_name("山脊 Field Radio");
    for (const auto &category : meshtastic::configuration::catalog()) {
        std::string bytes; append_varint(bytes, static_cast<unsigned>(category.payload_case) * 8U + 2U); bytes += '\0';
        if (category.scope == meshtastic::configuration::Scope::device) {
            assert(result.device_configs[category.payload_case].ParseFromString(bytes));
            result.device_config_revisions[category.payload_case] = 1;
        } else {
            assert(result.module_configs[category.payload_case].ParseFromString(bytes));
            result.module_config_revisions[category.payload_case] = 1;
        }
    }
    result.device_configs[meshtastic::Config::kNetwork].mutable_network()->set_wifi_ssid("通信网 Field Wi-Fi");
    result.device_configs[meshtastic::Config::kNetwork].mutable_network()->set_wifi_psk("HiddenPassword");
    result.lora_config = result.device_configs[meshtastic::Config::kLora].lora();
    result.device_config = result.device_configs[meshtastic::Config::kDevice].device();
    for (int index = 0; index < 8; ++index) {
        auto &channel = result.channel_configs[index];
        channel.set_index(index);
        channel.set_role(index == 0 ? meshtastic::Channel::PRIMARY : index == 1 ? meshtastic::Channel::SECONDARY : meshtastic::Channel::DISABLED);
        channel.mutable_settings()->set_name(index == 0 ? "Operations" : index == 1 ? "Logistics" : "");
        channel.mutable_settings()->set_psk(std::string(16, '\x42'));
        result.channel_revisions[index] = 1;
        if (index < 2) result.channels.push_back({index, channel.settings().name(), static_cast<std::uint32_t>(channel.role()), true});
    }
    return result;
}
}  // namespace
int main(int argc, char **argv)
{
    assert(argc == 4);
    const int width = std::atoi(argv[1]), height = std::atoi(argv[2]);
    display_width = width; framebuffer.resize(static_cast<std::size_t>(width) * height);
    std::vector<lv_color32_t> buffer(static_cast<std::size_t>(std::max(width, height)) * 80);
    lv_init();
    auto *display = lv_display_create(width, height);
    lv_display_set_buffers(display, buffer.data(), nullptr, static_cast<std::uint32_t>(buffer.size() * sizeof(lv_color32_t)), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush); lv_display_set_default(display);
    auto *group = lv_group_create(); lv_group_set_default(group);
    auto *root = lv_obj_create(lv_screen_active());
    lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(root, 0, 0); lv_obj_set_style_border_width(root, 0, 0);
    auto state = snapshot();
    {
        DeviceToolsViewModel model; model.sync(state, true);
        DeviceToolsView view(model, {}); view.create(root); pump();
        auto *selector = find(root, "settings_category_dropdown");
        // Every official category has a normal editor, with no packet/hex form.
        for (std::size_t index = 0; index < model.configuration_categories().size(); ++index) {
            lv_dropdown_set_selected(selector, index); lv_obj_send_event(selector, LV_EVENT_VALUE_CHANGED, nullptr); pump();
            assert(model.selected_draft().loaded());
            assert(lv_obj_get_child_count(find(root, "settings_configuration_fields")) > 0);
            assert_label_widths(find(root, "settings_configuration_fields"));
        }
        lv_dropdown_set_selected(selector, 3); lv_obj_send_event(selector, LV_EVENT_VALUE_CHANGED, nullptr); pump();
        auto *ssid = find(root, "settings_field_device_4_wifi_ssid");
        lv_group_focus_obj(ssid); lv_obj_send_event(ssid, LV_EVENT_FOCUSED, nullptr); pump();
        auto *keyboard = find(root, "settings_software_keyboard");
        assert_editor_visible(ssid, keyboard);
        lv_textarea_set_text(ssid, "Draft 通信网");
        state.device_configs[meshtastic::Config::kNetwork].mutable_network()->set_wifi_ssid("Remote refresh");
        state.device_config_revisions[meshtastic::Config::kNetwork] = 2;
        model.sync(state, true); pump();
        assert(std::string(lv_textarea_get_text(ssid)) == "Draft 通信网");
        capture(display, std::string(argv[3]) + "-settings.ppm");
        display_width = height; framebuffer.resize(static_cast<std::size_t>(height) * width);
        lv_display_set_resolution(display, height, width); view.update_layout(); pump();
        assert(find(root, "settings_field_device_4_wifi_ssid") == ssid);
        assert(std::string(lv_textarea_get_text(ssid)) == "Draft 通信网");
        assert(lv_group_get_focused(group) == ssid);
        assert_editor_visible(ssid, keyboard);
        capture(display, std::string(argv[3]) + "-settings-rotated.ppm");
        lv_obj_send_event(keyboard, LV_EVENT_CANCEL, nullptr); pump();
        assert(lv_obj_has_flag(keyboard, LV_OBJ_FLAG_HIDDEN));
        click(find(root, "settings_reboot"));
        assert(find(root, "settings_action_confirmation"));
        state.my_node_number = 202;
        state.owner->set_long_name("Second radio");
        state.device_configs[meshtastic::Config::kNetwork].mutable_network()->set_wifi_ssid("Second network");
        state.device_config_revisions[meshtastic::Config::kNetwork] = 3;
        model.sync(state, true); pump();
        assert(!lv_obj_find_by_name(root, "settings_action_confirmation"));
        assert(std::string(lv_textarea_get_text(find(root, "settings_node_name_input"))) == "Second radio");
        assert(std::string(lv_textarea_get_text(find(root, "settings_field_device_4_wifi_ssid"))) == "Second network");
        view.destroy(); pump();
        assert(!lv_obj_find_by_name(root, "settings_configuration_fields"));
        view.create(root); pump(); view.destroy();
    }
    display_width = width; framebuffer.resize(static_cast<std::size_t>(width) * height);
    lv_display_set_resolution(display, width, height); pump();
    {
        ChannelsViewModel model; model.sync(state);
        AppPage destination = AppPage::channels;
        AppNavigationCommands navigation; navigation.context = &destination;
        navigation.navigate = [](void *context, AppPage page) { *static_cast<AppPage *>(context) = page; };
        ChannelsView view(model, navigation); view.create(root); pump();
        auto *secondary = find(root, "channels_secondary");
        click(lv_obj_get_child(secondary, 0));
        assert(destination == AppPage::chat && model.selected_channel() == 1);
        lv_dropdown_set_selected(find(root, "channels_slot_dropdown"), 1);
        click(button(root, "Edit selected slot"));
        auto *name = find(root, "channels_name_input");
        lv_group_focus_obj(name); lv_obj_send_event(name, LV_EVENT_FOCUSED, nullptr); pump();
        auto *keyboard = find(root, "channels_software_keyboard");
        assert_editor_visible(name, keyboard);
        lv_textarea_set_text(name, "Draft");
        state.channel_configs[1].mutable_settings()->set_name("Remote"); state.channel_revisions[1] = 2;
        model.sync(state); pump(); assert(std::string(lv_textarea_get_text(name)) == "Draft");
        capture(display, std::string(argv[3]) + "-channels.ppm");
        display_width = height; framebuffer.resize(static_cast<std::size_t>(height) * width);
        lv_display_set_resolution(display, height, width); view.update_layout(); pump();
        assert(find(root, "channels_name_input") == name && std::string(lv_textarea_get_text(name)) == "Draft");
        assert(lv_group_get_focused(group) == name); assert_editor_visible(name, keyboard);
        lv_obj_send_event(keyboard, LV_EVENT_READY, nullptr); pump();
        click(find(root, "channels_save_channel"));
        assert(find(root, "channels_confirmation"));
        capture(display, std::string(argv[3]) + "-channel-confirmation.ppm");
        click(button(root, "Cancel"));
        assert(model.export_channels());
        lv_textarea_set_text(find(root, "channels_import_link"), model.share_link().c_str());
        click(button(root, "Preview channel link"));
        assert(model.import_summary().find("slots 0-1") != std::string::npos);
        lv_dropdown_set_selected(find(root, "channels_slot_dropdown"), 1);
        click(button(root, "+"));
        assert(model.draft_channel() && model.draft_channel()->index() == 2);
        assert(model.draft_channel()->role() == meshtastic::Channel::SECONDARY);
        assert(std::string(lv_textarea_get_text(find(root, "channels_name_input"))).empty());
        click(find(root, "channels_apply_import")); assert(find(root, "channels_confirmation"));
        state.my_node_number = 303;
        model.sync(state); pump();
        assert(!lv_obj_find_by_name(root, "channels_confirmation"));
        assert(!model.draft_channel() && model.share_link().empty() && model.import_summary().empty());
        assert(std::string(lv_textarea_get_text(find(root, "channels_import_link"))).empty());
        assert(lv_obj_has_flag(find(root, "channels_share_link"), LV_OBJ_FLAG_HIDDEN));
        view.destroy(); pump();
        assert(!lv_obj_find_by_name(root, "channels_software_keyboard"));
        view.create(root); pump(); view.destroy();
    }
    lv_obj_delete(root); lv_group_set_default(nullptr); lv_group_delete(group);
    lv_display_delete(display); lilygo_ui_fonts_deinit(); lv_deinit();
    return 0;
}
