# 0.2.0 重构验收记录

验收日期：2026-10-28。

## 构建与测试

- `cmake --preset host-simulator` 和对应构建通过。
- `ctest --preset host-simulator --output-on-failure --parallel 3`：23/23 通过。
- 协议测试使用本机假 radio，覆盖 ACK 来源校验、消息重试、设备身份隔离、
  配置事务顺序、并发保存互斥、中断重连和持久化迁移。
- 地图测试覆盖完整 PNG 解码、透明混色、损坏缓存重新下载、离线校验、
  清理缓存与后台任务并发，以及按设备隔离的位置、航点和路由历史。
- 320×568、568×1232、1232×568、1024×768 渲染测试通过，已检查主页面、
  地图、节点详情、设置与频道截图；测试还覆盖旋转、输入、焦点和滚动保持。
- `cmake --preset cm0-cross` 和对应 ARM64 构建通过，构建日志无编译警告。
- CPack 生成 `dist/lilygo-ui-meshtastic_0.2.0_arm64.deb`。已检查 ARM64 ELF、
  动态库依赖、Launcher/桌面元数据、GPL 及第三方许可文件。
- `lpm metadata --format cmake` 和 `git diff --check` 通过。

详细日志保存在 `build/host-simulator/refactor-*.log` 与
`build/cm0-cross/refactor-*.log`；渲染图保存在主机构建目录中。

## 实机连接

最终主机二进制执行：

```sh
LILYGO_MESHTASTIC_DB_PATH=:memory: \
  build/host-simulator/meshtastic-live-smoke 192.168.8.222 4403
```

结果：连接成功，本机节点 `e8351dae`，同步 4 个节点、1 个频道。此检查仅执行
握手和同步；未向无线网络发消息或修改实机配置。配置写入和消息投递由假 radio
协议测试验证，尚未在实机上验收。

## 实现范围

本轮按 Apple 客户端的主要业务参考重构 C++17/LVGL TCP 客户端，包含持久化会话、
发送状态、配置与频道编辑、节点工具及地图。当前不含 BLE、USB 串口、固件 DFU
和 Apple 平台专用集成；不代表 Apple 客户端全部功能已一一实现。
安装包已生成，尚未部署到目标屏幕设备。
