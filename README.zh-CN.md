# LILYGO Meshtastic

[English](README.md) | **简体中文**

lilygo-ui-meshtastic 是面向 LILYGO UI 的 Meshtastic 客户端。应用使用 C++17、
[LILYGO UI AppKit](https://github.com/LILYGO-UI/cm0-appkit) 和 LVGL 9.5 独立实现，
通过 `meshtasticd` 提供的 TCP Client API 连接 Meshtastic 电台。软件包名称为
`lilygo-ui-meshtastic`，桌面集成使用 `cc.lilygo.ui.Meshtastic`。

项目提供 CM0 ARM64 Linux 设备构建，以及用于开发和测试的 SDL2 桌面模拟器。
功能和交互范围参考 Meshtastic Apple 客户端；本仓库没有复制 Apple 客户端源码。

## 预览

<p>
  <img src="assets/previews/preview1.png" alt="Meshtastic 连接面板" width="240">
</p>

## 功能

- **会话与消息：** 浏览频道消息和私聊，搜索会话，显示未读数，分页加载历史，
  并在导航或旋转屏幕时保留草稿。
- **可靠发送状态：** 显示排队、已发送到电台、路由 ACK、拒绝、超时和失败状态。
  写入传输层与收到确认分开处理，重试始终由用户显式触发。
- **节点与地图：** 浏览节点、位置、遥测、航点和路由轨迹。内置 OpenStreetMap
  支持平移、缩放、地址搜索、节点选择、节点跟随和本地栅格瓦片缓存。
- **节点工具：** 请求遥测、发起路由追踪、创建或分享可编辑航点。遥测记录和路由
  轨迹在应用重启后仍然保留。
- **设备设置：** 通过类型化字段编辑设备和模块配置，保留界面未暴露的字段，写入后
  从电台回读校验，并对重启、关机和 NodeDB 重置等破坏性操作要求确认。
- **频道：** 编辑频道槽位和密钥，控制上行/下行设置，并通过标准 Meshtastic
  ChannelSet URL 导入或导出，导入时提供预览。
- **本地持久化：** SQLite 保存消息、节点、频道、配置、位置、遥测、航点和路由回复。
  数据按 TCP endpoint 和电台身份隔离。
- **响应式输入：** 在横屏和竖屏容器中使用同一套布局；支持实体键盘，没有 TCA8418
  键盘时使用 LVGL 屏幕键盘。

下载新地图瓦片和地址搜索需要联网。已缓存的瓦片可以离线使用，网格消息不依赖地图
网络。缓存路径和离线规则见[本地数据与地图](docs/03-storage-and-maps.md)。

## 开发环境搭建

项目标识、版本、软件包元数据、权限、构建预设和部署默认值由
[lpm.toml](lpm.toml) 提供。CMakePresets.json 定义 host-simulator 和 cm0-cross
配置。项目必须作为顶层 CMake 工程使用，平台 SDK 只能通过以下方式引入：

```cmake
find_package(LilyGoUI CONFIG REQUIRED)
```

所有项目命令均在仓库根目录执行。

### 获取源码

```sh
git clone --recurse-submodules https://github.com/LILYGO-UI/meshtastic.git
cd meshtastic
```

已有仓库可使用以下命令初始化锁定版本的 AppKit、Meshtastic schema、Protobuf 和
Abseil 依赖：

```sh
git submodule update --init --recursive
```

预设直接使用 third_party/cm0-appkit 中的源码 SDK，构建模拟器不需要单独安装 AppKit。
SDK 配置会在本应用中构建 AppKit 和 LVGL 静态库，不引入 AppKit 或 LVGL 共享库依赖。

### 主机依赖

需要 CMake 3.21 或更高版本、Python 3.11 或更高版本，以及支持 C++17 的 C/C++ 编译器。
预设使用 Unix Makefiles。AppKit 需要 patch 应用 LVGL 补丁。SDL2 提供主机显示，
FreeType 负责字体渲染，json-c 解析服务数据，SQLite 保存应用数据，libpng 校验地图
瓦片缓存。运行时 HTTP 请求使用 curl 可执行文件，因此不需要 libcurl 开发头文件。

#### Debian 12 / Ubuntu 24.04 或更高版本

```sh
sudo apt update
sudo apt install build-essential cmake git pkg-config patch libsdl2-dev \
  libfreetype6-dev libjson-c-dev libsqlite3-dev libpng-dev curl \
  ca-certificates python3 python3-venv python3-pip
```

较旧发行版需要先将 CMake 和 Python 升级到上述最低版本。

#### macOS

按需安装 Xcode Command Line Tools，然后使用 [Homebrew](https://brew.sh/) 安装依赖：

```sh
xcode-select --install
brew install git cmake pkg-config patch sdl2 freetype json-c sqlite libpng curl python
```

完成 Homebrew 的 Shell 配置，确保工具和库位于 PATH 中。

#### LPM

CMake 配置时会调用 lpm metadata --format cmake。使用 Python 3.11 或更高版本，在项目
虚拟环境中安装 LPM：

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install "git+https://github.com/LILYGO-UI/lpm.git"
lpm --version
```

每次打开新终端后，先重新执行 . .venv/bin/activate。CMake 会同时搜索 .venv/bin
和 PATH。如需指定其他 LPM，可在配置时传入
-DLPM_EXECUTABLE=/absolute/path/to/lpm。可使用以下命令检查由 lpm.toml 生成的元数据：

```sh
lpm metadata --format publish-json
```

构建前检查主机环境：

```sh
cmake --version
python --version
pkg-config --modversion sdl2 freetype2 json-c sqlite3 libpng
lpm metadata --format cmake
```

### 设备交叉编译器

仅构建桌面模拟器时可以跳过本节。设备构建需要在开发机上安装 AArch64 Linux 交叉编译器。

**Debian / Ubuntu：**

```sh
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
```

**Apple Silicon macOS：**

```sh
brew install aarch64-unknown-linux-gnu
```

BSP 工具链要求 PATH 中存在以下名称的可执行文件：

```sh
aarch64-linux-gnu-gcc --version
aarch64-linux-gnu-g++ --version
```

BSP 提供目标平台的头文件和库，但不会安装开发机所需的交叉编译器。交叉构建还需要
sysroot 中的目标平台开发文件，包括 json-c、FreeType 和 DRM；主机库不能替代这些文件。

## 构建与测试

完成环境搭建后，在仓库根目录使用 LPM。它会从 lpm.toml 读取构建预设和软件包输出目录。

### 桌面模拟器

使用默认 Meshtastic endpoint 127.0.0.1:4403 启动模拟器：

```sh
lpm start --foreground
```

如需复用已有构建，执行 lpm start --no-build --foreground。底层可执行文件为：

```sh
./build/host-simulator/lilygo-ui-meshtastic
```

目标板或本地服务需要在 TCP 4403 端口提供 Meshtastic Client API，并连接到电台。可使用
应用参数覆盖 endpoint：

```sh
./build/host-simulator/lilygo-ui-meshtastic \
  --host 192.168.8.222 --port 4403
```

同时支持 --host=value 和 --port=value。通过分隔符传给 LPM 的参数会转发给应用：

```sh
lpm start --foreground -- --host 192.168.8.222 --port 4403
```

服务不可用时，应用会报告配置同步超时并自动重连。应用没有 presentation-mode 参数，
布局依据实际 LVGL 容器尺寸响应，并在尺寸变化后重新应用。

没有实体 TCA8418 键盘时，点击消息输入框或节点名称字段会打开屏幕键盘。确认会发送消息
或保存编辑，取消会保留聊天草稿或恢复原节点名称。实体键盘发现、热插拔、焦点导航和
屏幕键盘抑制由 AppKit 负责。

### 测试

配置并构建主机模拟器，然后运行完整测试套件：

```sh
lpm test
```

| 测试组 | 覆盖内容 |
| --- | --- |
| meshtastic-maps | 地图投影、瓦片调度、PNG 校验、缓存行为、节点覆盖层和离线运行 |
| meshtastic-configuration | 设备/模块类型化编辑、Protobuf 字段保留、事务、回读校验和破坏性操作确认 |
| meshtastic-channels | 频道槽位、密钥、上行/下行设置和 ChannelSet URL 导入导出 |
| meshtastic-workflows | 可靠发送状态、ACK/NAK 匹配、重试、重连恢复和页面流程 |
| meshtastic-persistence、meshtastic-client-history | SQLite 迁移、endpoint/电台隔离、消息历史、遥测、位置和中断发送恢复 |
| meshtastic-protobufs、meshtastic-core | 官方字段编码、边界受限分帧、生成 Protobuf 转换和回环 TCP 握手 |
| meshtastic-view-model、meshtastic-qr | 路由/页面状态转换、用户命令和二维码负载生成 |
| meshtastic-render-* | 在 320 x 568、568 x 1232、1232 x 568 和 1024 x 768 下进行页面、设置、频道、地图和搜索的无头渲染 |

渲染测试会在 build/host-simulator 下写入 PPM 图片。修改 UI 后应检查这些图片；断言
通过不能验证视觉层级、文本裁切、键盘行为或旋转状态。验收要求见
[UI 开发规范](docs/01-user-interface.md)。

live smoke 测试默认不加入测试套件，可连接默认 endpoint 手动运行：

```sh
LILYGO_MESHTASTIC_DB_PATH=:memory: \
  build/host-simulator/meshtastic-live-smoke 127.0.0.1 4403
```

### CM0 设备构建

cm0-cross 预设面向 AArch64 架构的 CM0 Raspberry Pi OS，使用 Release 构建，禁用主机
测试，并将安装前缀设置为 /usr。构建并生成软件包：

```sh
lpm pack
```

首次执行 lpm pack（或 cmake --preset cm0-cross）时，工具链会下载 CM0 BSP 0.1.0，
校验锁定的 SHA-256，并将 sysroot 解压到 .cache/cm0-bsp/0.1.0/sysroot。首次下载需要
访问 GitHub，后续配置会复用缓存。如需修改缓存位置，可传入
-DCM0_BSP_CACHE_DIR=/absolute/path/to/cache。无需在相邻目录克隆 CM0BspBuilder。

CPack 将 ARM64 .deb 写入 dist/。软件包安装可执行文件、Launcher 清单、图标、桌面条目、
AppStream 元数据、第三方许可和 AppKit 字体许可。运行时依赖包含 lilygo-ui-appkit-dev >= 0.1.0，
最低版本由 lpm.toml 的 min_appkit_version 声明。

使用 lpm.toml 中的 SSH 目标部署最新软件包：

```sh
lpm deploy
```

<details>
<summary>底层 CMake 命令</summary>

LPM 封装了以下命令。配置阶段仍需要 LPM 读取项目元数据：

主机构建与测试：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator --output-on-failure
```

设备构建与打包：

```sh
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

</details>

### GPU 渲染构建

设备渲染器由源码 AppKit SDK 在构建时选择。需要使用支持 LILYGO_UI_RENDERER 的新版
SDK，以及包含 libgbm-dev、libegl-dev 和 libgles-dev 的目标匹配 CM0 sysroot。原始
锁定 AppKit 子模块和 BSP 0.1.0 不支持该 GPU 配置。

OpenGL ES 构建应显式选择新版 AppKit，并使用独立构建目录：

```sh
cmake --preset cm0-cross -B build/cm0-opengles \
  -DLilyGoUI_DIR=/path/to/appkit \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/cm0-sysroot/usr/share/cm0-bsp/toolchain.cmake \
  -DLILYGO_UI_RENDERER=opengles
cmake --build build/cm0-opengles --parallel
cpack --config build/cm0-opengles/CPackConfig.cmake -B dist/opengles
```

主机测试使用软件渲染。切换发布渲染器前，应在目标设备上比较渲染正确性、帧时间、内存
占用，以及应用反复退出和返回的行为。

## Meshtastic TCP 与协议

传输层遵循官方 [Meshtastic Client API](https://meshtastic.org/docs/development/device/client-api/)
和锁定版本的 [meshtastic/protobufs](https://github.com/meshtastic/protobufs) schema：

- 默认 endpoint 为 127.0.0.1:4403
- 连接时发送 32 个 0xc3 唤醒/重同步字节
- 帧以 0x94 0xc3 开头，后跟两个字节的大端长度
- ToRadio/FromRadio Protobuf 负载最大为 512 字节
- 使用 ToRadio.want_config_id 和匹配的 FromRadio.config_complete_id 完成握手
- 空闲链路周期发送 ToRadio.heartbeat
- 传输中断后自动重连，并支持正常关闭 TCP

协议层使用 Protobuf 36.0 生成的 C++ 类，同步本机节点信息、NodeDB 条目、位置、设备
指标、频道、设备配置、模块配置、元数据和收到的报文。客户端还提供通用
ToRadio/FromRadio 访问、任意 Data 报文、ADMIN_APP 请求、会话密钥管理、XModem
和 MQTT 代理消息以及 LoRa 区域更新。原始 FromRadio envelope 仍可通过事件队列获取。

生成源码已提交，正常构建不需要 protoc。主动更新 schema 子模块后，如需重新生成源码，
请提供 Protobuf 36.0 编译器：

```sh
MESHTASTIC_PROTOC=/path/to/protoc scripts/regenerate-protobufs.sh
```

## 故障排查

| 现象 | 处理方法 |
| --- | --- |
| lpm is required to read project metadata | 在 .venv 中安装 LPM，将其加入 PATH，或设置 LPM_EXECUTABLE。 |
| 找不到 LilyGoUI 或 LVGL | 执行 git submodule update --init --recursive；预设期望 third_party/cm0-appkit 存在。 |
| 配置同步阶段超时 | 检查 meshtasticd 或其他 Client API 服务是否监听所选主机和 TCP 端口，默认是 127.0.0.1:4403。 |
| 缺少 SDL2、FreeType、json-c、SQLite 或 libpng | 安装主机开发包；交叉构建时检查目标 sysroot。 |
| 找不到 Homebrew 库 | 使用 cmake --preset host-simulator -DCMAKE_PREFIX_PATH="$(brew --prefix)" 配置。 |
| Protobuf 重新生成失败 | 使用 Protobuf 36.0 的 protoc，并在执行脚本前初始化锁定的 schema 子模块。 |
| BSP 下载或交叉配置失败 | 检查 GitHub 访问、AArch64 编译器名称、sysroot 磁盘空间和 CM0_BSP_CACHE_DIR。 |
| 地图瓦片或地址搜索无法加载 | 检查 curl、CA 证书、网络和 LILYGO_MESHTASTIC_MAP_DISABLE_NETWORK；已缓存瓦片仍可离线使用。 |
| GPU 渲染配置失败 | 使用支持该功能的新版源码 AppKit SDK，并确认 sysroot 提供 GBM、EGL 和 GLES 开发文件。 |

## 文档

| 文档 | 内容 |
| --- | --- |
| [工程概览](docs/00-overview.md) | 仓库约束、元数据、源码边界、SDK 准备和标准工作流 |
| [UI 开发规范](docs/01-user-interface.md) | View/ViewModel 边界、字体、响应式布局、对象所有权和 UI 验收 |
| [客户端架构](docs/02-client-architecture.md) | 传输、协议、会话、持久化、配置、地图服务和消息流程 |
| [本地数据与地图](docs/03-storage-and-maps.md) | SQLite 路径、会话隔离、地图缓存、离线模式和交叉构建依赖 |
| [重构验收记录](docs/04-refactor-validation.md) | 构建、测试、渲染、打包和实机连接验收记录 |
| [UI 设计规范兼容入口](docs/ui-design-guidelines.md) | 指向当前 UI 开发规范 |

## 致谢与许可

本应用使用 [GPL-3.0-only](LICENSE) 授权。Meshtastic schema、Protobuf、Abseil、SQLite、
AppKit 和 LVGL 保留各自的许可证和声明。地图投影、瓦片缓存、HTTP 客户端以及部分异步
瓦片展示代码改编自 [LILYGO UI Maps](https://github.com/LILYGO-UI/lilygo-ui-maps)，
相关 MIT 声明见 [src/maps/NOTICE.md](src/maps/NOTICE.md)。

地图瓦片由 [OpenStreetMap.de](https://www.openstreetmap.de/) 提供，地址搜索使用
[Nominatim](https://nominatim.org/)，界面保留 OpenStreetMap 要求的署名。运行或分发应用时，
请遵守相关服务的使用政策。
