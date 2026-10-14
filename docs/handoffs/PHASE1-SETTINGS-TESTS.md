# 任务交接：阶段1 - AppSettings / AppPaths 隔离测试

- 状态：已接入 CMake；Qt 6.11.1 MinGW Debug 构建和隔离测试通过
- 负责人对话：settings_tests
- 日期：2026-10-10
- 依据：[开发与测试规范](../development-spec.md)，[阶段1 Settings/Paths 交接](PHASE1-SETTINGS-PATHS.md)

## 目标和结果

为 AppSettings 和 AppPaths 增加 QtTest 覆盖，不改生产 API。测试运行器将测试可执行文件复制到 QTemporaryDir 并从副本启动子进程；AppPaths 以 `QCoreApplication::applicationDirPath()` 为根，因此该方式让 bootstrap 配置、数据目录和 config.ini 全部落在本次临时根内。每个用例只清理临时根下已知的 `ClipboardHistory.ini`、`data/`、`migrated/` 和 `occupied/`。

## 改动清单

| 文件 | 用途 |
|---|---|
| `tests/settings/tst_app_settings.cpp` | Settings 默认值/schema/主题往返/无效值与未来版本；Paths 派生路径、可写检查、URL 辅助、空目录迁移、拒绝非法和非空目录；运行器隔离 |
| `docs/handoffs/PHASE1-SETTINGS-TESTS.md` | 测试覆盖、隔离策略和集成步骤 |

本任务只新增测试与交接文档；主任务已将测试目标集成到 CMake，并更新本交接文档的验证状态。

## 覆盖用例

- 默认主题暗色，并将缺失/v0 schema 升级到 v1。
- 四种主题在实例重建后仍可读取。
- 未知主题回退暗色且不覆盖配置原始值。
- 非数字及高于当前版本的 schema 禁止写入，并保留未来版本值。
- 路径派生关系、可写探测、QUrl 与本地路径辅助。
- 迁移到空目录时复制内容、更新 bootstrap 指针并发出变更信号。
- 空路径、相对路径和非空目标被拒绝；已有目标文件保持不变，bootstrap 不被创建。

## 安全隔离

测试目标在普通 CTest 调用下不直接运行用例。父进程仅复制测试 exe 到新建的 `QTemporaryDir`，并用环境变量标记子进程和临时根；子进程验证 `applicationDirPath()` 与该根完全一致后才执行用例。这样不会读写 Qt Creator 构建目录已有的 `ClipboardHistory.ini`、`data/` 或 `datas/`。临时目录由 `QTemporaryDir` 在测试结束时清理。

## 验证状态

| 类型 | 结果 |
|---|---|
| 源码检查 | 已按当前 AppSettings/AppPaths API 编写并通过构建 |
| QtTest | CTest app_settings 通过；完整回归 4/4 suites 通过 |
| 环境失败场景 | 未覆盖应用目录不可写、QSettings 实际磁盘满/权限错误；依赖平台权限、容器或故障注入，且当前生产 API 无安全的注入点 |
| 路径迁移回滚 | 未模拟符号链接/复制中断等失败；Windows 符号链接创建可能要求额外权限，生产 API 不提供文件操作注入点 |
| UI/系统主题变化 | 不属于 AppSettings/AppPaths 核心单元测试，需 UI/平台层验证 |

## 接手者下一步

1. 已将本测试源与 `src/core/settings/app_paths.cpp`、`src/core/settings/app_settings.cpp` 组成 QtTest 目标，链接 Qt Core、Qt Test。
2. 已在现有 Qt 6.11.1 MinGW 构建目录运行测试；测试必须由普通 CTest 父进程启动，以触发临时目录副本隔离。
3. 若后续 Windows 子进程启动失败，先确认 CTest 继承了 Qt/MinGW DLL 的 PATH；不要移除临时目录隔离而直接在项目构建目录执行。
4. 已记录实际构建和测试结果；隔离策略保持不变。
