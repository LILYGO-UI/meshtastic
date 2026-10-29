# Meshtastic 客户端架构

本轮实现以 Meshtastic Apple 的主要客户端业务和交互为参考，面向 LILYGO UI
独立实现。运行栈是 C++17、LVGL、AppKit、Linux TCP 与 SQLite。Apple 专用的
SwiftUI、SwiftData、MapKit、Watch、Widget、iCloud 和系统通知集成不构成本应用
的依赖。当前连接方式是 TCP Client API；蓝牙、USB 串口和固件 DFU 尚无应用入口。

## 服务边界

- transport/TcpTransport 负责连接、可取消读写和关闭。
- domain/meshtastic_protocol 负责边界受限的分帧和官方生成 Protobuf 消息解析。
- services/RadioSession 负责发送状态、ACK/NAK、超时和显式重试；不会自动重发。
- persistence/Repository 负责 SQLite 迁移、按 endpoint/radio 隔离的存储与历史查询。
- domain/MeshtasticClient 装配上述服务、握手/重连并发布不可变快照。
- configuration 为设备与模块设置提供类型化字段、验证和保留原始消息的编辑草稿。
- maps 包含应用内的投影、缓存、HTTP 和异步瓦片/搜索服务。
- 页面依赖方向是 View → ViewModel → Client/Domain；LVGL 操作限于 UI 线程。
  页面切换、按钮触发的动态列表重建均延迟到当前事件返回后执行。

## 功能流程

消息以数据库本地 ID 分页；radio packet ID 用于关联发送与 routing request_id。
内存快照只保留窗口，历史不会随窗口淘汰。会话摘要独立查询，最近消息窗口之外的
会话也会显示。只有激活中的会话标为已读。输入草稿按会话保存，横竖屏不重建页面。

发送依次经过 queued、sent、acknowledged，或 rejected/timed_out/failed。
“sent”只表示成功写入 radio transport。重连、退出或重启时未确认发送不会被自动
重放；用户需要显式重试，同一消息沿用 packet ID 并累加尝试次数。
routing 回复还须匹配发送记录的来源、目标和频道；已确认的终态不会被迟到的相反
结果覆盖。

Config、ModuleConfig 各类别分别保存，避免 protobuf oneof 互相覆盖。
设置草稿从完整消息复制，仅修改已暴露字段。一次保存按顺序等待 begin、各项写入
和 commit 的 ACK，再按请求 ID 匹配回读结果并与期望值比较。频道采用相同回读原则。
同一客户端的设置和频道共用事务占用状态，防止两个页面交叉写入。离开页面后保存
继续处理；写入中断或放弃时，先断开并重新同步设备，再允许下一次保存。管理员
会话密钥仅在当前连接内使用；与频道 PSK 一样，不输出到日志。

位置、遥测、航点与路由回复保存为类型化历史事件。节点详情显示最新历史，地图
把位置轨迹和路由中间节点投影到底图；过期航点会被剔除。节点详情可构造航点并
选择分享频道、位置、描述和有效期。

## 构建与验收

AppKit 只经 find_package(LilyGoUI CONFIG REQUIRED) 使用。本应用保留顶层
configure/build/test/install/package 能力；不会引用相邻 Maps 应用的构建目标。
地图迁入来源、版本和 MIT 许可位于 src/maps/NOTICE.md，底图署名保留在界面中。

验收包含协议、假 radio TCP 会话、持久化迁移/恢复/隔离、可靠消息状态、分类配置、
频道链接、地图异步请求与缓存，以及 320×568、568×1232、1232×568、1024×768
下的页面渲染、横竖屏输入/焦点/滚动保持。UI 自动测试使用独立数据和离线瓦片，
不访问用户数据库或在线地图服务。
