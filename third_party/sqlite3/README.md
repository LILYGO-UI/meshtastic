# SQLite public C API header

The sqlite3.h file is from SQLite 3.40.1. The pinned CM0 BSP contains the
compatible SQLite 3.46.1 runtime but not its development header. This project
uses public SQLite APIs available in both versions. Source:
https://www.sqlite.org/2022/sqlite-amalgamation-3400100.zip
Archive SHA-256: 49112cc7328392aa4e3e5dae0b2f6736d0153430143d21f69327788ff4efe734

SQLite is in the public domain (https://www.sqlite.org/copyright.html).
This header preserves the upstream public-domain dedication verbatim.

Native builds use the system SQLite development package. Cross builds may use
this header with the SQLite runtime library from their target sysroot when the
SDK does not contain libsqlite3-dev. No host library enters a cross build.
