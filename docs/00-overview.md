# LILYGO UI Meshtastic 工程规范

本文记录本应用从 LILYGO UI 模板继承的工程约束。文中的“必须”和
“不得”是发布前需要满足的要求。

## 稳定约束

- 本仓库必须作为顶层 CMake 3.21+ 工程独立配置、构建、测试、安装和打包。
- 应用源码和测试使用 C++17 或更新标准；C ABI 仅保留在 AppKit 描述符边界。
- 平台 SDK 只通过 `find_package(LilyGoUI CONFIG REQUIRED)` 使用；由 SDK 配置
  构建静态库。应用不得枚举 AppKit 私有源文件，或依赖 Launcher 和其他应用源码。
- UI 使用 SDK 提供的 LVGL 9.5。主机预览使用 SDL2，设备使用 DRM/KMS。
- 稳定 preset 名为 `host-simulator` 和 `cm0-cross`，构建目录位于
  `build/<preset>/`，不得执行源码内构建。
- 标准验收视口为 `568x1232` 和 `1232x568`；额外验证 `320x568` 和
  `1024x768`。

## 身份和元数据

`lpm.toml` 是唯一由开发者维护的身份和产品元数据源，包含版本、描述、许可、
作者、资源、权限、兼容性、Launcher 排序、构建 preset 和部署默认值。CMake
通过 `cmake/LpmMetadata.cmake` 调用 LPM，并生成身份头文件、Launcher manifest、
desktop 文件和 AppStream 元数据。

公开身份固定为：

```text
包名和可执行文件  lilygo-ui-meshtastic
应用 ID            cc.lilygo.ui.Meshtastic
Desktop/AppStream  cc.lilygo.ui.Meshtastic
```

`data/*.in` 只能包含通用且已正确转义的占位符。`publish.json` 是临时生成物，
不得提交或作为项目配置输入。

## 源码边界

- `src/domain/` 保存跨页面共享的 Meshtastic 协议、连接状态和领域对象，不依赖 LVGL。
- `src/pages/<page>/` 保存该页面的 ViewModel 和 View；页面私有状态也放在此处。
- `src/components/` 只保存跨页面复用的组件、语义 token 和 Subject 生命周期封装。
- `src/app.cpp` 只负责应用生命周期、快照分发和页面装配。
- `src/app_router.*` 管理全局导航，不由任何页面 View 持有。

依赖和数据流必须保持为：

```text
用户输入 -> View 事件 -> ViewModel 命令 -> Domain 更新
         -> Subject/展示状态 -> View 绑定 -> LVGL 渲染
```

## SDK 准备

开发前先初始化仓库锁定的 AppKit submodule：

```sh
git submodule update --init --recursive
```

默认 preset 将 `LilyGoUI_DIR` 指向 `third_party/cm0-appkit`。应用通过
`find_package(LilyGoUI CONFIG REQUIRED)` 加载源码 SDK 配置，由 SDK 构建
AppKit/LVGL 静态库并链接进应用。应用 CMake 不得枚举 SDK 私有源文件或引用
其私有头文件。AppKit 代码更新后需要更新子模块版本并重新构建应用。

`lilygo-ui-appkit-dev` 0.1.0 或更新版本同时提供源码 SDK、公共字体和许可证，
应用不重复打包字体。`lpm.toml` 的 `min_appkit_version` 指定该软件包的最低版本。
AppKit/LVGL 仍静态链接进每个应用，该软件包不包含 AppKit/LVGL 动态库。
交叉编译 sysroot 只需 BSP 和系统开发依赖，不需要预装 AppKit SDK。
主机预览使用源码 SDK 中的字体资源。

## 工作流

LPM 是首选入口，CMake presets 是可独立调用的底层接口：

```sh
lpm start
lpm test
lpm pack
lpm deploy
```

底层验收命令为：

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator --output-on-failure
cmake --preset cm0-cross
cmake --build --preset cm0-cross --parallel
cpack --config build/cm0-cross/CPackConfig.cmake -B dist
```

## 提交和验收边界

`.cache/`、`.lpm/`、`.venv/`、`build/`、`dist/`、`publish.json` 和本地 LVGL
Editor 导出目录不属于源码。发布前必须确认所有应用源码、测试、资源和 Git
submodule gitlink 已纳入版本控制。

每次提交至少执行主机配置、构建和测试。UI 修改还必须检查所有支持视口、运行时
尺寸切换、长文本和中西文 fallback；元数据、安装、SDK 或打包修改还必须执行
ARM64 交叉构建和 Debian 打包验证。

