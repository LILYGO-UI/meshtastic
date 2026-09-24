#include "app.hpp"
#include "domain/meshtastic_protocol.hpp"

#include <meshtastic/mesh.pb.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace {

constexpr int kPartialRows                        = 80;
constexpr std::uint32_t kColorPageBackground      = 0xf2f2f7;
constexpr std::uint32_t kColorContainerBackground = 0xffffff;
constexpr char kLongNodeName[] =
    u8"\u534e\u4e1c\u5c71\u810a\u5e94\u6025\u901a\u4fe1\u4e2d\u7ee7\u7ad9 Alpha-Long-Name-For-Wrapping";
constexpr char kPrimaryChannel[] = u8"\u5e94\u6025\u534f\u8c03\u9891\u9053 Primary-Operations-With-A-Long-Name";
constexpr char kLongMessage[] =
    u8"\u8fd9\u662f\u4e00\u6761\u7528\u4e8e\u9a8c\u8bc1\u4e2d\u6587\u56de\u9000\u3001\u81ea\u52a8\u6362\u884c\u548c"
    u8"\u7d27\u51d1\u5e03\u5c40\u7684\u8f83\u957f\u6d88\u606f; field report wraps without covering adjacent controls";
constexpr char kDraft[] =
    u8"\u8349\u7a3f\uff1a\u4fdd\u7559\u8f93\u5165\u3001\u5149\u6807\u548c\u7126\u70b9 across portrait and landscape";

std::vector<lv_color32_t> framebuffer;
int display_width = 0;

template <typename MessageType>
std::vector<std::uint8_t> serialize(const MessageType &message)
{
    std::vector<std::uint8_t> payload(message.ByteSizeLong());
    assert(payload.size() <= meshtastic::kMaxRadioPayload);
    assert(message.SerializeToArray(payload.data(), static_cast<int>(payload.size())));
    return payload;
}

