#include "maps/domain/map_model.hpp"
#include "maps/domain/tile_cache.hpp"
#include "maps/services/tile_service.hpp"
#include "pages/map/map_view_model.hpp"
#include "persistence/meshtastic_repository.hpp"

#include <png.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <thread>
#include <unistd.h>

namespace {
bool near(double a, double b, double epsilon = 0.00001) { return std::abs(a - b) < epsilon; }
std::filesystem::path temporary_root()
{
    auto pattern = (std::filesystem::temp_directory_path() / "meshtastic-map-test-XXXXXX").string();
    assert(::mkdtemp(pattern.data())); return pattern;
}
std::string invalid_header_png()
{
    // This old probe-only fixture has no pixels, IDAT or valid IHDR CRC.
    static const unsigned char png[] = {0x89,'P','N','G',0x0d,0x0a,0x1a,0x0a,
        0,0,0,13,'I','H','D','R',0,0,1,0,0,0,1,0,8,2,0,0,0,0,0,0,0,
        0,0,0,0,'I','E','N','D',0xae,0x42,0x60,0x82};
    return {reinterpret_cast<const char *>(png), sizeof(png)};
}
std::string tile_png()
{
    static const auto png = [] {
        const auto path=std::filesystem::path(__FILE__).parent_path()/"fixtures/maps/openstreetmap/14/13382/7137.png";
        std::ifstream stream(path,std::ios::binary); assert(stream);
        return std::string(std::istreambuf_iterator<char>(stream),{});
    }();
    return png;
}
std::string transparent_png()
{
    png_image image{}; image.version=PNG_IMAGE_VERSION; image.width=256; image.height=256; image.format=PNG_FORMAT_RGBA;
    std::vector<png_byte> pixels(PNG_IMAGE_SIZE(image));
    for(std::size_t i=0;i<pixels.size();i+=4) { pixels[i]=32; pixels[i+1]=96; pixels[i+2]=128; pixels[i+3]=(i/4)%2?128:0; }
    png_alloc_size_t size=0;
    assert(png_image_write_to_memory(&image,nullptr,&size,0,pixels.data(),0,nullptr));
    std::string result(size,'\0');
    assert(png_image_write_to_memory(&image,result.data(),&size,0,pixels.data(),0,nullptr));
    result.resize(size); png_image_free(&image); return result;
}
std::string corrupt_tile_png()
{
    auto result=tile_png();
    const auto idat=result.find("IDAT"); assert(idat!=std::string::npos && idat+5<result.size());
    result[idat+4]^=0x80; return result;
}
template <typename Condition> void wait_until(Condition condition)
{
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!condition()) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
}
void write_file(const std::filesystem::path &path,const std::string &contents)
{
    std::ofstream stream(path,std::ios::binary); stream<<contents; assert(stream.good());
}
int fake_curl(int argc,char **argv)
{
    assert(argc>1);
    const auto *configured=std::getenv("MESHTASTIC_TEST_MAP_HTTP_ROOT");
    assert(configured);
    const std::filesystem::path root(configured);
    const std::string url=argv[argc-1];
    const bool tile=url.find("tile.openstreetmap.de")!=std::string::npos;
    const std::string job=tile ? "tile" : url.find("first")!=std::string::npos ? "first" : "second";
    write_file(root/(job+".started"),url);
    if(std::filesystem::exists(root/(job+".block")))
        wait_until([&]{return std::filesystem::exists(root/(job+".release"));});
    const std::string body=tile ? tile_png() :
        "[{\"lat\":\"31.23\",\"lon\":\"121.47\",\"display_name\":\""+job+"\"}]";
    assert(::write(STDOUT_FILENO,body.data(),body.size())==static_cast<ssize_t>(body.size()));
    write_file(root/(job+".finished"),"");
    return 0;
}
void async_generations_and_clear(const char *executable)
{
    const auto root=temporary_root();
    const auto *path=std::getenv("PATH"); const std::string old_path=path?path:"";
    std::filesystem::create_symlink(std::filesystem::canonical(executable),root/"curl");
    ::setenv("PATH",(root.string()+":"+old_path).c_str(),1);
    ::setenv("MESHTASTIC_TEST_MAP_HTTP_ROOT",root.c_str(),1);
    ::unsetenv("LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK");
    std::filesystem::create_directories(root/"openstreetmap/3/4");
    write_file(root/"openstreetmap/3/4/5.png","");
    {
        meshmap::TileService service(root/"openstreetmap");
        meshmap::MapEvent event;
        wait_until([&]{return service.poll(event) && event.kind==meshmap::EventKind::cache_stats_ready;});
        assert(service.cache_stats().tiles==1 && service.cache_stats().bytes==0);
        write_file(root/"first.block","");
        const auto first=service.search("first");
        wait_until([&]{return std::filesystem::exists(root/"first.started");});
        const auto second=service.search("second");
        wait_until([&]{while(service.poll(event)) if(event.kind!=meshmap::EventKind::cache_stats_ready) return true; return false;});
        assert(event.kind==meshmap::EventKind::search_ready && event.generation==second && event.generation!=first);
        write_file(root/"first.release","");
        wait_until([&]{return std::filesystem::exists(root/"first.finished");});
        // An unrelated tile event proves the first worker has passed publication;
        // the stale search result must never become visible through poll().
        write_file(root/"tile.block","");
        service.request_tiles(11,{{3,4,5}});
        wait_until([&]{return std::filesystem::exists(root/"tile.started");});
        service.request_tiles(12,{{3,4,5}});
        write_file(root/"tile.release","");
        wait_until([&]{while(service.poll(event)) if(event.kind!=meshmap::EventKind::cache_stats_ready) return true; return false;});
        assert(event.kind==meshmap::EventKind::tile_ready && event.generation==12);
        assert(service.cache_stats().tiles==1 && service.cache_stats().bytes==tile_png().size());
        assert(!service.poll(event));
        // A file replaced after validation must disappear from find_tile(),
        // decode as corrupt on the worker and be fetched again.
        write_file(root/"openstreetmap/3/4/5.png",corrupt_tile_png());
        std::filesystem::path validated; assert(!service.find_tile({3,4,5},validated));
        service.request_tiles(13,{{3,4,5}});
        wait_until([&]{while(service.poll(event)) if(event.kind!=meshmap::EventKind::cache_stats_ready) return true; return false;});
        assert(event.kind==meshmap::EventKind::tile_ready && event.generation==13);
        assert(service.find_tile({3,4,5},validated));
        assert(service.cache_stats().tiles==1 && service.cache_stats().bytes==tile_png().size());
        std::filesystem::remove(root/"tile.started"); std::filesystem::remove(root/"tile.finished");
        std::filesystem::remove(root/"tile.release");
        service.request_tiles(13,{{3,4,6}});
        wait_until([&]{return std::filesystem::exists(root/"tile.started");});
        service.clear_cache();
        wait_until([&]{while(service.poll(event)) if(event.kind!=meshmap::EventKind::cache_stats_ready) return true; return false;});
        assert(event.kind==meshmap::EventKind::cache_cleared);
        assert(service.cache_stats().tiles==0 && service.cache_stats().bytes==0);
        write_file(root/"tile.release","");
        wait_until([&]{return std::filesystem::exists(root/"tile.finished");});
    }
    meshmap::TileCache cache(root/"openstreetmap");
    assert(cache.stats().tiles==0);
    ::setenv("PATH",old_path.c_str(),1); ::unsetenv("MESHTASTIC_TEST_MAP_HTTP_ROOT");
    ::setenv("LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK","1",1);
    std::filesystem::remove_all(root);
}
void projection_and_overlays()
{
    using meshmap::GeoPoint;
    meshmap::MapModel model;
    model.center_on({31.2304,121.4737},15);
    const auto center = model.project(model.center(),320,568);
    assert(near(center.x,160)); assert(near(center.y,284));
    const auto north = model.project({31.2314,121.4737},320,568);
    const auto east = model.project({31.2304,121.4747},320,568);
    assert(north.y<center.y && east.x>center.x);
    for (const int zoom : {2,8,18}) {
        const double x=meshmap::MapModel::longitude_to_world_x(121.4737,zoom);
        const double y=meshmap::MapModel::latitude_to_world_y(31.2304,zoom);
        assert(near(meshmap::MapModel::world_x_to_longitude(x,zoom),121.4737));
        assert(near(meshmap::MapModel::world_y_to_latitude(y,zoom),31.2304));
    }
    model.fit_points({{10,179.8},{10,-179.8}},568,400);
    assert(std::abs(model.center().longitude)>179);
    assert(model.zoom()>2);
    const auto left=model.project({10,179.8},568,400),right=model.project({10,-179.8},568,400);
    assert(std::abs(left.x-right.x)<500);
    assert(left.x>=0 && right.x<568);
    model.center_on({0,0},10);
    const auto segments=model.project_path({{0,-1},{0,1}},320,568);
    assert(segments.size()==1);
    assert(near(segments.front().first.x,0) && near(segments.front().second.x,319));
    const auto outside=model.project_path({{80,-1},{80,1}},320,568);
    assert(outside.empty());
    assert(meshmap::MapModel::valid_point({0,0}));
    assert(!meshmap::MapModel::valid_point({91,0}));
    assert(!meshmap::MapModel::valid_point({0,std::numeric_limits<double>::quiet_NaN()}));
    assert(model.visible_tiles(0,0).empty());
}
void cache_and_offline()
{
    const auto root=temporary_root();
    const auto cache_root=root/"openstreetmap";
    meshmap::TileCache cache(cache_root);
    std::string error;
    std::filesystem::path path;
    assert(cache.store({3,4,5},tile_png(),path,error));
    assert(path==cache_root/"3/4/5.png");
    assert(cache.find({3,4,5},path));
    assert(cache.stats().tiles==1);
    assert(!cache.store({3,4,6},"not PNG",path,error));
    assert(!cache.store({3,4,6},invalid_header_png(),path,error));
    assert(!cache.store({3,4,6},corrupt_tile_png(),path,error));
    auto checksum=tile_png(); const auto idat=checksum.find("IDAT"); assert(idat!=std::string::npos && idat>=4);
    const auto *length=reinterpret_cast<const unsigned char *>(checksum.data()+idat-4);
    const auto chunk_size=(std::size_t(length[0])<<24U)|(std::size_t(length[1])<<16U)|(std::size_t(length[2])<<8U)|length[3];
    assert(idat+4+chunk_size<checksum.size()); checksum[idat+4+chunk_size]^=1;
    assert(!cache.store({3,4,6},checksum,path,error));
    auto truncated=tile_png(); truncated.erase(truncated.size()/2,16);
    assert(!cache.store({3,4,6},truncated,path,error));
    assert(meshmap::TileCache::valid_tile_png(transparent_png()));
    assert(cache.store({3,4,7},transparent_png(),path,error));
    // Existing files are invisible until decoded in this cache instance.
    meshmap::TileCache reloaded(cache_root);
    assert(!reloaded.find({3,4,5},path));
    assert(reloaded.validate({3,4,5},path,error) && reloaded.find({3,4,5},path));
    write_file(path,invalid_header_png());
    assert(!reloaded.find({3,4,5},path));
    assert(!reloaded.validate({3,4,5},path,error));
    assert(cache.store({3,4,5},tile_png(),path,error));
    write_file(cache_root/"3/4/6.png",corrupt_tile_png());
    assert(!std::filesystem::exists(cache_root/"3/4/6.png.part"));
    {
        meshmap::TileService service(cache_root);
        service.request_tiles(1,{{3,4,5},{3,4,6},{3,4,7}});
        meshmap::MapEvent event; bool opaque=false,transparent=false,corrupt=false;
        wait_until([&]{
            while(service.poll(event)) {
                if(event.kind==meshmap::EventKind::tile_ready && event.tile.y==5) opaque=true;
                if(event.kind==meshmap::EventKind::tile_ready && event.tile.y==7) transparent=true;
                if(event.kind==meshmap::EventKind::tile_error && event.tile.y==6) corrupt=true;
            }
            return opaque && transparent && corrupt;
        });
        assert(service.find_tile({3,4,5},path));
        assert(service.find_tile({3,4,7},path));
        assert(!service.find_tile({3,4,6},path));
        assert(service.tile_url({3,4,5})=="https://tile.openstreetmap.de/3/4/5.png");
        assert(service.url_encode("上海 A")=="%E4%B8%8A%E6%B5%B7%20A");
        service.search("offline test");
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        do { while(!service.poll(event)) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); } }
        while(event.kind!=meshmap::EventKind::search_error);
        assert(event.kind==meshmap::EventKind::search_error);
        assert(event.error.find("internet")!=std::string::npos);
        service.clear_cache();
        do { while(!service.poll(event)) { assert(std::chrono::steady_clock::now()<deadline); std::this_thread::yield(); } }
        while(event.kind!=meshmap::EventKind::cache_cleared);
        assert(event.kind==meshmap::EventKind::cache_cleared);
        assert(!service.find_tile({3,4,5},path));
        // Clear races with background disk validation as well as downloads.
        std::filesystem::create_directories(cache_root/"6/4");
        std::vector<meshmap::TileId> queued;
        for(int y=0;y<64;++y) {
            queued.push_back({6,4,y});
            write_file(cache_root/"6/4"/(std::to_string(y)+".png"),tile_png());
        }
        service.request_tiles(2,queued); service.clear_cache();
        wait_until([&]{
            while(service.poll(event)) {
                assert(event.kind!=meshmap::EventKind::tile_ready);
                if(event.kind==meshmap::EventKind::cache_cleared) return true;
            }
            return false;
        });
        assert(meshmap::TileCache(cache_root).stats().tiles==0);
        for(const auto &tile:queued) assert(!service.find_tile(tile,path));
    }
    meshmap::TileCache unsafe(root);
    assert(!unsafe.clear(error));
    assert(std::filesystem::exists(root));
    meshmap::HttpClient http;
    assert(!http.get("http://example.com/", "image/png", 1024));
    assert(!http.get("file:///etc/passwd", "image/png", 1024));
    std::filesystem::remove_all(root);
}
void snapshot_overlays_and_follow()
{
    const auto root=temporary_root();
    {
        MapViewModel model(root/"openstreetmap");
        meshtastic::ClientSnapshot snapshot;
        snapshot.my_node_number=1;
        meshtastic::Node local; local.number=1; local.long_name="Local"; local.latitude=31.23; local.longitude=121.47;
        meshtastic::Node remote; remote.number=2; remote.long_name="远端 Remote"; remote.latitude=31.24; remote.longitude=121.48;
        snapshot.nodes={local,remote};
        for (int i=0;i<3;++i) {
            meshtastic::PositionRecord record; record.local_id=i+1; record.node_number=2;
            record.position.set_latitude_i(312300000+i*10000); record.position.set_longitude_i(1214700000+i*10000);
            snapshot.positions.push_back(record);
        }
        meshtastic::WaypointRecord waypoint;
        waypoint.waypoint.set_id(7); waypoint.waypoint.set_name("Camp");
        waypoint.waypoint.set_latitude_i(0); waypoint.waypoint.set_longitude_i(0);
        snapshot.waypoints.push_back(waypoint);
        waypoint.waypoint.set_id(8); waypoint.waypoint.set_expire(1);
        snapshot.waypoints.push_back(waypoint);
        meshtastic::RouteRecord pending_route; pending_route.from=1; pending_route.to=2; pending_route.outgoing=true;
        snapshot.routes.push_back(pending_route);
        meshtastic::RouteRecord route; route.from=2; route.to=1; route.request_id=42;
        snapshot.routes.push_back(route);
        model.sync(snapshot);
        assert(model.markers().size()==2 && model.waypoints().size()==1);
        assert(near(model.model().center().latitude,*local.latitude));
        assert(model.paths().size()==2);
        assert(model.paths().front().points.size()==3);
        assert(model.paths().back().route);
        assert(near(model.paths().back().points.front().latitude,*local.latitude));
        assert(near(model.paths().back().points.back().latitude,*remote.latitude));
        model.select_node(2); model.follow_selected();
        assert(model.selected_node()==2 && model.following());
        assert(near(model.model().center().latitude,*remote.latitude));
        snapshot.nodes.back().latitude=31.25; model.sync(snapshot);
        assert(near(model.model().center().latitude,31.25));
        model.pan_pixels(100,0);
        const auto browsing=model.model().center();
        snapshot.nodes.back().latitude=31.26; model.sync(snapshot);
        assert(near(model.model().center().latitude,browsing.latitude));
        assert(!model.following());
        model.follow_local(); assert(near(model.model().center().latitude,*local.latitude));
        model.select_waypoint(7); assert(model.has_selection() && model.selected_node()==0);
        const auto revision=lv_subject_get_int(model.content_subject());
        model.sync(snapshot); assert(lv_subject_get_int(model.content_subject())==revision);
    }
    std::filesystem::remove_all(root);
}
void follow_before_activation()
{
    const auto root = temporary_root();
    const auto database = root / "history.sqlite3";
    {
        meshtastic::Repository repository(database);
        const auto session = repository.activate("map-focus.test:4403", 1);
        meshtastic::Node local; local.number = 1; local.long_name = "Local";
        local.latitude = 31.23; local.longitude = 121.47;
        meshtastic::Node remote; remote.number = 2; remote.long_name = "Remote";
        remote.latitude = 31.25; remote.longitude = 121.49;
        repository.store_node(session, local);
        repository.store_node(session, remote);
        for (std::uint32_t i = 1; i <= 205; ++i) {
            meshtastic::PositionRecord position;
            position.node_number = 2;
            position.packet_id = i;
            position.position.set_latitude_i(312500000 + static_cast<std::int32_t>(i));
            position.position.set_longitude_i(1214900000 + static_cast<std::int32_t>(i));
            repository.store_position(session, position);
        }
    }
    {
        meshtastic::MeshtasticClient client({"map-focus.test", 4403}, database);
        MapViewModel model(root / "openstreetmap");
        model.set_client(&client);
        model.sync(client.snapshot());
        assert(model.markers().empty());
        model.select_node(2);
        model.follow_selected();
        model.set_active(true);
        assert(model.selected_node() == 2 && model.following());
        assert(near(model.model().center().latitude, 31.25));
        assert(near(model.model().center().longitude, 121.49));
        assert(model.model().zoom() == 15);
        // No new snapshot arrives after activating the page. Polling must still
        // apply the worker's complete history beyond the 200-point snapshot.
        wait_until([&] {
            meshmap::MapEvent event;
            model.poll(event);
            for (const auto &path : model.paths())
                if (!path.route && path.node_number == 2 && path.points.size() == 205) return true;
            return false;
        });
        model.set_active(false);
    }
    {
        MapViewModel model(root / "openstreetmap");
        meshtastic::ClientSnapshot snapshot;
        snapshot.my_node_number = 1;
        meshtastic::Node local; local.number = 1; local.long_name = "Local";
        local.latitude = 31.23; local.longitude = 121.47;
        snapshot.nodes.push_back(local);
        model.select_node(2);
        model.follow_selected();
        model.sync(snapshot);
        assert(model.following());
        meshtastic::Node remote; remote.number = 2; remote.long_name = "Remote";
        remote.latitude = 31.25; remote.longitude = 121.49;
        snapshot.nodes.push_back(remote);
        model.sync(snapshot);
        assert(near(model.model().center().latitude, 31.25));
        assert(model.model().zoom() == 15);
    }
    std::filesystem::remove_all(root);
}
void waypoint_precision_updates()
{
    const auto root = temporary_root();
    {
        MapViewModel model(root / "openstreetmap");
        meshtastic::ClientSnapshot snapshot;
        meshtastic::WaypointRecord waypoint;
        waypoint.waypoint.set_id(5);
        waypoint.waypoint.set_name("Precise");
        waypoint.waypoint.set_latitude_i(312304000);
        waypoint.waypoint.set_longitude_i(1214737000);
        snapshot.waypoints.push_back(waypoint);
        model.sync(snapshot);
        const auto revision = lv_subject_get_int(model.content_subject());
        snapshot.waypoints[0].waypoint.set_latitude_i(312304001);
        model.sync(snapshot);
        assert(lv_subject_get_int(model.content_subject()) != revision);
        assert(near(model.waypoints().front().point.latitude, 31.2304001, 0.00000001));
    }
    std::filesystem::remove_all(root);
}
}  // namespace
int main(int argc,char **argv)
{
    if(argc>1) return fake_curl(argc,argv);
    ::setenv("LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK","1",1);
    projection_and_overlays(); cache_and_offline(); async_generations_and_clear(argv[0]);
    lv_init(); snapshot_overlays_and_follow(); follow_before_activation(); waypoint_precision_updates(); lv_deinit();
    return 0;
}
