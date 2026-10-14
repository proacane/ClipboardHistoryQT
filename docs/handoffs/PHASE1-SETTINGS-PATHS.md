# 任务交接：阶段1 - Settings / Paths

- 状态：集成并自动测试；路径迁移已改为下次启动执行
- 负责人对话：阶段1：路径与配置模块
- 日期：2026-10-10
- 关联开发规范/任务：[开发与测试规范](../development-spec.md)，[ADR-0001](../decisions/ADR-0001-technical-baseline.md)

## 目标和结果

- 目标：提供便携数据根、可自定义的数据路径和本地 INI 设置模块。
- 已完成：实现 AppPaths 与 AppSettings，支持四种主题值、显式默认值及配置 schema 版本 1。
- 未完成/阻塞：按本阶段更正，没有新增测试源文件；模块源文件已加入顶层 CMake；构建和测试均未运行。

## 改动清单

| 文件 | 变更用途 | 需要集成者注意的事项 |
|---|---|---|
| src/core/settings/app_paths.h | 路径 API 声明 | 调用前需在 QCoreApplication 设置应用名和组织名。 |
| src/core/settings/app_paths.cpp | 默认 EXE 旁 data/，支持迁移到用户选择的空目录 | 创建目录失败或 Qt 没有可写位置时返回空字符串；调用者应中止相关 I/O，且不要记录完整路径。 |
| src/core/settings/app_settings.h | 主题设置 API | Theme 为 Dark、Light、System、Blue；默认 Dark 与当前 QML 一致。 |
| src/core/settings/app_settings.cpp | QSettings 读写与 schema 版本策略 | 使用数据根 config.ini，与 QML Settings 显式共用 INI 文件。 |
| docs/handoffs/PHASE1-SETTINGS-PATHS.md | 接口和集成交接 | 测试源文件及共享构建配置留给主任务/验证阶段处理。 |

## 契约和决策

- 对外接口 / 数据 schema / 配置变化：命名空间 ClipboardHistory::Core。AppPaths 提供 dataDirectory()、imagesDirectory()、databaseFilePath()、configDirectory()。AppSettings 提供 theme()、setTheme()、isWritable()。QSettings 键为 Appearance/themeMode，现行值为“暗色”“浅色”“蓝色”；旧“跟随系统”值回退为暗色；schema 元数据为 meta/schemaVersion=1。
- 默认值：暗色（与当前 QML 的初始值一致）。无效主题值回退暗色；缺失或 v0 schema 升级到 v1。损坏、无效版本或更高版本配置禁写，避免覆盖/降级。
- 兼容影响：数据默认落在可执行文件旁 data/；ClipboardHistory.ini 只在可执行文件旁记录自定义根目录。config.ini、数据库、图片和日志位于当前数据根。AppSettings 使用 QSettings::IniFormat，QML QtCore.Settings 通过 location 显式访问同一 config.ini。
- 已同步给哪些依赖模块：交接主任务；本阶段未改调用方或共享构建文件。

## 验证证据

| 类型 | 命令/场景 | 结果 |
|---|---|---|
| 构建 | 未运行（模块现已加入目标，待用户用 Qt Creator 验证） | 未运行 |
| 测试 | 未运行；本阶段不添加测试源。后续测试项见下方接手步骤。 | 未运行 |
| 性能基准 | 不适用 | 不适用 |

建议集成后使用 Qt Creator 对所选 Qt 6.11 kit 执行 Configure 和 Debug Build。测试目标接入后执行 ctest --test-dir <build-dir> --output-on-failure；当前没有本模块测试可运行。

## 风险、已知问题与复现

- 风险/限制：自定义根指针仍需写入可执行文件目录，因此便携部署目录必须可写。当前路径切换复制数据快照，HistoryService/PrivacyLogger 的活动连接生命周期尚未接入；数据服务完成后须协调退出和重启。AppSettings 目前只覆盖主题，热键和保留策略后续扩展。
- 已知问题：模块已加入顶层 CMake，尚未构建验证；没有测试源/测试证据。调用者必须检查路径 API 的空返回值和 isWritable() / setTheme() 结果。
- 复现步骤：集成构建后，在隔离的 Qt 测试目录中构造 AppSettings；设置并重建实例验证主题往返；分别注入坏格式及高于 1 的 schema 验证禁写。测试需隔离 QSettings 的组织/应用名和标准路径，不得触碰用户真实配置。

