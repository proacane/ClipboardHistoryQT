# 阶段 6 / 性能基准（初步）

## 状态

部分完成：隔离合成 runner 已完成 repository 读取、文本/图片队列入库和清空清理的三轮独立 Release 进程测量，2026-10-12 又在用户 Qt Creator 主 RelWithDebInfo 构建中完成三轮复测。最新功能和长稳验收见 [本轮验收记录](ACCEPTANCE-2026-10-12.md)。完整性能门槛仍未完成；本报告不得作为发布签核。

2026-10-13 自主补测：当前 Release 编译 QML 的原生 Windows/D3D11 宿主完成 10k/100k 三进程启动与搜索、各约 60 秒滚动、10 张复杂合成 4K 图入库及短时隐藏/呼出资源观察。六进程通过、0 warning；首屏代理仍约 0.91–1.44 秒，Profiler 已定位隐藏设置页/弹窗首次创建。方法、数值、哈希和限制见 [Release 原生补测](PERFORMANCE-RELEASE-2026-10-13.md)。这不是正式主程序冷启动或 GPU Present 门槛签核，不覆盖真实剪贴板/托盘。

## 改动

- `benchmarks/performance_runner.cpp`：固定 seed 的临时数据集生成；按 10k/100k/500k 测首屏、深页、搜索命中/无命中；每项预热一次并采 30 次，写出 JSON/CSV；检查临时路径在 runner 构建目录内，进程退出自动清理。只创建合成数据，不访问系统剪贴板或用户数据库。
- `CMakeLists.txt`：新增关闭默认的 `CLIPBOARDHISTORY_BUILD_BENCHMARKS` 选项和独立 `clipboard_history_perf` 目标，不影响常规应用构建。
- `docs/performance/2026-10-11-synthetic-read-baseline.md`：方法、限制、结果和分析。
- `docs/performance/2026-10-11-synthetic-read-baseline.json`、`.csv`：原始摘要及采样。
- `docs/development-spec.md`：更新性能阶段状态。

## 验证

- Qt 6.11.1 / MinGW GCC 13.1.0 Release runner 构建通过。
- 命令：

  ```powershell
  & 'C:\Qt\Tools\CMake_64\bin\cmake.exe' -S . -B build/Perf-Baseline -G 'MinGW Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/mingw1310_64/bin/mingw32-make.exe -DCMAKE_AR=C:/Qt/Tools/mingw1310_64/bin/ar.exe -DCMAKE_RANLIB=C:/Qt/Tools/mingw1310_64/bin/ranlib.exe -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64 -DBUILD_TESTING=OFF -DCLIPBOARDHISTORY_BUILD_BENCHMARKS=ON
  & 'C:\Qt\Tools\CMake_64\bin\cmake.exe' --build build/Perf-Baseline --target clipboard_history_perf --parallel 4
  $env:PATH = 'C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
  & '.\build\Perf-Baseline\clipboard_history_perf.exe'
  ```

- 三个独立进程各生成 10k/100k/500k 数据集；每个读操作每进程 30 个样本；临时数据根在各次运行后自动清理。
- 三轮 P95 中位数：500k 首 50 条 0.116 ms、深页 30.252 ms、搜索命中 61.633 ms、搜索无命中 427.779 ms。完整数字见 performance 报告和原始 CSV/JSON。
- 此处初测尚未运行冷/热启动、GUI 交互、QML 帧时间和 30 分钟长稳；三轮重复、队列入库和清空后来已补齐，见后文。清空不代表自动保留或孤儿扫描。本轮不是完整门禁。
- 首次从代理 shell 启动 QML Profiler 时，启动环境未显式包含 Qt/MinGW `bin`，目标进程异常退出；补齐 `PATH` 后应用窗口可创建，并在约 15 秒后连接 Profiler。该次诊断未保存 trace。命令行按工具帮助使用可执行文件位置参数（不加 `--`）：

  ```powershell
  Set-Location 'E:\Code\ClipBoardHistoryQT\build\Perf-QmlProfiler'
  $env:PATH = 'C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
  & 'C:\Qt\6.11.1\mingw_64\bin\qmlprofiler.exe' -o 'profiler\traces\qmlprofiler-trace-ClipboardHistory-manual.qtd' '.\ClipboardHistory.exe'
  ```

  测试版数据根配置在 `build/Perf-QmlProfiler/ClipboardHistory.ini`，指向 `build/Perf-Baseline/perf-data-CoMotI/500000`，其中只有合成记录。应用退出后 trace 才写完。
