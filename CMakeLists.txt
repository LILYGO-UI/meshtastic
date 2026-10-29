cmake_minimum_required(VERSION 3.21)

include(cmake/LpmMetadata.cmake)
project(${LILYGO_UI_CMAKE_PROJECT_NAME} LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
find_package(LilyGoUI CONFIG REQUIRED)
find_package(Threads REQUIRED)
include(cmake/PNGDependency.cmake)
include(cmake/SQLiteDependency.cmake)
find_path(MESHTASTIC_JSON_INCLUDE_DIR NAMES json-c/json.h REQUIRED)
find_library(MESHTASTIC_JSON_LIBRARY NAMES json-c REQUIRED)
add_library(MeshtasticJSON::MeshtasticJSON UNKNOWN IMPORTED)
set_target_properties(MeshtasticJSON::MeshtasticJSON PROPERTIES
    IMPORTED_LOCATION "${MESHTASTIC_JSON_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MESHTASTIC_JSON_INCLUDE_DIR}")

set(ABSL_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/abseil-cpp EXCLUDE_FROM_ALL)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_CONFORMANCE OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_PROTOC_BINARIES OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBPROTOC OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_LIBUPB OFF CACHE BOOL "" FORCE)
set(protobuf_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_LOCAL_DEPENDENCIES_ONLY ON CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_HAVE_BUILTIN_ATOMICS ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/protobuf EXCLUDE_FROM_ALL)

configure_file(
    cmake/app_identity.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app_identity.h
    @ONLY)

set(MESHTASTIC_PROTOBUF_SOURCES
    src/generated/meshtastic/admin.pb.cc
    src/generated/meshtastic/apponly.pb.cc
    src/generated/meshtastic/atak.pb.cc
    src/generated/meshtastic/channel.pb.cc
    src/generated/meshtastic/config.pb.cc
    src/generated/meshtastic/connection_status.pb.cc
    src/generated/meshtastic/device_ui.pb.cc
    src/generated/meshtastic/mesh.pb.cc
    src/generated/meshtastic/module_config.pb.cc
    src/generated/meshtastic/portnums.pb.cc
    src/generated/meshtastic/telemetry.pb.cc
    src/generated/meshtastic/xmodem.pb.cc)
add_library(meshtastic_protobufs STATIC ${MESHTASTIC_PROTOBUF_SOURCES})
target_include_directories(meshtastic_protobufs SYSTEM PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/generated)
target_compile_features(meshtastic_protobufs PUBLIC cxx_std_17)
target_link_libraries(meshtastic_protobufs PUBLIC
    protobuf::libprotobuf-lite
    utf8_validity)

add_library(lilygo_ui_meshtastic_core STATIC
    src/domain/meshtastic_protocol.cpp
    src/domain/meshtastic_client.cpp
    src/transport/tcp_transport.cpp
    src/services/radio_session.cpp
    src/persistence/meshtastic_repository.cpp
    src/configuration/admin_transaction.cpp
    src/configuration/configuration_catalog.cpp
    src/maps/domain/map_model.cpp
    src/maps/domain/tile_cache.cpp
    src/maps/services/http_client.cpp
    src/maps/services/tile_service.cpp)
target_include_directories(lilygo_ui_meshtastic_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_meshtastic_core PRIVATE
    -Wall -Wextra -Wpedantic)
target_compile_features(lilygo_ui_meshtastic_core PUBLIC cxx_std_17)
target_link_libraries(lilygo_ui_meshtastic_core PUBLIC
    Threads::Threads
    PNG::PNG
    ${MESHTASTIC_SQLITE_TARGET}
    MeshtasticJSON::MeshtasticJSON
    meshtastic_protobufs)

add_library(lilygo_ui_meshtastic_ui STATIC
    src/app.cpp
    src/app_router.cpp
    src/components/components.cpp
    src/components/qr_code.cpp
    src/components/search_input.cpp
    src/pages/channels/channels_view.cpp
    src/pages/channels/channels_view_model.cpp
    src/pages/chat/chat_view.cpp
    src/pages/chat/chat_view_model.cpp
    src/pages/connection/connection_view.cpp
    src/pages/connection/connection_view_model.cpp
    src/pages/device_tools/device_tools_view.cpp
    src/pages/device_tools/device_tools_view_model.cpp
    src/pages/map/map_view.cpp
    src/pages/map/map_view_model.cpp
    src/pages/messages/messages_view.cpp
    src/pages/messages/messages_view_model.cpp
    src/pages/node_detail/node_detail_view.cpp
    src/pages/node_detail/node_detail_view_model.cpp
    src/pages/nodes/nodes_view.cpp
    src/pages/nodes/nodes_view_model.cpp)
