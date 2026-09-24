#ifndef LILYGO_UI_MESHTASTIC_MAP_VIEW_MODEL_HPP
#define LILYGO_UI_MESHTASTIC_MAP_VIEW_MODEL_HPP

#include "components/lv_subject.hpp"
#include "domain/meshtastic_client.hpp"

#include <string>
#include <vector>

struct MapMarkerViewModel {
    std::string name;
    double latitude  = 0.0;
    double longitude = 0.0;
    bool local       = false;
};

class MapViewModel {
public:
    MapViewModel();

    MapViewModel(const MapViewModel &)            = delete;
    MapViewModel &operator=(const MapViewModel &) = delete;
    MapViewModel(MapViewModel &&)                 = delete;
    MapViewModel &operator=(MapViewModel &&)      = delete;

    void sync(const meshtastic::ClientSnapshot &snapshot);

    [[nodiscard]] const std::vector<MapMarkerViewModel> &markers() const noexcept;
    [[nodiscard]] lv_subject_t *content_subject() noexcept;
    [[nodiscard]] lv_subject_t *status_subject() noexcept;

private:
    void publish_content();

    IntSubject content_revision_;
    StringSubject<96> status_;
    std::vector<MapMarkerViewModel> markers_;
};

#endif
