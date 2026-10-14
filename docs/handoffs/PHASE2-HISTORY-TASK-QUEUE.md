# 任务交接：阶段2 - HistoryTaskQueue

- 状态：已实现并独立测试；尚未由 main.cpp 创建。应用接线待数据库/日志目录切换协调完成后进行。队列本身和采集单测当前均通过（完整 CTest 7/7）。
- 负责人对话：主任务
- 日期：2026-10-10
- 依据：[开发与测试规范](../development-spec.md)、[HistoryService](PHASE2-HISTORY-SERVICE.md)

## 目标与边界

把同步的 HistoryService 固定放进一个专用 QThread，通过 queued 请求串行写入。剪贴板采集和 UI 只能提交快照，不直接持有 Repository 或做同步持久化。本次只实现任务队列和 worker，不启动真实剪贴板监听。

## 实现

| 文件 | 用途 |
|---|---|
| src/core/history/history_worker.h/.cpp | 在 worker 线程创建、初始化和销毁 HistoryService；处理记录和关闭槽 |
| src/core/history/history_task_queue.h/.cpp | GUI/调用线程的提交接口、就绪/停止状态、queued FIFO 事件和关闭排空 |
| tests/history/tst_history_task_queue.cpp | 测试初始化失败、任务 FIFO 写入、超大快照拒绝和关闭前排空 |
| CMakeLists.txt | 将 worker/queue 加入应用目标，并注册独立 QtTest |
| docs/development-spec.md | 增加队列模块契约和当前分工状态 |

## API 和运行契约

- 构造后 worker 线程异步初始化。submit() 在初始化成功前返回 NotReady；后续可能返回 Accepted、QueueFull 或 Stopping。调用方须统计/处理拒收，不能把拒收当成已记录。
- 接收的请求经同一信号发送方发往同一个 worker，按 FIFO 顺序处理。worker 在其线程内创建 HistoryService，SQLite 连接和 ImageStore 均保持线程亲和。
- 背压限制为最多 32 个未完成任务，以及估算的 64 MiB 在途快照数据；文本按 QString 字节数估算，图片使用 QImage::sizeInBytes()。这不是整个进程的内存上限，可容纳常见 4K RGBA 图像。
- shutdown() 先关闭新提交，再把关闭事件排在已接受记录之后。worker 完成旧任务、销毁服务后发出 stopped 并退出线程。析构函数等待线程完成，以免丢弃已接受记录。
- 存储调用仍是同步的，但只发生在 worker 线程；单个大 PNG 编码或 DB 写入会延迟该队列后续任务，不会同步阻塞提交者/UI。

## 验证证据

- 构建：cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8，应用和全部测试目标成功。
- 测试：设置 Qt/MinGW PATH 和 QT_PLUGIN_PATH=C:/Qt/6.11.1/mingw_64/plugins 后运行 ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --output-on-failure，**6/6 suites 通过**。
- 新增队列测试确认：初始化失败时不接受写入；超过单个快照预算时返回 QueueFull；完成一张大图后可再次提交相同大小图像；FIFO 接收的三条文本在最新优先分页中按相反顺序展示；调用 shutdown 后所有已接受记录均已持久化，之后提交返回 Stopping。
- 性能基准、真实 QClipboard 平台测试、应用退出集成测试均未运行/未实现。

## 未完成项与风险

- HistoryTaskQueue 尚未由 main.cpp 创建；ClipboardCapture 尚未实现，当前应用不会因此开始记录剪贴板。
- 64 MiB/32 项是背压阈值。超限的采集将被拒收；接入方必须避免静默丢失，例如记录不含正文的拒收计数/事件并定义恢复策略。
- QImage 内存估算不覆盖 Qt/数据库编码缓冲区、隐式共享底层存储和其它进程内存。
- 析构会同步等待已接受任务写完。应用关闭/路径切换必须放在明确生命周期编排中；不要在仍有新生产者时先释放队列。
- 图片落盘后 DB 失败仍可能留下孤儿文件；删除和孤儿清理尚未实现。

## 接手者下一步

1. 实现 ClipboardCapture：只在 GUI 线程读取 QClipboard/QMimeData，复制文本、图像或本地 URL 快照，不做编码/哈希/文件 I/O。
2. 由应用生命周期拥有 HistoryTaskQueue；处理 initialized 失败、submit 背压拒收、recordCompleted 失败和退出排空。
3. 为文本、图像、本地 URL、混合 MIME、空 MIME、超长内容、敏感来源和应用自身回写建立隔离测试。
4. 单独增加 ClipboardCapture GUI 回调耗时和 worker 入队至提交时延的性能 runner；不读取用户历史数据库。