target_include_directories(lilygo_ui_meshtastic_ui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_options(lilygo_ui_meshtastic_ui PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_meshtastic_ui PUBLIC
    lilygo_ui_meshtastic_core
    LilyGoUI::Runtime)

add_executable(lilygo_ui_meshtastic src/main.cpp)
target_compile_options(lilygo_ui_meshtastic PRIVATE
    -Wall -Wextra -Wpedantic)
target_link_libraries(lilygo_ui_meshtastic PRIVATE lilygo_ui_meshtastic_ui)
set_target_properties(lilygo_ui_meshtastic PROPERTIES
    OUTPUT_NAME "${LILYGO_UI_EXECUTABLE_NAME}")

configure_file(data/launcher.json.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json @ONLY)
configure_file(data/application.desktop.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop @ONLY)
configure_file(data/application.metainfo.xml.in
    ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml @ONLY)

install(TARGETS lilygo_ui_meshtastic
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.json
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/launcher/apps
    COMPONENT app)
lilygo_ui_install_launcher_icon(
    "${LILYGO_UI_APP_ID}"
    "${CMAKE_CURRENT_SOURCE_DIR}/${LILYGO_UI_APP_ICON}")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    COMPONENT app)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LILYGO_UI_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo
    COMPONENT app)
lilygo_ui_install_font_licenses(
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}"
    app)
configure_file(third_party/meshtastic-protobufs/LICENSE
    ${CMAKE_CURRENT_BINARY_DIR}/LICENSE.meshtastic-protobufs COPYONLY)
configure_file(third_party/protobuf/LICENSE
    ${CMAKE_CURRENT_BINARY_DIR}/LICENSE.protobuf COPYONLY)
configure_file(third_party/abseil-cpp/LICENSE
    ${CMAKE_CURRENT_BINARY_DIR}/LICENSE.abseil COPYONLY)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/maps/NOTICE.md
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3/README.md
    ${CMAKE_CURRENT_BINARY_DIR}/LICENSE.meshtastic-protobufs
    ${CMAKE_CURRENT_BINARY_DIR}/LICENSE.protobuf
    ${CMAKE_CURRENT_BINARY_DIR}/LICENSE.abseil
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/${LILYGO_UI_PACKAGE_NAME}
    COMPONENT app)

