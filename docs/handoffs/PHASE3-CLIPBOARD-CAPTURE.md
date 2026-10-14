# 任务交接：阶段3 - ClipboardCapture 快照分类

- 状态：采集模块由 main 持有并持续监听；用户已决定不提供暂停控件。Debug 构建通过。MIME 隔离测试通过；真实 Windows 剪贴板需用户验收。
- 负责人对话：主任务
- 日期：2026-10-11
- 依据：[开发与测试规范](../development-spec.md)、[HistoryTaskQueue](PHASE2-HISTORY-TASK-QUEUE.md)、[任务交接模板](TASK-HANDOFF-TEMPLATE.md)

## 目标和完成状态

实现可单测的 QMimeData 快照分类器，以及以 GUI 线程为调用契约的 QClipboard 监听类。采集回调只读取/复制 MIME 数据、校验并向 HistoryTaskQueue 提交；不执行 SQLite、哈希、图片编码或文件 I/O。

应用创建该监听器；服务初始化后开始读取剪贴板并持续监听。历史模型、状态反馈和全局队列均由 main 持有。数据目录迁移仍在队列/数据库创建前完成。

## 改动文件

| 文件 | 用途 |
|---|---|
| src/core/history/clipboard_capture.h/.cpp | GUI 线程监听、静态快照分类、敏感格式检查、队列投递和结果错误转发 |
| tests/history/tst_clipboard_capture.cpp | 合成 MIME 测试：文本/富 MIME、图片优先、文件 URL、HTTP(S)、混合/未知/空数据、敏感标记、超限 |
| CMakeLists.txt | clipboard_capture 独立 QtTest target，并加入应用目标 |
| src/app/main.cpp、src/ui/Main.qml | 创建/拥有队列与采集器，提供采集失败提示 |
| docs/development-spec.md | 更新模块契约和当前状态 |

## MIME 分类和安全策略

- 优先级：先检查敏感标记，再按图片、URL 列表、纯文本顺序分类；一个 MIME 项同时提供 HTML 和纯文本时保留纯文本。
- 图片存为 kind=image；同质本地 URL 列表存为 kind=path；同质 HTTP(S) 列表存为 kind=link；文本中的单个纯 HTTP(S) URL 也归为 link，其余纯文本归为 text。
- 混合本地/远程 URL、非 HTTP(S) 远程 URL、未知格式、空值和无效图片不写库；超过 64 MiB 快照预算或超过 256 个 URL 的内容拒绝。该上限允许标准 4K RGBA 图像。
- Windows 的 ExcludeClipboardContentFromMonitorProcessing 格式出现时拒绝整个条目。CanIncludeInClipboardHistory 仅在格式值恰为 little-endian DWORD 1 时允许；0、缺失/畸形值均 fail-closed。另忽略 KDE passwordManagerHint 标记。
- QClipboard::ownsClipboard() 时跳过应用自身写入；系统监听使用 Clipboard mode，不读取 Linux Selection。
- 正文、路径、图片和 MIME 数据不传给日志；main.cpp 接线时应只记错误码/拒收计数。
- 缺少敏感标记的密码/令牌仍无法可靠识别；用户已选择持续监听，产品需在说明中明确此行为。

## 验证证据

- 构建：`cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8`，通过。
- 测试：8 个 CTest 分别启动均通过，包括 ClipboardCapture、HistoryModel、Repository、队列、服务、图片、设置和日志测试。
- GUI smoke test：应用启动 4 秒并能保持运行，stderr 只有 Qt QML 调试提示，无 QML 运行错误。
- 未运行真实 Windows 剪贴板端到端测试、性能基准或长稳测试。

## 风险及未完成项

- ClipboardCapture 构造函数要求传入 GUI 线程的 QClipboard 和 HistoryTaskQueue；它们的 owner-thread 约束目前靠调用契约/Q_ASSERT，而非运行时跨线程代理。
- onClipboardChanged 会直接读取当前 MIME 内容。系统剪贴板提供者可能在交付数据时阻塞；本模块没有独立进程隔离。平台性能需在 Windows 合成剪贴板应用上测量。
- 超预算、未知格式和敏感格式被拒绝，通过 captureRejected 发出错误枚举；队列过载通过 submissionRejected 发出状态。未来接线需要以隐私数值事件记录这些拒收，且不能记录正文。
- 自动化测试只用合成 MIME；还需实机验证 Windows OLE 剪贴板 provider、图片和文件 URL。
- 数据目录迁移已在启动时、服务创建前执行；如果未来加入运行期迁移，仍需暂停采集、排空队列并关闭 DB/日志句柄。

## 下一步

1. 用户在 Windows 上用无敏感文本、网页链接、多文件、图片验证捕获和再次复制。
2. 验证持续监听、应用自身复制回写不重复入库，以及退出后 SQLite/image 数据可重启读取。
3. 性能记录使用合成 clipboard 内容，单独测回调到 enqueue P50/P95/P99 和 worker commit 延时。