## 接手者下一步

1. 用 Qt Creator 对 Qt 6.11 kit 执行 Configure 和 Debug Build。
2. 后续验证时使用隔离的 QSettings 路径/测试模式，验证不会碰真实用户配置。
3. 添加测试覆盖默认值、四主题写入/重启读取、无效主题恢复、缺失/v0 schema 升级、损坏配置与未来 schema 禁止写回、标准路径创建失败。
4. 后续接入 QML 时统一从 C++ AppSettings 访问 Appearance/themeMode；旧的“跟随系统”配置需回退为暗色。

## 集成验收

- [ ] 已检查实际 diff 与交接文档一致
- [ ] 依赖模块已收到接口变化
- [ ] 该模块必测项及集成测试已核对
- [ ] 未解决事项已进入主任务清单






## 主任务路径/Git 集成补充（2026-10-10）

- AppPaths 已改为默认 EXE 旁 data/，并公开 setDataDirectory() 给 QML；目标目录必须可写且为空。迁移复制完成后才更新 EXE 旁 ClipboardHistory.ini；失败时回滚已复制文件，旧目录保留。
- AppSettings、QML Settings、PrivacyLogger 共用所选数据根；SQLite API 位于 history.sqlite3，图片与日志目录分别为 images/、logs/。
- 初版设置界面曾使用路径编辑框；2026-10-10 已替换为独立设置页和 Qt 文件夹选择器。更改后建议重启，以确保后续模块重新打开数据库时使用新根。安装目录不可写（如 Program Files）时启动失败，不写入系统用户目录。
- 项目已初始化 Git；当前主机未配置 Git 提交作者，因此尚未创建初始提交。构建与测试未运行。
- 路径迁移使用手写目录复制/回滚，风险是并发运行的数据写入尚未与未来 HistoryService 停机流程协调；数据核心接入后必须在 UI 层禁用运行中切换或改成协调重启。

## UI 路径设置更新（2026-10-10）

- 外观与数据目录设置现在位于独立设置页；目录通过 Qt Quick Dialogs FolderDialog 选择，并以只读文本显示。
- AppPaths 增加 currentDataDirectoryUrl 供 Qt 文件夹选择器定位当前目录；CMake 增加 QuickDialogs2 组件和链接。
- 目录切换仍调用 setDataDirectory()，保留原有空目录/可写校验、复制和失败回滚；应用成功后仍建议重启。
- 构建与测试未运行；交接者应在 Qt 6.11.1 MinGW kit 中验证 CMake 配置、QML 类型导入和 Windows 文件夹选择器。

## Windows 路径选择跟进（2026-10-10）

- QML 将 FolderDialog 返回值交给 AppPaths.localFilePath()，由 Qt QUrl::toLocalFile() 转为本地路径，避免在 QML URL 值上调用不存在的 JS 方法。
- 修正 QDir::relativeFilePath() 在 Qt 中使用通用正斜杠输出、而 Windows 原逻辑使用反斜杠前缀造成的同级目录误拒绝；比较前使用 QDir::fromNativeSeparators() 统一格式。
- 用户反馈修改后路径应用无误。该运行验证未覆盖构建配置和自动化测试。

## 重启时迁移（2026-10-11）

- `setDataDirectory()` 现在只校验并记录 `Storage/pendingDataDirectory`，本次运行仍继续使用当前数据根；再次选择当前活动目录可取消待切换项。
- `AppPaths::applyPendingDataDirectoryChange()` 在 `main()` 中、服务创建前运行。它会再次校验目标为空且可写，复制失败时回滚新副本并保留源目录/待切换项；成功后再更新 `Storage/dataDirectory`。
- `tests/settings/tst_app_settings.cpp` 增加“重启应用待迁移”和“目标被占用时保留源数据”覆盖。测试启动器复制 exe 到隔离目录时需将 Qt bin 目录加到子进程 PATH。
- 验证：Qt 6.11.1 MinGW Debug 构建成功；7 个 CTest 单独执行均通过。一次合并 CTest 运行中第 2 个及之后的进程返回 `0xc0000135`，尚未确定是 CTest/Windows 执行环境还是启动时依赖问题；直接从同一构建目录依次运行 7 个测试 exe 均通过。应继续调查批量 CTest 行为。
- 用户无需验证迁移 UI；真实 Windows 文件夹选择器和实际目录迁移体验仍需后续平台 smoke test。
