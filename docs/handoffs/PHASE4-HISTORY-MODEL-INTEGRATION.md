# 任务交接：阶段4 - 历史模型与应用接线

- 状态：历史浏览、收藏、删除/清空和设置页已接通；Debug/Release 全目标构建通过；10 个测试目标在两种配置下分项通过。
- 日期：2026-10-11
- 依据：[开发与测试规范](../development-spec.md)、[阶段3 ClipboardCapture](PHASE3-CLIPBOARD-CAPTURE.md)

## 目标与完成内容

- 真实 SQLite 历史替换 QML 示例记录。
- C++ `QAbstractListModel` 按 50 条分页读取，SQL 只返回最长 512 字符的内容预览；展开长文本时异步读取完整内容，不顺带修改剪贴板。点击条目才复制；图片预览按本地文件 URL 异步解码。
- 搜索和类型筛选在 SQLite worker 线程中执行；QML 输入延迟 250ms 后触发查询。
- 应用主程序拥有队列、采集器和模型；退出时先销毁模型/采集器，再由队列排空并关闭 worker。
- QML 提供加载/空/错误态和复制结果状态；持续监听剪贴板；长文本异步展开，图片在条目内显示裁切预览并可展开；文件类别显示为“文件”，来源缺失时明确提示。另含收藏筛选/切换、按稳定记录 ID 删除、清空确认、旧数据库导入和四种主题/存储设置页。

## 变更文件

| 文件 | 用途 |
|---|---|
| `src/core/history/history_repository.h/.cpp` | 有界预览分页、子串搜索和类型筛选 |
| `src/core/history/history_service.h/.cpp` | 预览页、单条读取和复制请求 |
| `src/core/history/history_worker.h/.cpp` | worker queued page/content/copy 请求 |
| `src/core/history/history_task_queue.h/.cpp` | worker 请求 API 及响应转发 |
| `src/core/history/history_model.h/.cpp` | QAbstractListModel、分页、筛选、异步展开、图片 URL 和缺失文件校验 |
| `src/app/main.cpp` | 创建应用服务对象并接 QClipboard |
| `src/ui/Main.qml` | 实际历史列表、搜索、类型按钮、展开/收起、状态和复制交互 |
| `tests/history/tst_history_repository.cpp` | 有界预览、字面搜索和类型筛选测试 |
| `tests/history/tst_history_model.cpp` | `QAbstractItemModelTester`、分页行和完整内容复制测试 |
| `CMakeLists.txt` | 接入应用源码和 model 测试目标 |

## 搜索语义

目前搜索用 `instr(lower(content), lower(:search))`，作为字面子串匹配，不把 `%`/`_` 当通配符。SQLite `lower()` 默认仅覆盖 ASCII 大小写；中文按原样匹配。该路径没有 FTS 索引，500k 数据查询延迟待后续性能基准评估。

## 验证

- Debug 构建：`cmake --build build/Desktop_Qt_6_11_1_MinGW_64_bit_Debug --parallel 8`，通过。
- Debug/Release 各 10 个 CTest 目标：分别使用 `ctest --test-dir <build> -R '^<name>$' --output-on-failure -j1` 执行，均通过。
- 本次变更后 Debug 与 Release 的 `history_model`、`history_service`、`history_task_queue`、`clipboard_capture` 四个相关目标再次分项通过；模型测试覆盖异步完整内容、文件源缺失和图片 URL。
- 将多个测试放在一次 CTest 调用中时，首个测试通过后，部分后续 exe 仍返回 `0xc0000135`。独立 CTest 进程均通过；该 Windows 子进程加载问题尚未解释。
- Release 部署副本启动 smoke test 5 秒无 QML stderr 错误，副实例激活退出码 0。QML 构建编译通过；历史 context-property / `parent.hovered` 警告仍存在。未在真实剪贴板或输入框执行端到端自动粘贴。
- Release x64：`cmake -S . -B build/Release -G "MinGW Makefiles" ... -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON` 和 `cmake --build build/Release --parallel 8`，通过；本轮相关目标 `history_model`、`history_service`、`history_task_queue`、`clipboard_capture`、`windows_platform_controller` 在 Debug/Release 均分项通过。

## 用户体验回归修正（2026-10-12）

- 用户实测发现底栏记录数原先取的是已加载到 `ListView` 的行数，只有分页后才增长。新增全库 `totalCount`：首次加载和删除/清空/导入后从 SQLite 读取；新建记录按 worker 返回的去重结果即时递增。搜索和滚动分页不额外扫描总数，底栏始终显示全库总记录数。
- 用户反馈滚轮速度偏慢。`ListView` 使用 `WheelHandler` 将标准滚轮一格调为约 60 px，接近一条普通记录高度。
- 搜索体验用户认为略慢但可接受，因此本次保留现有字面子串查询，不新增 FTS 或索引。
- 本次只构建了 QML Profiler 配置：`cmake --build build/Perf-QmlProfiler --target ClipboardHistory --parallel 6`，通过。按本轮开发约束未运行或新增自动化测试；滚轮手感需用户下次使用确认，QML 帧时间仍未测量。

## 未完成/风险

- 自动保留策略、Linux 平台后端、正式安装包尚未实现；性能基准按用户要求暂缓。
- 文件路径若在列表载入后才失效，复制失败会通过 `sourceMissing` 角色即时更新对应行；监视源文件父目录，在路径恢复时清除提示，复制成功也会重核验。父目录不可监视时，下次复制或刷新列表会更新。仍需真实 Windows 文件剪切板场景验收。
- 搜索使用 SQLite 字面子串扫描，无 FTS；性能门槛暂缓。
- Windows 剪贴板 provider 可能在 GUI 线程取快照期间阻塞，需实机体验验证。
- QML 主题/窗口拖动、DPI 缩放、无边框窗口和多文件复制需用户验收。
- 一次 `ctest --test-dir build/Release --output-on-failure -j1` 中仅首项通过，后续 7 个进程以 `0xc0000135` 退出；逐项独立启动通过。Debug 有相同现象。根因仍未定位，需 Windows DLL loader / Process Monitor 诊断。
- Windows 托盘在 Explorer 重启后不会自动恢复；托盘/快捷键和完整剪贴板体验仍需桌面验收。
- 应用开源许可证待定；当前热键用 Win32 `RegisterHotKey`，未引入 QHotkey。

## 接手验收

1. 使用无敏感文本、链接、单/多文件 URL、图片逐项复制到剪贴板，确认列表显示与再次复制。
2. 确认剪贴板持续监听，并验证应用复制回写不会把同一内容反复写入历史。
3. 搜索 ASCII 大小写、中文、`%`、`_` 字符串；切换全部/文本/链接/路径/图片类别。
4. 重启验证主题和记录持久化；验证 FolderDialog 迁移只在重启后生效。
5. 以 Windows 10 验收长列表滚动、DPI 缩放、窗口拖动/缩放和退出排空。