- 故障根因与处理：合成 500k 数据目录由 Codex 沙箱账号创建，目录 ACL 的所有者是 `DESKTOP-73LE3U3\CodexSandboxOffline`；普通桌面用户只有读取权限。此前在沙箱账号下进行的 Qt 可写探针和主窗口启动检查不代表桌面账号，因此用户运行 Profiler 时仍会得到“无法写入软件数据目录”。已只对 `build/Perf-Baseline/perf-data-CoMotI/500000` 合成测试目录树授予 `BUILTIN\Users` 修改权限，并确认该目录及 `history.sqlite3` 的 ACL 已包含 Modify；没有触碰用户数据目录。
- 修复后复核：使用同一测试版和数据目录，以 Qt/MinGW `bin` 加入 `PATH` 后，Qt `AppPaths` 探针通过，应用主窗口可创建；Profiler 约 15 秒后连接成功。此项验证是在沙箱账号下完成，不能替代桌面账号最终验证。诊断启动过程未保存 trace，因此 QML 帧数据仍待采集；现在可重新运行上面的 Profiler 命令继续采集。

## 风险与后续

- 2026-10-11 初测的 500k 无命中搜索接近候选 500 ms 阈值；2026-10-12 三轮复测 P95 为 403–449 ms，中位 408 ms。用户认为搜索速度可接受；`OFFSET` 深页延迟仍随数据量上升。
- 设备为 Ryzen 9 7940HX / Windows 10 22H2 / 16 GiB；GPU 型号和 SSD/HDD 介质未采集。当前记录不等同文档候选 Ref-01 配置。
- 2026-10-12 已补齐读取、入库和清空清理三轮数据；500k 隔离 QML 的 RSS 趋势和 30 分钟运行校验也已完成。仍需真实桌面 GPU 滚动帧时间、实际应用缓冲的内存口径、冷/热启动及尚未实现的自动保留/孤儿扫描。

## 补充：入库与清理基准（2026-10-12）

### 状态与发现

- 性能 runner 已扩展：在 10k/100k/500k 读取场景外，增加文本/图片经 `HistoryTaskQueue` 入库，以及 `HistoryService::clear()` 清理数据库与图片文件的测量。
- 首次 4K 入库自检发现 3840×2160 RGBA 图像为 32 MiB，原 16 MiB 快照与队列上限会在数据库前拒收。上限已改为 64 MiB，32 项任务数限制不变；标准 4K RGBA 图像现可进入处理队列。超过 64 MiB 的快照仍会被拒绝。
- 用户确认搜索速度可接受。三轮 500k 无命中 repository 搜索 P95 均低于候选 500 ms；保留当前字面子串搜索实现。

### 方法与限制

- 3 个独立 Release 进程，Qt 6.11.1 / MinGW GCC 13.1.0，Windows 10 22H2；每次生成固定 seed 的合成数据，退出时删除本次临时目录。
- 入库每个场景采 30 次：32 字符唯一文本、16 KiB 唯一文本、重复文本、1080p 唯一图、4K 唯一图。逐项串行提交；计时从 `HistoryTaskQueue::submit()` 到 `recordCompleted`，包含队列派发、编码/哈希、文件写入和数据库提交，不含 GUI 剪贴板快照。
- 合成图使用纯色底加一个变化像素，适合复现尺寸和真实编码路径，但不代表高复杂度照片/截图的数据压缩成本。
- 清理每个独立进程对 10k/100k/500k 数据集各测一次 `HistoryService::clear()`；同时验证数据库记录归零且图片目录为空。每个规模每轮只有一个耗时样本，因此下表是三轮单次耗时，不是稳定的 P95。
- runner RSS 是独立 benchmark 进程的工作集；它不代表 GUI 应用滚动后的内存，也不能替代 5 分钟内存趋势测量。

### 入库结果

单位：毫秒。每列为一次独立进程中 30 个串行样本的 P95；“三轮中位”是这三个 P95 的中位数。

| 操作 | 第 1 轮 | 第 2 轮 | 第 3 轮 | 三轮中位 |
|---|---:|---:|---:|---:|
| 唯一文本 32 字符 | 1.979 | 1.963 | 2.546 | 1.979 |
| 唯一文本 16 KiB | 2.302 | 2.225 | 2.441 | 2.302 |
| 重复文本 | 2.150 | 2.083 | 2.296 | 2.150 |
| 唯一图片 1080p | 40.228 | 40.936 | 44.755 | 40.936 |
| 唯一图片 4K | 148.403 | 144.718 | 149.762 | 148.403 |

以上入库值低于规范候选线（文本 100 ms、4K 图片 2 s）。由于测试图复杂度低，4K 图像结果只代表这份合成夹具。

### 清理结果与搜索复测