if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    foreach(suite IN ITEMS maps configuration channels workflows persistence client_history)
        string(REPLACE "_" "-" suite_slug "${suite}")
        add_executable(meshtastic-${suite_slug}-tests tests/test_${suite}.cpp)
        target_compile_options(meshtastic-${suite_slug}-tests PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
        if(suite STREQUAL "persistence" OR suite STREQUAL "client_history")
            target_link_libraries(meshtastic-${suite_slug}-tests PRIVATE lilygo_ui_meshtastic_core)
        else()
            target_link_libraries(meshtastic-${suite_slug}-tests PRIVATE lilygo_ui_meshtastic_ui)
        endif()
        add_test(NAME meshtastic-${suite_slug} COMMAND meshtastic-${suite_slug}-tests)
    endforeach()
    add_executable(meshtastic-protobuf-tests tests/test_meshtastic_protobufs.cpp)
    target_compile_options(meshtastic-protobuf-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-protobuf-tests PRIVATE
        meshtastic_protobufs)
    add_test(NAME meshtastic-protobufs COMMAND meshtastic-protobuf-tests)

    add_executable(meshtastic-core-tests tests/test_core.cpp)
    target_compile_options(meshtastic-core-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-core-tests PRIVATE
        lilygo_ui_meshtastic_core)
    add_test(NAME meshtastic-core COMMAND meshtastic-core-tests)

    add_executable(meshtastic-view-model-tests tests/test_view_model.cpp)
    target_compile_options(meshtastic-view-model-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-view-model-tests PRIVATE
        lilygo_ui_meshtastic_ui)
    add_test(NAME meshtastic-view-model COMMAND meshtastic-view-model-tests)

    add_executable(meshtastic-qr-tests tests/test_qr_code.cpp)
    target_compile_options(meshtastic-qr-tests PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-qr-tests PRIVATE
        lilygo_ui_meshtastic_ui)
    add_test(NAME meshtastic-qr COMMAND meshtastic-qr-tests)

    add_executable(meshtastic-live-smoke tests/live_smoke.cpp)
    target_compile_options(meshtastic-live-smoke PRIVATE
        -Wall -Wextra -Wpedantic)
    target_link_libraries(meshtastic-live-smoke PRIVATE
        lilygo_ui_meshtastic_core)

    add_executable(meshtastic-render-settings-channels tests/render_settings_channels.cpp)
    target_compile_options(meshtastic-render-settings-channels PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-render-settings-channels PRIVATE lilygo_ui_meshtastic_ui)
    add_executable(meshtastic-render-search tests/render_search.cpp)
    target_compile_options(meshtastic-render-search PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-render-search PRIVATE lilygo_ui_meshtastic_ui)
    add_test(NAME meshtastic-render-search COMMAND meshtastic-render-search)
    add_executable(meshtastic-render-map tests/render_map.cpp)
    target_compile_options(meshtastic-render-map PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-render-map PRIVATE lilygo_ui_meshtastic_ui)
    target_compile_definitions(meshtastic-render-map PRIVATE
        MESHTASTIC_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(meshtastic-render-ui tests/render_ui.cpp)
    target_compile_options(meshtastic-render-ui PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_link_libraries(meshtastic-render-ui PRIVATE
        lilygo_ui_meshtastic_ui)
    foreach(viewport IN ITEMS compact-portrait portrait landscape large-landscape)
        if(viewport STREQUAL "compact-portrait")
            set(_width 320)
            set(_height 568)
        elseif(viewport STREQUAL "portrait")
            set(_width 568)
            set(_height 1232)
        elseif(viewport STREQUAL "landscape")
            set(_width 1232)
            set(_height 568)
        else()
            set(_width 1024)
            set(_height 768)
        endif()
        add_test(NAME meshtastic-render-${viewport} COMMAND meshtastic-render-ui
            ${_width} ${_height}
            ${CMAKE_CURRENT_BINARY_DIR}/meshtastic-${viewport}.ppm)
        add_test(NAME meshtastic-settings-render-${viewport} COMMAND meshtastic-render-settings-channels
            ${_width} ${_height} ${CMAKE_CURRENT_BINARY_DIR}/meshtastic-settings-${viewport})
        add_test(NAME meshtastic-map-render-${viewport} COMMAND meshtastic-render-map
            ${_width} ${_height} ${CMAKE_CURRENT_BINARY_DIR}/meshtastic-map-${viewport}.ppm)
        set_tests_properties(meshtastic-map-render-${viewport} PROPERTIES
            ENVIRONMENT "LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK=1;LILYGO_MESHTASTIC_DB_PATH=:memory:")
        set_tests_properties(meshtastic-render-${viewport} PROPERTIES
            ENVIRONMENT "LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK=1;LILYGO_MESHTASTIC_DB_PATH=:memory:")
    endforeach()
    set_tests_properties(meshtastic-view-model PROPERTIES
        ENVIRONMENT "LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK=1;LILYGO_MESHTASTIC_DB_PATH=:memory:")
    set_tests_properties(meshtastic-maps meshtastic-workflows meshtastic-persistence meshtastic-client-history
        meshtastic-core meshtastic-render-compact-portrait meshtastic-render-portrait
        meshtastic-render-landscape meshtastic-render-large-landscape PROPERTIES
        RESOURCE_LOCK meshtastic_loopback)
    set_tests_properties(meshtastic-maps PROPERTIES
        ENVIRONMENT "LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK=1;LILYGO_MESHTASTIC_DB_PATH=:memory:")
endif()

# New applications have no legacy CM0 package to replace. Add Debian migration
# fields only when this project supersedes an actually published package.
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_PACKAGE_VERSION "${LILYGO_UI_VERSION}")
if(LILYGO_UI_VERSION MATCHES
   "^([0-9]+:)?([0-9]+)\\.([0-9]+)\\.([0-9]+)")
    set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
    set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
    set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
else()
    set(CPACK_PACKAGE_VERSION_MAJOR 0)
    set(CPACK_PACKAGE_VERSION_MINOR 0)
    set(CPACK_PACKAGE_VERSION_PATCH 0)
endif()
set(CPACK_PACKAGE_CONTACT "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_VENDOR "${LILYGO_UI_PACKAGE_CONTACT}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${LILYGO_UI_APP_SUMMARY}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${LILYGO_UI_APP_HOMEPAGE}")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_SET_DESTDIR ON)
set(CPACK_COMPONENTS_ALL app)
set(CPACK_DEBIAN_APP_PACKAGE_NAME "${LILYGO_UI_PACKAGE_NAME}")
set(CPACK_DEBIAN_APP_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_APP_PACKAGE_DEPENDS
    "libc6, libstdc++6, libdrm2, libfreetype6, libpng16-16t64, zlib1g, libbz2-1.0, libbrotli1, libsqlite3-0, libjson-c5, curl, ca-certificates, lilygo-ui-appkit-dev (>= ${LILYGO_UI_MIN_APPKIT_VERSION})")
if(LILYGO_UI_GRAPHICS_DEBIAN_DEPENDS)
    string(APPEND CPACK_DEBIAN_APP_PACKAGE_DEPENDS
        ", ${LILYGO_UI_GRAPHICS_DEBIAN_DEPENDS}")
endif()
set(CPACK_DEBIAN_APP_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        "${LILYGO_UI_PACKAGE_ARCHITECTURE}")
endif()
include(CPack)
