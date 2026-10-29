# Prefer the platform's development package. The pinned BSP has only the
# runtime .so, so ship a compatible public header as a reproducible fallback.
find_package(SQLite3 QUIET)
if(SQLite3_FOUND)
    if(TARGET SQLite3::SQLite3)
        set(MESHTASTIC_SQLITE_TARGET SQLite3::SQLite3)
    else()
        set(MESHTASTIC_SQLITE_TARGET SQLite::SQLite3)
    endif()
else()
    find_library(MESHTASTIC_SQLITE_RUNTIME NAMES sqlite3 libsqlite3.so.0 REQUIRED)
    add_library(MeshtasticSQLite::SQLite3 UNKNOWN IMPORTED)
    set_target_properties(MeshtasticSQLite::SQLite3 PROPERTIES
        IMPORTED_LOCATION "${MESHTASTIC_SQLITE_RUNTIME}"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite3/include")
    set(MESHTASTIC_SQLITE_TARGET MeshtasticSQLite::SQLite3)
endif()