| 操作 | 第 1 轮 | 第 2 轮 | 第 3 轮 | 三轮中位 |
|---|---:|---:|---:|---:|
| 清空 10k 记录和图片（单样本） | 9.999 | 9.858 | 10.106 | 9.999 |
| 清空 100k 记录和图片（单样本） | 75.327 | 74.266 | 74.187 | 74.266 |
| 清空 500k 记录和图片（单样本） | 373.916 | 367.867 | 378.194 | 373.916 |
| 500k 搜索无命中 P95 | 407.606 | 448.857 | 403.293 | 407.606 |

清理三个规模的数据库与图片一致性自检全部通过。清空数据不是尚未实现的自动保留/孤儿扫描策略，后者仍未测。

### 构建、测试和原始数据

- Release 性能 runner 与 `ClipboardHistory` 构建通过；Debug `tst_clipboard_capture`、`tst_history_task_queue` 分项测试通过。4K 图片快照、4K 队列接收和超 64 MiB 拒收均有覆盖。
- 三轮原始 JSON/CSV：
  - [第 1 轮 JSON](../performance/2026-10-12-full-benchmark-run1.json) / [CSV](../performance/2026-10-12-full-benchmark-run1.csv)
  - [第 2 轮 JSON](../performance/2026-10-12-full-benchmark-run2.json) / [CSV](../performance/2026-10-12-full-benchmark-run2.csv)
  - [第 3 轮 JSON](../performance/2026-10-12-full-benchmark-run3.json) / [CSV](../performance/2026-10-12-full-benchmark-run3.csv)

### 补充：RelWithDebInfo 体验与 QML trace（2026-10-12）

- 用户反馈在 RelWithDebInfo 测试构建中手测未发现明显卡顿；该构建已配置 50 万条合成记录。操作时长和具体场景未记录，因此这属于定性体验结果。
- 原 skill 解析器遗漏采用 timing1..5 的 SceneGraph 事件，原先的“4 帧、333.33 ms 卡顿”结论撤回。原始 trace 为 106.9456515 秒、31,883 条记录，含 4,255 条 RenderLoop 完成事件；4 个动画采样不是总帧数。完成间隔 P95 14.0717 ms、最大 29.470 s，没有场景标记，不能把静止/隐藏与卡顿区分；按大于 100 ms 间隔分段后的最长连续段仅 7.935 秒，未证明连续 60 秒滚动。见 [原始 SceneGraph 摘要](../performance/2026-10-12-res-scenegraph.json) 和 [更正的分析报告](../../profiler/reports/profile-report-ClipboardHistory-2026-10-12-171558.md)。
- 原 trace 的三个对话框宽度循环和无效 Accessible.Text 已在本轮源码修复，并通过隔离 QML 交互回归；旧 trace 不能证明修后帧性能。

### 补充：主 RelWithDebInfo 构建三轮复测

- 在既有 `Desktop_Qt_6_11_1_MinGW_64_bit_RelWithDebInfo` 中启用 benchmark 目标，不创建新的应用构建变体。生成器 v2 修正合成数据哈希，与 Repository 的长度前缀算法一致；生成后通过真实去重接口自检。
- 500k 无命中搜索 P95 为 666.704 / 678.587 / 483.247 ms，2/3 超过 500 ms 候选线，不能写为性能通过。与上文纯 Release 构建选项不同，且第一轮有夹具准备负载，不能用差值直接判代码回归。
- 首 50 条 repository P95 为 0.240 / 0.230 / 0.153 ms；4K 简单合成图入队至提交 P95 为 170.560 / 152.137 / 257.279 ms。计时范围及三轮完整结果见 [本轮验收记录](ACCEPTANCE-2026-10-12.md)。
- 原始数据：[第 1 轮](../performance/2026-10-12-acceptance-relwithdebinfo-run1.json)、[第 2 轮](../performance/2026-10-12-acceptance-relwithdebinfo-run2.json)、[第 3 轮](../performance/2026-10-12-acceptance-relwithdebinfo-run3.json)，各有同名 CSV。

### 剩余门槛

- 交互式 QML 长列表滚动帧时间仍待有效量化；手测无明显问题不能替代 60 秒帧时间采集。
- 独立 QML 测试使用 500k 合成副本和真实数据模型，已完成 30 分钟：59,869 次滚动驱动、895 次查询、179 轮录入/读取/删除、最终计数正确、0 Qt/QML warning，队列关闭 59.27 ms。RSS 采样峰 115.23 MiB、末值 82.72 MiB，外部 OS 峰值观测 166.79 MiB；后半程中位仍轻微上升，不签无限期无泄漏。offscreen/software 渲染不能替代真实桌面 GPU 帧验收；GUI 冷/热启动仍未测。见 [最终 JSON](../performance/2026-10-12-acceptance-soak-500k.json) 与 [统计摘要](../performance/2026-10-12-acceptance-soak-summary.json)。
- 清理基准只测清空，不包含自动保留或孤儿扫描策略；系统剪贴板 GUI 回调耗时也不包含在入库计时中。
