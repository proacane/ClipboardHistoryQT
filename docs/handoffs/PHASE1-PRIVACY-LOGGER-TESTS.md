# 任务交接：阶段1 - PrivacyLogger 隔离测试

- 状态：已接入 CMake；Qt 6.11.1 MinGW Debug 构建和隔离测试通过。
- 负责人对话：阶段1 PrivacyLogger 测试子任务。
- 日期：2026-10-10。
- 关联开发规范/任务：`docs/development-spec.md` §1、§2、§3、§4；`docs/handoffs/PHASE1-PRIVACY-LOGGING.md`。

## 目标和结果

- 目标：自动验证日志只写数值事件字段，异步队列在 logger 析构时排空，并保证测试不接触真实用户数据。
- 已完成：新增 QtTest 源。测试复制自身到 `QTemporaryDir` 并从该目录启动子进程；`AppPaths` 以子进程 `applicationDirPath()` 为根，默认日志路径是临时目录内的 `data/logs/events.log`。测试确认四类记录落盘、字段顺序与数值正确，canary 与临时根绝对路径都未进入日志。
- 已完成：CTest target 已注册，完整回归 4/4 suites 通过。Windows CRLF 行尾导致初版断言多读到一个回车；测试现先规范化换行后通过。

## 改动清单

| 文件 | 变更用途 | 需要集成者注意的事项 |
|---|---|---|
| `tests/logging/tst_privacy_logger.cpp` | 隔离子进程写日志并断言格式、隐私 canary 和析构排空 | 测试目标需编译本文件、`src/core/logging/PrivacyLogger.cpp`、`src/core/settings/app_paths.cpp` 和 `src/core/settings/app_paths.h`，链接 Qt Core/Test；显式列头文件以确保 `AppPaths` 的 `Q_OBJECT` 由 AUTOMOC 处理。无需新增生产接口。 |
| `docs/handoffs/PHASE1-PRIVACY-LOGGER-TESTS.md` | 记录覆盖范围、隔离策略和验证状态 | 主任务已完成 CMake 集成及运行，并已更新状态和结果。 |

## 契约和决策

- 对外接口 / 数据 schema / 配置变化：无；子任务未修改生产代码；主任务随后集成 CMake target 并更新共享测试清单。
- 兼容影响：测试要求测试进程能复制自身并在临时目录启动。它把 Qt `BinariesPath` 前置到子进程 PATH，保留原 PATH 以供 MinGW runtime 加载；若项目测试环境仍无法加载该副本，集成时需在测试运行环境解决 DLL 搜索路径，不能退回到真实数据根。
- 测试隔离：临时目录初始为空，不存在旁置 `ClipboardHistory.ini`，所以 AppPaths 走默认 `temp/data`。测试不会调用 `setDataDirectory()`、不会读取剪贴板，也不会删除临时目录之外的文件。

## 覆盖与未覆盖

- 已覆盖源码断言：Event、Duration、Count、Error 四类记录；写线程输出顺序与各字段；logger 析构排空；canary 和临时根路径未出现在主日志。
- 未覆盖：队列满/争用丢弃计数（时序不可确定且当前没有队列容量注入接口）；滚动边界和归档数；目录/打开/写入/旋转失败；线程争用；连续高负载/性能。测试不能把 canary 作为正文传给 logger，因为其公开接口只接收数值；canary 作为隔离子进程参数传入并检查不落盘，验证范围应理解为“日志不夹带进程上下文文本”，而不是对自由文本输入过滤的测试。
- 需要接口方案时：若后续要确定性测试队列满载、滚动阈值和后端故障，应先由主任务评估可注入 AppPaths 根及可控 sink/阈值等生产接口；本子任务没有加入测试专用接口。

## 验证证据

| 类型 | 命令/场景 | 结果 |
|---|---|---|
| 构建 | cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8 | 成功，应用和四个测试目标均构建 |
| 测试 | ctest --test-dir build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --output-on-failure（已配置 Qt/MinGW PATH 和 QT_PLUGIN_PATH） | 4/4 suites 通过；privacy_logger 通过 |
| 手工验证 | 检查源码确认默认日志根是 `AppPaths::dataDirectory()/logs/events.log`，而测试子进程 executable 与临时根同目录 | 已静态检查并由隔离自动测试验证 |
| 性能基准 | 不适用 | 未运行 |

## 风险、已知问题与复现

- 风险/限制：副本子进程启动依赖当前 Qt/MinGW 运行时 DLL 可通过 PATH 解析。本机 Qt 6.11.1 MinGW Debug 已验证副本子进程可启动；其他 Qt/编译器组合仍需验证。
- 已知问题：队列满载、日志滚动与 I/O 故障目前不可通过现有公共接口稳定注入；没有断言 `droppedRecords()`，因为并发 worker 竞争可能使结果不确定。
- 复现步骤：从 CTest 运行 privacy_logger；它会创建临时目录、复制测试程序、启动 logger 子进程、检查临时目录下 `data/logs/events.log`，结束后由 `QTemporaryDir` 清理。

## 接手者下一步

1. 已在 BUILD_TESTING 条件注册 tst_privacy_logger，链接 Qt6::Core 和 Qt6::Test。
2. 已构建并运行测试；日志只写入测试临时目录，子进程 PATH/DLL 加载正常。
3. 后续评估队列满载、日志轮转和 I/O 故障的确定性测试方案。

## 集成验收

- [x] 已检查实际 diff 与交接文档一致
- [x] 测试 target 已注册并成功构建
- [x] 隔离测试已实际运行且临时目录外没有写入
- [x] 未覆盖的队列满载、轮转和 I/O 故障已保留在主任务测试清单
