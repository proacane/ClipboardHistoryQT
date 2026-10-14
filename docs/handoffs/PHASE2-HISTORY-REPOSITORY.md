# 任务交接：阶段2 - HistoryRepository

- 状态：已集成；Qt 6.11.1 MinGW 构建与隔离自动测试通过
- 负责人对话：阶段2：HistoryRepository 数据仓库
- 日期：2026-10-10
- 关联开发规范/任务：[开发与测试规范](../development-spec.md)，[ADR-0001](../decisions/ADR-0001-technical-baseline.md)，[阶段1 Settings / Paths](PHASE1-SETTINGS-PATHS.md)，[阶段1隐私日志](PHASE1-PRIVACY-LOGGING.md)

## 目标和结果

- 目标：实现 SQLite 元数据仓库与完全隔离的测试源，不接入采集、服务、图片存储或 UI。
- 已完成：添加线程归属检查、schema 初始化/migration、参数化 CRUD、稳定分页、明确错误码和临时数据库测试；主任务后续已扩展到 schema v3。
- 未完成/阻塞：只读介质和磁盘满故障仍未覆盖；HistoryService 首个切片已接入仓储，尚未接入采集/UI。

## 改动清单

| 文件 | 变更用途 | 需要集成者注意的事项 |
|---|---|---|
| `src/core/history/history_repository.h` | 仓库窄接口、数据类型和错误码 | `HistoryRepository` 构造、初始化、调用和析构须在同一线程；仅 `initialize()` 创建 SQLite 连接。 |
| `src/core/history/history_repository.cpp` | SQLite 初始化/migration、参数化 CRUD 和分页 | SQL 错误仅返回错误码，不带 SQL 错误文本或私人路径；不记录日志。 |
| `tests/history/tst_history_repository.cpp` | CRUD、稳定分页、非法参数、跨线程、v0 初始化迁移、未来 schema 和损坏 DB 测试 | 所有数据库文件均创建于 `QTemporaryDir`。 |
| `CMakeLists.txt` | Qt Sql 应用依赖、仓库源文件、可选 Qt Test 目标 | 测试由 `BUILD_TESTING` 控制，默认跟随 CTest 默认值。 |

## 契约和决策

- 对外接口 / 数据 schema / 配置变化：`ClipboardHistory::Core::HistoryRepository` 提供 `initialize()`、`insert()`、`get()`、`update()`、`page()`、`remove()`。`RepositoryStatus` 使用 `RepositoryError` 分类失败。当前 schema v3 的 `history_entries` 保存自增 ID、kind、content、image_ref、UTC 毫秒时间戳、usage_count 和 content_hash；v1 usage_count 默认 1，v1/v2 历史项会回填 SHA-256。
- 排序：`created_at_ms_utc DESC, id DESC`，同一时间戳时 ID 决定稳定次序。分页参数为 offset/limit，拒绝负 offset 和非正 limit。
- 兼容影响：数据库路径由调用方传入，仓库不依赖 AppPaths；连接名使用 UUID 防止 Qt SQL 全局连接名冲突；连接在构造线程的 initialize 调用中创建，所有操作拒绝异线程调用，析构要求回到所属线程。
- 未冻结决策保留给上层：不实现或假设去重、置顶、保留/清理、搜索规则或索引策略；接口当前没有搜索谓词，后续按产品决策扩展。仓库也不管理图片文件生命周期。
- 已同步给哪些依赖模块：Repository 与 ImageStore 已由主任务集成；HistoryService 通过 `recordOrIncrement()` 调用仓储。

## 验证证据

| 类型 | 命令/场景 | 结果 |
|---|---|---|
| 构建 | Qt 6.11.1 MinGW；`cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8` | 应用与测试目标构建成功 |
| 测试 | `ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --output-on-failure`（配置 Qt/MinGW 运行环境） | HistoryRepository 9 passed，0 failed |
| 性能基准 | 不适用，本模块未建立基线 | 未运行 |

