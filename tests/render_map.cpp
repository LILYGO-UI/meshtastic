#include "pages/map/map_view.hpp"

#include <cm0/typography.h>
#include <png.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {
std::vector<lv_color32_t> framebuffer;
int display_width = 0;
lv_point_t pointer_point{};
lv_indev_state_t pointer_state=LV_INDEV_STATE_RELEASED;
void read_pointer(lv_indev_t *,lv_indev_data_t *data) { data->point=pointer_point; data->state=pointer_state; }
void flush(lv_display_t *display,const lv_area_t *area,std::uint8_t *pixels)
{
    auto *source=reinterpret_cast<lv_color32_t *>(pixels);
    const int width=area->x2-area->x1+1;
    for(int y=area->y1;y<=area->y2;++y)
        std::copy_n(source+static_cast<std::size_t>(y-area->y1)*width,width,
                    framebuffer.begin()+static_cast<std::size_t>(y)*display_width+area->x1);
    lv_display_flush_ready(display);
}
void pump(int duration=100)
{
    for(int elapsed=0;elapsed<duration;elapsed+=5) { lv_tick_inc(5); lv_timer_handler(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    lv_obj_update_layout(lv_screen_active());
}
void capture(lv_display_t *display,const std::string &path)
{
    pump(); lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    std::ofstream file(path,std::ios::binary); assert(file);
    file<<"P6\n"<<display_width<<' '<<lv_display_get_vertical_resolution(display)<<"\n255\n";
    for(const auto pixel:framebuffer) { file.put(pixel.red); file.put(pixel.green); file.put(pixel.blue); }
    assert(file.good());
}
void click(lv_obj_t *root,const char *name)
{
    auto *object=lv_obj_find_by_name(root,name); assert(object && lv_obj_is_visible(object));
    lv_obj_send_event(object,LV_EVENT_CLICKED,nullptr); pump();
}
void assert_visible(lv_obj_t *object)
{
    assert(object && lv_obj_is_visible(object));
    lv_area_t area{}; lv_obj_get_coords(object,&area);
    assert(area.x1>=0 && area.y1>=0);
    assert(area.x2<lv_display_get_horizontal_resolution(nullptr));
    assert(area.y2<lv_display_get_vertical_resolution(nullptr));
}
std::size_t image_count(lv_obj_t *root)
{
    std::size_t count=lv_obj_has_class(root,&lv_image_class)?1:0;
    for(std::uint32_t i=0;i<lv_obj_get_child_count(root);++i) count+=image_count(lv_obj_get_child(root,i));
    return count;
}
bool near(double a,double b) { return std::abs(a-b)<0.00001; }
void verify_transparent_cached_tile(const std::filesystem::path &cache,lv_display_t *display)
{
    png_image png{}; png.version=PNG_IMAGE_VERSION; png.width=256; png.height=256; png.format=PNG_FORMAT_RGBA;
    std::vector<png_byte> pixels(PNG_IMAGE_SIZE(png));
    for(std::size_t i=0;i<pixels.size();i+=4) { pixels[i]=255; pixels[i+3]=(i/4)%256<128?0:128; }
    png_alloc_size_t bytes=0; assert(png_image_write_to_memory(&png,nullptr,&bytes,0,pixels.data(),0,nullptr));
    std::string encoded(bytes,'\0'); assert(png_image_write_to_memory(&png,encoded.data(),&bytes,0,pixels.data(),0,nullptr));
    encoded.resize(bytes); png_image_free(&png);
    meshmap::TileCache tile_cache(cache); std::string error; std::filesystem::path path;
    assert(tile_cache.store({3,1,1},encoded,path,error));
    lv_obj_set_style_bg_color(lv_screen_active(),lv_color_hex(0x14283c),0);
    lv_obj_set_style_bg_opa(lv_screen_active(),LV_OPA_COVER,0);
    auto *image=lv_image_create(lv_screen_active()); const auto source="A:"+path.string();
    lv_image_set_src(image,source.c_str()); lv_obj_set_pos(image,10,10);
    pump(); lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    const auto transparent=framebuffer[30*display_width+30],blended=framebuffer[30*display_width+190];
    assert(transparent.red==20 && transparent.green==40 && transparent.blue==60);
    assert(std::abs(int(blended.red)-138)<=2 && std::abs(int(blended.green)-20)<=2 && std::abs(int(blended.blue)-30)<=2);
    lv_obj_delete(image);
}
}  // namespace

int main(int argc,char **argv)
{
    assert(argc==4);
    const int width=std::atoi(argv[1]),height=std::atoi(argv[2]);
    display_width=width; framebuffer.resize(static_cast<std::size_t>(width)*height);
    const int max_width=std::max(width,height);
    std::vector<lv_color32_t> buffer(static_cast<std::size_t>(max_width)*80);
    auto pattern=(std::filesystem::temp_directory_path()/"meshtastic-map-render-XXXXXX").string();
    assert(::mkdtemp(pattern.data())); const std::filesystem::path temporary(pattern);
    const auto fixtures=std::filesystem::path(__FILE__).parent_path()/"fixtures/maps/openstreetmap";
    const auto cache=temporary/"openstreetmap";
    std::filesystem::copy(fixtures,cache,std::filesystem::copy_options::recursive);
    ::setenv("LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK","1",1);
    lv_init();
    auto *display=lv_display_create(width,height); assert(display);
    lv_display_set_buffers(display,buffer.data(),nullptr,static_cast<std::uint32_t>(buffer.size()*sizeof(lv_color32_t)),LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display,flush); lv_display_set_default(display);
    auto *pointer=lv_indev_create(); lv_indev_set_type(pointer,LV_INDEV_TYPE_POINTER); lv_indev_set_read_cb(pointer,read_pointer);
    {
        MapViewModel model(cache);
        meshtastic::ClientSnapshot snapshot;
        snapshot.my_node_number=1;
        meshtastic::Node local; local.number=1; local.long_name="Local Radio"; local.latitude=22.5633; local.longitude=114.0491;
        meshtastic::Node remote; remote.number=2; remote.long_name="山脊中继站 Alpha long node name";
        remote.latitude=22.5672; remote.longitude=114.0542;
        snapshot.nodes={local,remote};
        meshtastic::WaypointRecord waypoint; waypoint.waypoint.set_id(7); waypoint.waypoint.set_name("集合点 Camp");
        waypoint.waypoint.set_description("Trail meeting point");
        waypoint.waypoint.set_latitude_i(225610000); waypoint.waypoint.set_longitude_i(1140432000);
        snapshot.waypoints.push_back(waypoint);
        for(int i=0;i<8;++i) {
            meshtastic::PositionRecord position; position.local_id=i+1; position.node_number=2;
            position.position.set_latitude_i(225633000+i*5000); position.position.set_longitude_i(1140491000+i*7000);
            snapshot.positions.push_back(position);
        }
        model.sync(snapshot); model.fit_nodes();
        // The fixture tiles use zoom 14; retain their authentic scale while the
        // viewport varies so image rendering is exercised in every orientation.
        while(model.model().zoom()>14) model.zoom_out();
        while(model.model().zoom()<14) model.zoom_in();
        std::uint32_t selected_detail=0;
        AppNavigationCommands navigation{}; navigation.context=&selected_detail;
        navigation.node_detail=[](void *context,std::uint32_t node){*static_cast<std::uint32_t *>(context)=node;};
        MapView view(model,navigation);
        auto *page=view.create(lv_screen_active()); pump();
        const auto image_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(image_count(page)==0) { assert(std::chrono::steady_clock::now()<image_deadline); pump(); }
        assert(lv_obj_find_by_name(page,"map_canvas"));
        assert(image_count(page)>0);
        assert(lv_obj_find_by_name(page,"map_waypoint"));
        assert(lv_obj_find_by_name(page,"map_position_history"));
        for(const auto *name:{"map_zoom_in","map_zoom_out","map_locate","map_fit_nodes","map_search_input"})
            assert_visible(lv_obj_find_by_name(page,name));
        capture(display,argv[3]);

        auto *marker=lv_obj_find_by_name(page,"map_node_marker"); assert(marker);
        lv_area_t marker_area{}; lv_obj_get_coords(marker,&marker_area);
        pointer_point={(marker_area.x1+marker_area.x2)/2,(marker_area.y1+marker_area.y2)/2};
        pointer_state=LV_INDEV_STATE_PRESSED; lv_indev_read(pointer);
        pointer_state=LV_INDEV_STATE_RELEASED; lv_indev_read(pointer); pump();
        assert(model.selected_node()!=0);
        assert_visible(lv_obj_find_by_name(page,"map_selection"));
        click(page,"map_node_details"); assert(selected_detail==model.selected_node());
        click(page,"map_follow_selected"); assert(model.following());
        click(page,"map_zoom_in"); const auto zoom=model.model().zoom();
        click(page,"map_zoom_out"); assert(model.model().zoom()==zoom-1);
        auto *canvas=lv_obj_find_by_name(page,"map_canvas"); lv_area_t canvas_area{}; lv_obj_get_coords(canvas,&canvas_area);
        pointer_point={canvas_area.x1+lv_area_get_width(&canvas_area)/4,canvas_area.y1+lv_area_get_height(&canvas_area)/3};
        pointer_state=LV_INDEV_STATE_PRESSED; lv_indev_read(pointer);
        pointer_point.x+=30; pointer_point.y+=15; lv_indev_read(pointer);
        pointer_state=LV_INDEV_STATE_RELEASED; lv_indev_read(pointer); pump(); assert(!model.following());
        click(page,"map_locate"); assert(model.following());
        assert(near(model.model().center().latitude,*local.latitude));
        model.select_node(2); model.follow_selected(); pump();
        while(model.model().zoom()>14) model.zoom_out();
        pump(); capture(display,std::string(argv[3])+"-selected.ppm");
        const auto center=model.model().center(); const auto selected=model.selected_node();
        auto *search=lv_obj_find_by_name(page,"map_search_input");
        lv_textarea_set_text(search,"Shenzhen 深圳");
        display_width=height; framebuffer.resize(static_cast<std::size_t>(height)*width);
        lv_display_set_resolution(display,height,width); pump(200);
        assert(lv_obj_find_by_name(lv_screen_active(),"map_search_input")==search);
        assert(std::string(lv_textarea_get_text(search))=="Shenzhen 深圳");
        assert(model.selected_node()==selected && near(model.model().center().latitude,center.latitude));
        assert_visible(lv_obj_find_by_name(page,"map_selection"));
        for(const auto *name:{"map_zoom_in","map_zoom_out","map_locate"}) assert_visible(lv_obj_find_by_name(page,name));
        capture(display,std::string(argv[3])+"-rotated.ppm");
        click(page,"map_search_button"); pump();
        assert(std::string(lv_subject_get_string(model.status_subject())).find("internet")!=std::string::npos);
        // Header cache action opens a scoped modal; clearing removes only the
        // isolated test cache and leaves mesh overlays intact.
        auto *clear_label=lv_obj_find_by_name(page,"map_cache_confirmation");
        assert(clear_label && lv_obj_has_flag(clear_label,LV_OBJ_FLAG_HIDDEN));
        // Find the header action through its callback-independent label text.
        auto find_clear=[](auto &&self,lv_obj_t *object)->lv_obj_t * {
            if(lv_obj_has_class(object,&lv_label_class) && std::string(lv_label_get_text(object))=="Clear cache")
                return lv_obj_get_parent(object);
            for(std::uint32_t i=0;i<lv_obj_get_child_count(object);++i)
                if(auto *found=self(self,lv_obj_get_child(object,i))) return found;
            return nullptr;
        };
        auto *clear=find_clear(find_clear,page); assert(clear); lv_obj_send_event(clear,LV_EVENT_CLICKED,nullptr); pump();
        assert(!lv_obj_has_flag(clear_label,LV_OBJ_FLAG_HIDDEN));
        click(page,"map_cache_confirm"); pump();
        assert(meshmap::TileCache(cache).stats().tiles==0);
        assert(model.markers().size()==2 && model.waypoints().size()==1);
        view.destroy(); pump();
        assert(!lv_obj_find_by_name(lv_screen_active(),"map_canvas"));
        view.create(lv_screen_active()); pump(); assert(model.selected_node()==selected);
        view.destroy();
    }
    verify_transparent_cached_tile(cache,display);
    lv_indev_delete(pointer); lv_display_delete(display); lilygo_ui_fonts_deinit(); lv_deinit();
    std::filesystem::remove_all(temporary);
    return 0;
}