bool send_all(int socket, const std::vector<std::uint8_t> &data)
{
    std::size_t offset = 0;
    while (offset < data.size()) {
#if defined(MSG_NOSIGNAL)
        const auto sent = ::send(socket, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
#else
        const auto sent = ::send(socket, data.data() + offset, data.size() - offset, 0);
#endif
        if (sent <= 0) return false;
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

std::vector<std::uint8_t> receive_frame(int socket, meshtastic::FrameParser &parser)
{
    std::uint8_t buffer[256]{};
    while (true) {
        const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
        if (count <= 0) return {};
        auto frames = parser.feed(buffer, static_cast<std::size_t>(count));
        if (!frames.empty()) return std::move(frames.front());
    }
}

void append_radio_frame(std::vector<std::uint8_t> &stream, const meshtastic::FromRadio &message)
{
    const auto frame = meshtastic::frame_payload(serialize(message));
    assert(!frame.empty());
    stream.insert(stream.end(), frame.begin(), frame.end());
}

std::vector<std::uint8_t> make_radio_snapshot(std::uint32_t config_id)
{
    std::vector<std::uint8_t> stream;

    meshtastic::FromRadio my_info;
    my_info.mutable_my_info()->set_my_node_num(0x12345678U);
    append_radio_frame(stream, my_info);

    meshtastic::FromRadio local_node;
    auto *local = local_node.mutable_node_info();
    local->set_num(0x12345678U);
    local->mutable_user()->set_id("!12345678");
    local->mutable_user()->set_long_name("Field Radio");
    local->mutable_user()->set_short_name("FR");
    local->set_last_heard(static_cast<std::uint32_t>(std::time(nullptr)));
    local->mutable_device_metrics()->set_battery_level(91);
    append_radio_frame(stream, local_node);

    meshtastic::FromRadio remote_node;
    auto *remote = remote_node.mutable_node_info();
    remote->set_num(0x87654321U);
    remote->mutable_user()->set_id("!87654321");
    remote->mutable_user()->set_long_name(kLongNodeName);
    remote->mutable_user()->set_short_name("EC");
    remote->set_last_heard(static_cast<std::uint32_t>(std::time(nullptr) - 125));
    remote->set_snr(7.25F);
    remote->set_hops_away(3);
    remote->mutable_device_metrics()->set_battery_level(64);
    remote->mutable_device_metrics()->set_voltage(3.92F);
    remote->mutable_position()->set_latitude_i(312304000);
    remote->mutable_position()->set_longitude_i(1214737000);
    remote->mutable_position()->set_altitude(36);
    append_radio_frame(stream, remote_node);

    meshtastic::FromRadio primary_channel;
    auto *primary = primary_channel.mutable_channel();
    primary->set_index(0);
    primary->set_role(meshtastic::Channel::PRIMARY);
    primary->mutable_settings()->set_name(kPrimaryChannel);
    primary->mutable_settings()->set_psk("\x01", 1);
    append_radio_frame(stream, primary_channel);

    meshtastic::FromRadio secondary_channel;
    auto *secondary = secondary_channel.mutable_channel();
    secondary->set_index(1);
    secondary->set_role(meshtastic::Channel::SECONDARY);
    secondary->mutable_settings()->set_name(u8"\u73b0\u573a\u540e\u52e4 Secondary Logistics");
    append_radio_frame(stream, secondary_channel);

    for (std::uint32_t index = 0; index < 12; ++index) {
        meshtastic::FromRadio incoming;
        auto *packet = incoming.mutable_packet();
        packet->set_from(0x87654321U);
        packet->set_to(meshtastic::kBroadcastNode);
        packet->set_id(1000U + index);
        packet->set_channel(index % 2U);
        packet->set_rx_time(static_cast<std::uint32_t>(std::time(nullptr) - index * 20U));
        packet->set_rx_rssi(-72 - static_cast<std::int32_t>(index));
        packet->set_rx_snr(6.5F);
        packet->mutable_decoded()->set_portnum(meshtastic::TEXT_MESSAGE_APP);
        packet->mutable_decoded()->set_payload(std::string(kLongMessage) + " #" + std::to_string(index + 1U));
        append_radio_frame(stream, incoming);
    }

    meshtastic::FromRadio complete;
    complete.set_config_complete_id(config_id);
    append_radio_frame(stream, complete);
    return stream;
}

class FakeRadio {
public:
    FakeRadio()
    {
        const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(listener >= 0);
        int reuse = 1;
        assert(::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0);
        sockaddr_in address{};
        address.sin_family      = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port        = 0;
        assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
        assert(::listen(listener, 1) == 0);
        socklen_t address_size = sizeof(address);
        assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_size) == 0);
        port_   = ntohs(address.sin_port);
        server_ = std::thread([listener] {
            const int peer = ::accept(listener, nullptr, nullptr);
            assert(peer >= 0);
            ::close(listener);

            meshtastic::FrameParser parser;
            const auto handshake = receive_frame(peer, parser);
            meshtastic::ToRadio request;
            assert(request.ParseFromArray(handshake.data(), static_cast<int>(handshake.size())));
            assert(request.payload_variant_case() == meshtastic::ToRadio::kWantConfigId);
            assert(request.want_config_id() != 0);
            assert(send_all(peer, make_radio_snapshot(request.want_config_id())));

            std::uint8_t ignored[256]{};
            while (::recv(peer, ignored, sizeof(ignored), 0) > 0) {
            }
            ::close(peer);
        });
    }

    ~FakeRadio()
    {
        if (server_.joinable()) server_.join();
    }

    [[nodiscard]] std::uint16_t port() const noexcept
    {
        return port_;
    }

private:
    std::uint16_t port_ = 0;
    std::thread server_;
};

void flush_display(lv_display_t *display, const lv_area_t *area, std::uint8_t *pixels)
{
    const int width    = area->x2 - area->x1 + 1;
    const auto *source = reinterpret_cast<const lv_color32_t *>(pixels);

    for (int y = area->y1; y <= area->y2; ++y) {
        const auto source_offset = static_cast<std::size_t>(y - area->y1) * static_cast<std::size_t>(width);
        const auto destination_offset =
            static_cast<std::size_t>(y) * static_cast<std::size_t>(display_width) + static_cast<std::size_t>(area->x1);
        std::copy_n(source + source_offset, width, framebuffer.begin() + destination_offset);
    }
    lv_display_flush_ready(display);
}

bool write_ppm(const char *path, int width, int height)
{
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << "P6\n" << width << ' ' << height << "\n255\n";
    for (const auto &pixel : framebuffer) {
        file.put(static_cast<char>(pixel.red));
        file.put(static_cast<char>(pixel.green));
        file.put(static_cast<char>(pixel.blue));
    }
    return file.good();
}

lv_obj_t *find_label_containing(lv_obj_t *root, std::string_view text)
{
    if (lv_obj_has_class(root, &lv_label_class)) {
        const char *label_text = lv_label_get_text(root);
        if (label_text && std::string_view(label_text).find(text) != std::string_view::npos) return root;
    }
    const auto child_count = lv_obj_get_child_count(root);
    for (std::uint32_t index = 0; index < child_count; ++index) {
        if (auto *match = find_label_containing(lv_obj_get_child(root, static_cast<std::int32_t>(index)), text))
            return match;
    }
    return nullptr;
}

lv_obj_t *button_for_label(lv_obj_t *root, std::string_view text)
{
    lv_obj_t *object = find_label_containing(root, text);
    while (object && !lv_obj_has_class(object, &lv_button_class)) object = lv_obj_get_parent(object);
    return object;
}

bool is_descendant_of(lv_obj_t *object, lv_obj_t *ancestor)
{
    while (object) {
        if (object == ancestor) return true;
        object = lv_obj_get_parent(object);
    }
    return false;
}

void pump_lvgl(std::uint32_t milliseconds)
{
    lv_tick_inc(milliseconds);
    (void)lv_timer_handler();
}

template <typename Predicate>
void wait_until(Predicate predicate)
{
    for (int attempt = 0; attempt < 400; ++attempt) {
        pump_lvgl(10);
        lv_obj_update_layout(lv_screen_active());
        if (predicate()) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(false && "timed out waiting for UI state");
}

void click_tail_event(lv_event_t *event)
{
    *static_cast<bool *>(lv_event_get_user_data(event)) = true;
}

lv_obj_t *click_and_wait(lv_obj_t *trigger, lv_obj_t *root, const char *expected_name)
{
    assert(trigger && root && expected_name);
    assert(lv_obj_find_by_name(root, expected_name) == nullptr);
    lv_obj_t *page_before = lv_obj_find_by_name(root, "page_body");
    assert(page_before);
    bool tail_callback_ran = false;
    lv_event_dsc_t *tail =
        lv_obj_add_event_cb(trigger, click_tail_event, LV_EVENT_CLICKED, static_cast<void *>(&tail_callback_ran));
    assert(tail);
    assert(lv_obj_send_event(trigger, LV_EVENT_CLICKED, nullptr) == LV_RESULT_OK);
    assert(tail_callback_ran);
    assert(lv_obj_is_valid(trigger));
    assert(lv_obj_find_by_name(root, "page_body") == page_before);
    assert(lv_obj_remove_event_dsc(trigger, tail));

    lv_obj_t *marker = nullptr;
    wait_until([&] {
        marker = lv_obj_find_by_name(root, expected_name);
        return marker != nullptr;
    });
    lv_obj_t *page_after = lv_obj_find_by_name(root, "page_body");
    assert(page_after && is_descendant_of(marker, page_after));
    return marker;
}

lv_obj_t *navigation_button(lv_obj_t *root, const char *name)
{
    lv_obj_t *navigation = lv_obj_find_by_name(root, "app_navigation");
    assert(navigation && lv_obj_get_child_count(navigation) == 4);
    lv_obj_t *button = lv_obj_find_by_name(navigation, name);
    assert(button && lv_obj_has_class(button, &lv_button_class));
    return button;
}

void assert_horizontal_label_bounds(lv_obj_t *root)
{
    if (lv_obj_has_class(root, &lv_label_class) && lv_obj_is_visible(root)) {
        lv_obj_t *parent = lv_obj_get_parent(root);
        assert(parent);
        lv_area_t label_area{};
        lv_area_t parent_content{};
        lv_obj_get_coords(root, &label_area);
        lv_obj_get_content_coords(parent, &parent_content);
        if (label_area.x1 < parent_content.x1 - 1 || label_area.x2 > parent_content.x2 + 1) {
            std::fprintf(stderr, "label overflow: '%s' label=[%d,%d] parent=[%d,%d]\n", lv_label_get_text(root),
                         label_area.x1, label_area.x2, parent_content.x1, parent_content.x2);
        }
        assert(label_area.x1 >= parent_content.x1 - 1);
        assert(label_area.x2 <= parent_content.x2 + 1);
    }
    const auto child_count = lv_obj_get_child_count(root);
    for (std::uint32_t index = 0; index < child_count; ++index)
        assert_horizontal_label_bounds(lv_obj_get_child(root, static_cast<std::int32_t>(index)));
}

void assert_info_row_layout(lv_obj_t *title)
{
    assert(title && lv_obj_has_class(title, &lv_label_class));
    lv_obj_t *text_column = lv_obj_get_parent(title);
    assert(text_column && lv_obj_get_child_count(text_column) >= 2);
    lv_obj_t *detail = lv_obj_get_child(text_column, 1);
    assert(detail && lv_obj_has_class(detail, &lv_label_class));
    lv_obj_t *row = text_column;
    while (row && !lv_obj_has_class(row, &lv_button_class)) row = lv_obj_get_parent(row);
    assert(row);

    lv_area_t title_area{};
    lv_area_t detail_area{};
    lv_area_t row_area{};
    lv_obj_get_coords(title, &title_area);
    lv_obj_get_coords(detail, &detail_area);
    lv_obj_get_coords(row, &row_area);
    assert(title_area.y2 < detail_area.y1);
    assert(title_area.y1 >= row_area.y1 - 1);
    assert(detail_area.y2 <= row_area.y2 + 1);
}

void assert_cjk_fallback(lv_obj_t *label)
{
    assert(label && lv_obj_has_class(label, &lv_label_class));
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    assert(font);
    lv_font_glyph_dsc_t glyph{};
    assert(lv_font_get_glyph_dsc(font, &glyph, 0x4e2dU, 0));
    assert(!glyph.is_placeholder);
    assert(glyph.resolved_font);
}

void assert_minimum_hit_targets(lv_obj_t *root)
{
    const bool checks_hit_target = lv_obj_has_class(root, &lv_button_class) || lv_obj_has_class(root, &lv_slider_class);
    if (checks_hit_target && lv_obj_is_visible(root)) {
        lv_area_t click_area{};
        lv_obj_get_click_area(root, &click_area);
        if (lv_area_get_width(&click_area) < 44 || lv_area_get_height(&click_area) < 44) {
            std::fprintf(stderr, "small hit target: name=%s size=%dx%d\n", lv_obj_get_name(root),
                         lv_area_get_width(&click_area), lv_area_get_height(&click_area));
        }
        assert(lv_area_get_width(&click_area) >= 44);
        assert(lv_area_get_height(&click_area) >= 44);
    }
    const auto child_count = lv_obj_get_child_count(root);
    for (std::uint32_t index = 0; index < child_count; ++index)
        assert_minimum_hit_targets(lv_obj_get_child(root, static_cast<std::int32_t>(index)));
}

bool areas_overlap(const lv_area_t &left, const lv_area_t &right)
{
    return left.x1 <= right.x2 && right.x1 <= left.x2 && left.y1 <= right.y2 && right.y1 <= left.y2;
}

void assert_primary_layout(lv_obj_t *root, int width, int height)
{
    lv_obj_t *surface    = lv_obj_find_by_name(root, "app_surface");
    lv_obj_t *content    = lv_obj_find_by_name(root, "app_content");
    lv_obj_t *navigation = lv_obj_find_by_name(root, "app_navigation");
    assert(surface && content && navigation);
    assert(lv_obj_get_width(surface) == width);
    assert(lv_obj_get_height(surface) == height);
    if (lv_obj_has_flag(navigation, LV_OBJ_FLAG_HIDDEN)) return;

    lv_area_t surface_area{};
    lv_area_t content_area{};
    lv_area_t navigation_area{};
    lv_obj_get_coords(surface, &surface_area);
    lv_obj_get_coords(content, &content_area);
    lv_obj_get_coords(navigation, &navigation_area);
    assert(content_area.x1 >= surface_area.x1 && content_area.y1 >= surface_area.y1);
    assert(content_area.x2 <= surface_area.x2 && content_area.y2 <= surface_area.y2);
    assert(navigation_area.x1 >= surface_area.x1 && navigation_area.y1 >= surface_area.y1);
    assert(navigation_area.x2 <= surface_area.x2 && navigation_area.y2 <= surface_area.y2);
    assert(!areas_overlap(content_area, navigation_area));
    if (width >= 800) {
        assert(lv_obj_get_height(content) == height);
        assert(lv_obj_get_height(navigation) == height);
    } else {
        assert(lv_obj_get_width(content) == width);
        assert(lv_obj_get_width(navigation) == width);
    }
}

lv_obj_t *find_scroll_target(lv_obj_t *root)
{
    if (lv_obj_has_flag(root, LV_OBJ_FLAG_SCROLLABLE) && lv_obj_get_scroll_bottom(root) > 120) return root;
    const auto child_count = lv_obj_get_child_count(root);
    for (std::uint32_t index = 0; index < child_count; ++index) {
        if (auto *target = find_scroll_target(lv_obj_get_child(root, static_cast<std::int32_t>(index)))) return target;
    }
    return nullptr;
}

void resize_display(lv_display_t *display, lv_obj_t *root, int width, int height)
{
    display_width = width;
    std::fill(framebuffer.begin(), framebuffer.end(), lv_color32_t{});
    lv_display_set_resolution(display, width, height);
    lv_obj_update_layout(root);
    lv_obj_invalidate(root);
    lv_refr_now(display);
    assert_primary_layout(root, width, height);
}

void assert_render_quality()
{
    std::unordered_set<std::uint32_t> colors;
    std::size_t page_pixels      = 0;
    std::size_t container_pixels = 0;
    std::size_t dark_pixels      = 0;
    for (const auto &pixel : framebuffer) {
        const auto rgb = (static_cast<std::uint32_t>(pixel.red) << 16U) |
                         (static_cast<std::uint32_t>(pixel.green) << 8U) | pixel.blue;
        colors.insert(rgb);
        if (rgb == kColorPageBackground) ++page_pixels;
        if (rgb == kColorContainerBackground) ++container_pixels;
        if (pixel.red < 80 && pixel.green < 80 && pixel.blue < 80) ++dark_pixels;
    }
    const auto pixel_count = framebuffer.size();
    assert(colors.size() >= 16);
    assert(page_pixels > pixel_count / 50);
    assert(container_pixels > pixel_count / 100);
    assert(dark_pixels > 20);
}

}  // namespace

int main(int argc, char **argv)
{
    assert(argc == 4);
    const int initial_width  = std::atoi(argv[1]);
    const int initial_height = std::atoi(argv[2]);
    assert(initial_width >= 320 && initial_height >= 240);
    const int rotated_width  = initial_height;
    const int rotated_height = initial_width;

    FakeRadio radio;
    display_width = initial_width;
    framebuffer.resize(static_cast<std::size_t>(initial_width) * static_cast<std::size_t>(initial_height));
    const auto max_width = std::max(initial_width, rotated_width);
    std::vector<lv_color32_t> draw_buffer(static_cast<std::size_t>(max_width) * kPartialRows);

    lv_init();
    lv_display_t *display = lv_display_create(initial_width, initial_height);
    assert(display);
    lv_display_set_buffers(display, draw_buffer.data(), nullptr,
                           static_cast<std::uint32_t>(draw_buffer.size() * sizeof(lv_color32_t)),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_default(display);

    cm0_app_context_t context{};
    context.root = lv_screen_active();
    lilygo_ui_meshtastic_configure("127.0.0.1", radio.port(), true);
    const cm0_app_descriptor_t *descriptor = cm0_app_get_descriptor();
    descriptor->open(&context);
    lv_obj_update_layout(context.root);

    lv_obj_t *surface    = lv_obj_find_by_name(context.root, "app_surface");
    lv_obj_t *content    = lv_obj_find_by_name(context.root, "app_content");
    lv_obj_t *navigation = lv_obj_find_by_name(context.root, "app_navigation");
    lv_obj_t *page       = lv_obj_find_by_name(context.root, "page_body");
    assert(surface && content && navigation && page);
    assert(lv_color_eq(lv_obj_get_style_bg_color(surface, LV_PART_MAIN), lv_color_hex(kColorPageBackground)));
    assert(lv_obj_get_style_bg_opa(surface, LV_PART_MAIN) == LV_OPA_COVER);
    assert(lv_obj_get_style_bg_grad_dir(surface, LV_PART_MAIN) == LV_GRAD_DIR_NONE);
    assert(lv_color_eq(lv_obj_get_style_bg_color(navigation, LV_PART_MAIN), lv_color_hex(kColorContainerBackground)));
    assert(lv_obj_get_style_bg_opa(navigation, LV_PART_MAIN) == LV_OPA_COVER);
    assert_primary_layout(context.root, initial_width, initial_height);

    wait_until([&] {
        lv_obj_t *status = lv_obj_find_by_name(context.root, "connection_status");
        return status && std::string_view(lv_label_get_text(status)).find("Connected") != std::string_view::npos;
    });
    assert(find_label_containing(context.root, "LoRa"));
    assert_horizontal_label_bounds(lv_obj_find_by_name(context.root, "page_body"));
    assert_minimum_hit_targets(lv_obj_find_by_name(context.root, "page_body"));

    lv_obj_t *brightness_slider = lv_obj_find_by_name(context.root, "brightness_slider");
    lv_obj_t *brightness_value  = lv_obj_find_by_name(context.root, "brightness_value");
    assert(brightness_slider && brightness_value);
    lv_slider_set_value(brightness_slider, 55, LV_ANIM_OFF);
    assert(lv_obj_send_event(brightness_slider, LV_EVENT_VALUE_CHANGED, nullptr) == LV_RESULT_OK);
    assert(std::string_view(lv_label_get_text(brightness_value)).find("55%") != std::string_view::npos);

    click_and_wait(button_for_label(context.root, "Device tools"), context.root, "device_tools");
    assert(find_label_containing(context.root, "0x94 0xC3"));
    click_and_wait(button_for_label(context.root, LV_SYMBOL_LEFT), context.root, "connection_settings");

    click_and_wait(navigation_button(context.root, "nav_messages"), context.root, "messages_list");
    lv_obj_t *sender  = find_label_containing(context.root, kLongNodeName);
    lv_obj_t *message = find_label_containing(context.root, "field report wraps");
    assert(sender && message);
    assert_cjk_fallback(sender);
    assert_info_row_layout(sender);
    assert_horizontal_label_bounds(lv_obj_find_by_name(context.root, "messages_list"));
    assert_minimum_hit_targets(lv_obj_find_by_name(context.root, "page_body"));

    lv_obj_t *channels_button = lv_obj_find_by_name(context.root, "messages_channels");
    click_and_wait(channels_button, context.root, "channels_primary");
    lv_obj_t *channel_name = find_label_containing(context.root, kPrimaryChannel);
    assert(channel_name && lv_obj_find_by_name(context.root, "channels_secondary"));
    assert_cjk_fallback(channel_name);
    assert_info_row_layout(channel_name);
    assert_horizontal_label_bounds(lv_obj_find_by_name(context.root, "page_body"));
    click_and_wait(navigation_button(context.root, "nav_messages"), context.root, "messages_list");

    lv_obj_t *compose = lv_obj_find_by_name(context.root, "messages_compose");
    click_and_wait(compose, context.root, "chat_composer_input");
    lv_obj_t *chat_page     = lv_obj_find_by_name(context.root, "page_body");
    lv_obj_t *chat_messages = lv_obj_find_by_name(context.root, "chat_messages");
    lv_obj_t *composer      = lv_obj_find_by_name(context.root, "chat_composer_input");
    lv_obj_t *send          = lv_obj_find_by_name(context.root, "chat_send");
    assert(chat_page && chat_messages && composer && send);
    assert(lv_obj_has_class(composer, &lv_textarea_class));
    assert(find_label_containing(chat_messages, "field report wraps"));
    assert_horizontal_label_bounds(chat_page);

    lv_textarea_set_text(composer, kDraft);
    lv_textarea_set_cursor_pos(composer, 12);
    const auto cursor_before = lv_textarea_get_cursor_pos(composer);
    lv_group_t *focus_group  = lv_group_create();
    assert(focus_group);
    lv_group_add_obj(focus_group, composer);
    lv_group_focus_obj(composer);
    assert(lv_group_get_focused(focus_group) == composer);

    lv_obj_update_layout(context.root);
    lv_obj_t *scroll_target = find_scroll_target(chat_messages);
    if (!scroll_target) scroll_target = find_scroll_target(chat_page);
    assert(scroll_target);
    lv_obj_scroll_to_y(scroll_target, 64, LV_ANIM_OFF);
    lv_obj_update_layout(context.root);
    const auto scroll_before = lv_obj_get_scroll_y(scroll_target);
    assert(scroll_before > 0);

    resize_display(display, context.root, rotated_width, rotated_height);
    assert(lv_obj_find_by_name(context.root, "page_body") == chat_page);
    assert(lv_obj_find_by_name(context.root, "chat_messages") == chat_messages);
    assert(lv_obj_find_by_name(context.root, "chat_composer_input") == composer);
    assert(std::strcmp(lv_textarea_get_text(composer), kDraft) == 0);
    assert(lv_textarea_get_cursor_pos(composer) == cursor_before);
    assert(lv_group_get_focused(focus_group) == composer);
    if (lv_obj_get_scroll_y(scroll_target) != scroll_before) {
        std::fprintf(stderr, "scroll changed: before=%d after=%d target=%s bottom=%d\n", scroll_before,
                     lv_obj_get_scroll_y(scroll_target), lv_obj_get_name(scroll_target),
                     lv_obj_get_scroll_bottom(scroll_target));
    }
    assert(lv_obj_get_scroll_y(scroll_target) == scroll_before);
    assert_horizontal_label_bounds(chat_page);
    lv_group_delete(focus_group);

    click_and_wait(button_for_label(context.root, LV_SYMBOL_LEFT), context.root, "messages_list");
    click_and_wait(navigation_button(context.root, "nav_nodes"), context.root, "nodes_list");
    lv_obj_t *node_name = find_label_containing(context.root, kLongNodeName);
    assert(node_name);
    assert_cjk_fallback(node_name);
    assert_info_row_layout(node_name);
    assert_horizontal_label_bounds(lv_obj_find_by_name(context.root, "nodes_list"));

    click_and_wait(button_for_label(context.root, kLongNodeName), context.root, "node_detail");
    lv_obj_t *detail_name = find_label_containing(context.root, kLongNodeName);
    assert(detail_name);
    assert_cjk_fallback(detail_name);
    assert(find_label_containing(context.root, "0x87654321"));
    assert_horizontal_label_bounds(lv_obj_find_by_name(context.root, "node_detail"));
    click_and_wait(button_for_label(context.root, LV_SYMBOL_LEFT), context.root, "nodes_list");

    click_and_wait(navigation_button(context.root, "nav_map"), context.root, "map_canvas");
    assert(find_label_containing(context.root, "Offline map ready"));
    assert(find_label_containing(context.root, kLongNodeName));
    click_and_wait(navigation_button(context.root, "nav_connect"), context.root, "connection_settings");

    page = lv_obj_find_by_name(context.root, "page_body");
    resize_display(display, context.root, initial_width, initial_height);
    assert(lv_obj_find_by_name(context.root, "page_body") == page);
    brightness_slider = lv_obj_find_by_name(context.root, "brightness_slider");
    brightness_value  = lv_obj_find_by_name(context.root, "brightness_value");
    assert(brightness_slider && brightness_value);
    assert(lv_slider_get_value(brightness_slider) == 55);
    assert(std::string_view(lv_label_get_text(brightness_value)).find("55%") != std::string_view::npos);
    assert_minimum_hit_targets(lv_obj_find_by_name(context.root, "page_body"));

    std::fill(framebuffer.begin(), framebuffer.end(), lv_color32_t{});
    lv_obj_invalidate(context.root);
    lv_refr_now(display);
    assert_render_quality();
    assert(write_ppm(argv[3], initial_width, initial_height));

    descriptor->close();
    lv_display_delete(display);
    lilygo_ui_fonts_deinit();
    lv_deinit();
    return 0;
}