## 风险、已知问题与复现

- 风险/限制：schema 当前只有版本 0 到 1 的初始创建路径；未来 schema 版本会明确拒绝，正式已有数据库的逐版升级需在 schema 演进时增加 migration。事务用于 schema 初始化，单条 CRUD 由单 SQL 语句原子执行。
- 已知问题：当前隔离测试覆盖基本读写、分页、线程拒绝、未来 schema 和损坏库；只读介质与磁盘满故障尚未覆盖。
- 复现步骤：Qt Creator 配置测试目标后运行 `tst_history_repository` 或 CTest；测试只使用各用例 `QTemporaryDir` 中的文件。

## 接手者下一步

1. 在 Qt Creator 选择 Qt 6.11.1 MinGW 64-bit kit 后执行 Configure，确认 Qt Sql、SQLite driver 和 Qt Test 可用。
2. 构建 `ClipboardHistory` 与 `tst_history_repository`，运行测试目标；若需 CMake 命令，可用 `cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="<Qt 6.11.1 MinGW 安装路径>"` 后 `cmake --build build --target ClipboardHistory tst_history_repository`，再 `ctest --test-dir build --output-on-failure`。
3. 接入 HistoryService 时由其负责产品去重/保留策略；搜索语义确定后再协商仓库接口和索引迁移。

## 集成验收

- [ ] 已检查实际 diff 与交接文档一致
- [ ] 依赖模块已收到接口变化
- [ ] HistoryRepository 必测项及集成测试已核对
- [ ] 未解决事项已进入主任务清单




## 主任务集成核对（2026-10-10）

- 已核对实际改动与模块边界；仅新增 history repository、隔离测试、Qt Sql/Test CMake 接入和本交接文档。
- 当前本机 Qt 6.11.1 MinGW 安装包含 Qt6Test CMake package 和 QSQLITE 插件文件；这只确认文件存在，不代表配置、插件加载或测试成功。
- 集成审阅补强连接清理：无效 SQLite driver/连接时也会移除已登记的 Qt SQL 连接名。
- HistoryRepository 已完成本机 Qt 6.11.1 MinGW 构建和自动测试；实际结果见本文“主任务构建与测试验收”。

## 主任务构建与测试验收（2026-10-10）

- 构建：`cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8`，应用和测试目标均成功。
- 测试：设置 Qt 6.11.1 / MinGW 运行时与插件路径后，`ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --output-on-failure`，HistoryRepository 用例通过（QtTest 9 passed，0 failed）。
- 修复：null `QString` 绑定 SQLite `NOT NULL` 列会成为 SQL `NULL`；insert/update 现在将 null content/imageRef 规范为空文本，覆盖无图片引用记录。
- 未覆盖：只读介质/磁盘满故障注入、HistoryService 接入、搜索、性能基准。

## 主任务集成扩展（2026-10-10）

- schema 从 v1/v2 迁移到 v3，给既有记录补 usage_count/content_hash 并分批回填；新库直接创建 v3。迁移仍在初始化事务内执行。
- 新增 `recordOrIncrement()`，用 SHA-256 索引定位候选项，再按精确 kind/content/image_ref 校验；事务内递增次数、更新时间或新增 usage_count=1 的记录。次数达到 int64 上限时保持饱和。
- 分页和单条读取返回 usage_count；普通低层 `insert()` 仍按单条插入，合并行为由 HistoryService 调用 recordOrIncrement。
- 重复事件即使系统毫秒时间相同，也会取得单调递增的记录时间，保持 recent-first 分页顺序；这可能在高速事件下让时间显示轻微超前。
- 当前 Debug 构建和 5/5 CTest suites 通过；仓储 QtTest 为 12/12。新增 v1/v2 migration、哈希回填、usage_count 与重复排序测试。性能、磁盘满和并发多进程写入未验证。
