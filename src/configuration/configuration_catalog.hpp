#ifndef LILYGO_UI_MESHTASTIC_CONFIGURATION_CATALOG_HPP
#define LILYGO_UI_MESHTASTIC_CONFIGURATION_CATALOG_HPP

#include <meshtastic/config.pb.h>
#include <meshtastic/module_config.pb.h>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace meshtastic::configuration {
enum class Scope { device, module };
enum class FieldType { boolean, integer, decimal, text, enumeration };
struct Choice { int value; std::string label; };

// Only the listed fields are edited; the device's complete message is retained.
struct Field {
    std::string id, label, help;
    FieldType type;
    double minimum = 0, maximum = 0;
    std::size_t maximum_bytes = 0;
    bool secret = false;
    std::vector<Choice> choices;
    std::function<std::string(const Config &, const ModuleConfig &)> read;
    std::function<void(Config &, ModuleConfig &, const std::string &)> write;
};
struct Category {
    Scope scope;
    int payload_case, admin_type;
    std::string title;
    std::vector<Field> fields;
};
const std::vector<Category> &catalog();
bool validate(const Field &field, const std::string &value, std::string &error);
class Draft {
public:
    bool load(const Category &category, const Config &config);
    bool load(const Category &category, const ModuleConfig &config);
    bool update(std::size_t field_index, const std::string &value, std::string &error);
    void discard();
    void mark_saved();
    bool loaded() const noexcept;
    bool dirty() const;
    const Category *category() const noexcept;
    std::string value(std::size_t field_index) const;
    const Config &device_config() const noexcept;
    const ModuleConfig &module_config() const noexcept;
    bool matches(const Config &config) const;
    bool matches(const ModuleConfig &config) const;
private:
    const Category *category_ = nullptr;
    Config device_, original_device_;
    ModuleConfig module_, original_module_;
};
}  // namespace meshtastic::configuration
#endif
