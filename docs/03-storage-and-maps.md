# 本地数据与地图

## 数据库

SQLite 数据库按以下优先级确定位置：

1. LILYGO_MESHTASTIC_DB_PATH：完整文件路径；:memory: 可用于临时测试。
2. LILYGO_MESHTASTIC_DATA_DIR/meshtastic.sqlite3。
3. XDG_DATA_HOME/lilygo-ui-meshtastic/meshtastic.sqlite3。
4. HOME/.local/share/lilygo-ui-meshtastic/meshtastic.sqlite3。
5. 系统临时目录下 lilygo-ui-meshtastic.sqlite3。

会话按 TCP endpoint 和 radio node number 隔离。启动时可以查看上次缓存数据；
新连接确认 radio 身份后切换到相应会话。不同设备不会合并聊天或重放待发操作。
数据库使用 user_version 迁移，保留旧会话；完整保存的配置和事件消息保留未知
Protobuf 字段，节点记录保存应用所用字段。打开持久化失败会
在状态页显示错误，并使用临时内存存储继续运行；此时新数据无法跨重启保留。
当前数据库版本为 4。接收去重按本机观察时间限制在 60 秒内，并同时比较发送者、
packet ID、接收者、频道和内容，允许设备日后复用 packet ID。发件重试沿用原记录。

数据库包含消息与频道密钥；备份时应退出应用后复制数据库文件。
数据库采用 DELETE journal，持久化文件权限设为仅当前用户可读写。
本地缓存清理只清理底图，不删除聊天、设置和节点历史。

## 地图

地图核心来自 LILYGO UI Maps 的 Web Mercator/PNG 瓦片实现，在本仓库独立构建。
新瓦片使用 OpenStreetMap DE，地址搜索使用 Nominatim；界面保留 OSM 署名。
下载通过受限的 curl 子进程完成，依赖目标系统的 curl 与 CA certificates。

LILYGO_MESHTASTIC_MAP_CACHE_DIR 指定独立缓存目录；默认目录位于本应用的
XDG cache/home cache 路径中。瓦片按 zoom/x/y.png 保存并校验后原子提交。
磁盘缓存和下载内容在后台经 libpng 完整解码校验，损坏瓦片视为缓存未命中。
拖拽或缩放后的旧请求不改变新视口，清理缓存后旧下载也不会写回。

LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK=1 禁止在线瓦片和地址搜索，已缓存的
瓦片仍可显示。没有缓存的区域显示待下载网格，节点、航点、轨迹仍可操作。
离线能力是缓存复用；不包含离线地理编码或离线区域预下载。

定位优先使用连接 radio 的位置；没有本机位置时可适配全部已知节点。
节点详情的 Show on Map 会选中并跟随该节点；手动拖动会暂停跟随。
地图不会另行占用 Maps 应用的 GNSS UART；本机位置来自 Meshtastic radio。

## 构建依赖

主机需 SQLite、libpng 和 json-c 开发包。Debian 可安装 libsqlite3-dev、libpng-dev、
libjson-c-dev，macOS Homebrew 可安装 sqlite、libpng、json-c。目标设备需 libsqlite3-0、libjson-c5、curl、
ca-certificates，已纳入生成的 Debian 包依赖。

固定 BSP 只含 SQLite 3.46.1 运行库，缺少开发头时，CMake 使用仓库内兼容的
SQLite 3.40.1 公开 API 头文件，并始终链接目标 sysroot 的运行库。来源和摘要记录在
third_party/sqlite3/README.md。交叉构建不会链接主机的 SQLite。
固定 BSP 已提供 libpng 头和动态库，但缺少 FindPNG 所要求的 zlib 开发头；
交叉构建直接导入 sysroot 的 libpng 动态库，使用不需要 zlib 头的简化解码 API。
