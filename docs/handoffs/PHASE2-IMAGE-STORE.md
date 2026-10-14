# 任务交接：阶段2 - ImageStore

- 状态：实现完成；Qt 6.11.1 MinGW Debug 构建和隔离测试通过
- 负责人对话：主任务（ImageStore 模块）
- 日期：2026-10-10
- 关联规范/任务：[开发与测试规范](../development-spec.md)、[ADR-0001](../decisions/ADR-0001-technical-baseline.md)、[HistoryRepository](PHASE2-HISTORY-REPOSITORY.md)

## 目标和结果

- 目标：把图片转换为可复用的 PNG 内容文件，以 SHA-256 作为数据库引用；文件原子提交，损坏或冲突时保护已有文件。
- 已完成：实现隔离 ImageStore、线程归属检查、输入引用校验、PNG 编码、内容寻址、重复内容去重、QSaveFile 原子提交、加载与删除；新增临时目录 QtTest。
- 未完成/阻塞：HistoryService 首个切片已协调图片写入与记录入库；删除时引用检查、孤儿回收和失败后孤儿处理仍未实现，本模块不能单独删除未确认无引用的图片。

## 改动清单

| 文件 | 变更用途 | 注意事项 |
|---|---|---|
| `src/core/history/image_store.h/.cpp` | PNG 内容文件存储 | 构造与所有调用固定在同一线程；应在存储工作线程创建。数据库只保存返回的 64 位小写十六进制引用，不保存可由用户控制的路径。 |
| `tests/history/tst_image_store.cpp` | 往返、去重、冲突保护、输入/路径故障和跨线程测试 | 只使用 QTemporaryDir，不接触用户图像目录。 |
| `CMakeLists.txt` | Qt Gui、模块源文件和 image_store CTest target | `BUILD_TESTING=ON` 时注册 `image_store` 测试。 |

## 接口和约束

- `ImageStore::store(QImage)` 成功返回 PNG 内容的 SHA-256 小写十六进制引用；物理路径为 `<imagesDirectory>/<reference>.png`，reference 不包含扩展名或路径。
- 相同编码结果复用同一文件；文件已存在时采用流式 SHA-256 校验，若目标内容不匹配则返回 `HashConflict`，不覆盖已有文件。
- 首次写入通过 `QSaveFile` 并在完整写入后 `commit()`；短写/失败调用 `cancelWriting()`。
- `load` 和 `remove` 只接受 64 个 ASCII 小写 hex 字符，防止路径注入。状态只返回错误码，不包含私有目录或图像内容。
- 所有 API 都必须从创建 ImageStore 的线程调用。PNG 编码、读写应由后续 HistoryService 的后台存储线程调用；剪贴板通知和 UI 线程不可直接做这些工作。
- ImageStore 不负责 DB 引用计数、回滚事务、旧图片清理或孤儿扫描；服务只有在 DB 引用安全提交/移除后才能协调删除。

## 验证证据

| 类型 | 命令/场景 | 结果 |
|---|---|---|
| 配置 | Qt 6.11.1 MinGW，`cmake -S . -B build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug -DBUILD_TESTING=ON` | 成功 |
| 构建 | `cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8` | ClipboardHistory、HistoryRepository 和 ImageStore 测试目标成功 |
| 测试 | `ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --output-on-failure`（预先配置 Qt/MinGW PATH 和 `QT_PLUGIN_PATH`） | 当前完整回归 5/5 CTest suites 通过；ImageStore 套件仍为 8 个用例（含 init/cleanup） |
| 性能 | 无基准 runner；未运行大图压力/性能基准 | 未运行 |

## 风险和未覆盖项

- 测试尚未注入系统级磁盘满、只读 ACL、进程中止和 QSaveFile commit 失败；这些留到存储服务故障注入阶段。
- encoded PNG 暂存在内存后哈希，以便先确定内容寻址目标；大图会额外占用压缩后数据量的内存。编码必须留在后台线程，性能基准需纳入 1080p/4K。
- HashConflict 防止损坏目标被静默覆盖；恢复/人工修复策略属于后续维护模块。

## 接手者下一步

1. HistoryService 首个切片已固定线程拥有 Repository 和 ImageStore；后续需接专用存储 worker 与 queued 请求。
2. 图片写入顺序为 ImageStore 原子写入 → DB 引用提交；DB 失败时当前保留可能的孤儿文件以避免误删共享图。删除时需先提交 DB 变更，再仅删除已无引用的旧图。
3. 增加数据库事务失败、引用一致性和孤儿回收集成测试；通过后更新本交接文档中的未完成项。
