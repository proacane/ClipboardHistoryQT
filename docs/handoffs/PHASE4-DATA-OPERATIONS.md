# 阶段 4：收藏、删除与清空

- 状态：完成（核心与异步模型接口）；待主任务接入 QML
- 负责人对话：`/root/favorites_delete`
- 日期：2026-10-11
- 关联开发规范/任务：`docs/development-spec.md` §1、§2 HistoryService、§6 首版验收

## 目标和结果

- 目标：首版支持收藏/取消收藏、删除单条记录和清空历史；删除图片时只清除数据库中已无引用的图片文件。
- 已完成：数据库 schema v4 的 favorite 字段及 v1/v2/v3 向前迁移；Repository/Service/Worker/Queue/Model 异步操作接口；图片引用安全清理；迁移、服务和模型操作隔离测试。
- 未完成：QML 尚未接入操作控件和清空确认；自动保留/自动清理策略未实现，按要求保持未决。

## 改动清单

| 文件 | 变更用途 | 需要集成者注意的事项 |
|---|---|---|
| `src/core/history/history_repository.h/.cpp` | schema v4、收藏读写、事务删除/清空、返回孤儿图片引用 | v1/v2/v3 都迁移到 v4；记录原有 favorite 默认 false |
| `src/core/history/history_service.h/.cpp` | 提供收藏、删除、清空；数据库提交后删除无引用图片 | 删除完成但图片文件清理失败时 `cleanupPending=true`，数据库删除仍成功 |
| `src/core/history/history_worker.h/.cpp` | 增加串行 worker 槽和 mutationCompleted 信号 | 所有操作仍由唯一存储线程执行 |
| `src/core/history/history_task_queue.h/.cpp` | 增加异步 `setFavorite`、`remove`、`clear` 请求 | 调用方法返回 false 表示队列未就绪/停止或 ID 无效 |
| `src/core/history/history_model.h/.cpp` | favorite role 读取真实值；暴露 QML invokable | QML 调用 `setFavorite(row, bool)`、`removeItem(row)`、`clearHistory()`；监听 `mutationFailed()` 和 `mutationCompleted(cleanupPending)` |
| `tests/history/tst_history_repository.cpp` | 收藏持久化、删除引用判断、v3→v4 迁移覆盖 | 隔离 SQLite 文件 |
| `tests/history/tst_history_service.cpp` | 图片共享引用、清空图片清理、清理失败仍保留已提交数据库状态 | 隔离临时目录 |
| `tests/history/tst_history_model.cpp` | 模型调用收藏/删除/清空异步链路 | 验证操作完成后列表刷新 |

## 契约和决策

- schema 从 v3 升到 v4，新增 `history_entries.favorite INTEGER NOT NULL DEFAULT 0`。v1/v2 先按既有迁移补齐 usage_count/content_hash，再新增 favorite；迁移仍在 initialization transaction 中。
- `HistoryRepository::remove(id, &orphanImageRef)` 事务内删除记录并查询剩余引用；只有提交成功且没有其他引用时才返回图片引用。`clear(&orphanImageRefs)` 事务内收集 DISTINCT 非空图片引用并删除所有记录。
- `HistoryService` 在数据库事务提交后清理孤儿图片。文件系统清理失败不会回滚已确认的记录删除，以免报告失败却无法恢复；结果通过 `cleanupPending` 告知调用端，未删文件会成为孤儿，可后续清理。
- Worker/Queue 按线程队列顺序运行 mutation；Model 成功后刷新列表，失败发出 `mutationFailed()`。
- 自动保留上限、按天清理和孤儿文件定期扫描不在本模块实现范围内。

## 验证证据

| 类型 | 命令/场景 | 结果 |
|---|---|---|
| 构建 | `cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --target tst_history_repository tst_history_service tst_history_task_queue tst_history_model --parallel 8` | 通过（Qt 6.11.1 MinGW Debug） |
| 构建 | 同构建目录 `--target ClipboardHistory --parallel 8` | 通过；核心模型接口与当前 app/QML 编译成功 |
| 测试 | `ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug -R '^history_repository$' --output-on-failure` | 通过；v3→v4 保留既有行并默认不收藏 |
| 测试 | 同上分别将正则替换为 `history_service`、`history_task_queue`、`history_model` | 4 个目标分别通过；未在同一次 CTest 进程批量运行 |
| 性能基准 | 未运行 | 本任务不涉及性能基准 |

运行测试时需将 `C:/Qt/6.11.1/mingw_64/bin` 和 `C:/Qt/Tools/mingw1310_64/bin` 加入 PATH，并设置 `QT_PLUGIN_PATH=C:/Qt/6.11.1/mingw_64/plugins`。单项测试结果符合项目当前验证方式；批量 CTest 的 Windows 启动问题仍由主任务跟进。

## 风险、已知问题与复现

- 若应用程序目录中的图片文件被占用或权限禁止删除，数据库记录仍按用户操作删除，结果 `cleanupPending=true`；磁盘上会留下无引用图片文件，不会影响其他记录。
- QML/main 尚未接线；当前核心测试不能替代用户手动验收收藏星标、删除交互和清空确认。
- 收藏过滤入口、清空确认框和清理失败提示由 UI 集成任务处理。

## 接手者下一步

1. 在 QML 行内收藏控件调用 `historyModel.setFavorite(index, !favorite)`；收藏图标绑定 `favorite` role。
2. 单条删除调用 `historyModel.removeItem(index)`；清空按钮调用 `historyModel.clearHistory()`，UI 侧在清空前确认。
3. 监听 `mutationFailed()` 与 `mutationCompleted(cleanupPending)`，展示通用失败或图片残留提示，不记录剪贴板内容。
4. 做完 QML/main 集成后运行 Debug 与 Release 构建，并由用户验收 Windows 真实使用体验。

## 集成验收

- [x] 已检查实际 diff 与交接文档一致
- [x] 依赖模块已收到接口变化（主任务已确认接手 QML/main/CMake）
- [x] Repository、Service、Queue、Model 隔离测试通过
- [x] 自动保留策略继续留待决策
