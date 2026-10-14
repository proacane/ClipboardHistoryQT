# 任务交接：阶段2 - HistoryService 首个切片

- 状态：已实现并通过 Qt 6.11.1 MinGW Debug 构建与自动测试；尚未接入剪贴板采集、QML 或后台任务队列。
- 负责人对话：主任务
- 日期：2026-10-10
- 依据：[开发与测试规范](../development-spec.md)、[HistoryRepository](PHASE2-HISTORY-REPOSITORY.md)、[ImageStore](PHASE2-IMAGE-STORE.md)

## 当前行为（暂行产品假设）

- 精确重复键：`kind + content + image_ref`，按 SQLite 二进制文本相等匹配；不做大小写、URL 或 Unicode 归一化。
- 重复项保留原 ID，更新时间并递增 `usage_count`；新项次数为 1。递增到 `qint64` 上限后饱和，不溢出。
- 本次按“继续”暂用上次推荐的合并策略；用户尚未明确点选。若用户要求每次复制独立记录，应调整该策略及测试。
- 不自动删除记录；记录数量/年龄保留策略待设置和用户确认后实现。去重查找使用 SHA-256 非唯一索引，再比较原始字段防哈希碰撞；迁移以 256 行一批回填，降低额外内存。

## 改动清单

| 文件 | 用途 |
|---|---|
| `src/core/history/history_service.h/.cpp` | 同线程拥有 Repository/ImageStore；校验记录；图片先内容寻址落盘，再原子记录/合并数据库项；提供分页代理 |
| `src/core/history/history_repository.h/.cpp` | schema v3、v1/v2 到 v3 迁移、usage count、SHA-256 去重索引和事务内 `recordOrIncrement` |
| `tests/history/tst_history_service.cpp` | 重复合并、计数与时间排序、图像重复、无效输入、图像目录失败、线程边界 |
| `tests/history/tst_history_repository.cpp` | exact duplicate、same-millisecond 排序、v0 初始化、v1 保留已有数据迁移 |
| `CMakeLists.txt` | 应用与 `history_service` QtTest target 注册 |
| `docs/development-spec.md`、`docs/decisions/ADR-0001-technical-baseline.md`、Repository handoff | 同步模块状态和 schema 决策 |

## 线程与数据边界

- `HistoryService` 是同步、线程归属类：构造/初始化/记录/分页都必须发生在同一线程。正式调用方必须把它创建在单一存储 worker 中，不能从剪贴板 GUI 回调或 QML 直接调用；本模块尚未加入事件队列/worker 生命周期。
- 图片通过 `ImageStore` 原子落盘后，再由 Repository 事务新增或递增记录。数据库操作失败时不删除图片，因为相同内容文件可能已被既有记录引用；这种情况可能留下孤儿文件，待引用核对可靠后再做清理。
- 删除、置顶、搜索、暂停、保留策略、孤儿扫描、服务关闭/路径切换协调均未实现。

## 验证证据

- 构建：`cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8`，应用和全部测试目标成功。
- 测试：配置 Qt 6.11.1 / MinGW PATH 与 `QT_PLUGIN_PATH` 后，`ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --output-on-failure`，**5/5 suites 通过**。
- `history_repository` 12 QtTest cases：v0 初始化到 v3、v1/v2 数据迁移与哈希回填、使用次数、重复事件与同毫秒新近排序。
- `history_service` 7 QtTest cases：文本去重、图片引用与重复、输入校验、图片写入失败和错误线程。
- 性能基准未运行；这些结果只证明功能正确性，不代表大图吞吐或数据库压力达标。

## 风险和接手者下一步

1. HistoryService 已放入 HistoryTaskQueue 的专用 worker 线程；剪贴板采集、应用生命周期接线和路径切换停止/重启协调仍未完成。
2. 后续删除/清理必须先核对 DB 引用；不能在 DB 提交失败时移除共享图片。
3. 用户确认去重策略后再将“暂行”改成正式决策；历史保留阈值确认前不执行自动删除。
4. 添加数据库提交失败/磁盘满故障注入、孤儿核对和引用一致性测试，再接 ClipboardCapture 与 QAbstractItemModel。
5. UI/列表和合成数据接入后再跑规范中的 10k/100k/500k 与 1080p/4K 性能基准。
