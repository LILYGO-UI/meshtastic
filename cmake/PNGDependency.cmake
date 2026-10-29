# The pinned BSP provides libpng headers and its shared library, but omits
# zlib development headers required by CMake's FindPNG. The simplified libpng
# read API needs no zlib headers; libpng's shared library carries that runtime
# dependency. Search only the target roots selected by the cross toolchain.
if(CMAKE_CROSSCOMPILING)
    find_path(MESHTASTIC_PNG_INCLUDE_DIR NAMES png.h PATH_SUFFIXES libpng16 REQUIRED)
    find_library(MESHTASTIC_PNG_LIBRARY NAMES png16 png libpng16.so.16 REQUIRED)
    add_library(PNG::PNG UNKNOWN IMPORTED)
    set_target_properties(PNG::PNG PROPERTIES
        IMPORTED_LOCATION "${MESHTASTIC_PNG_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MESHTASTIC_PNG_INCLUDE_DIR}")
else()
    find_package(PNG REQUIRED)
endif()
