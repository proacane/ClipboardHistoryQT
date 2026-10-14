# 2026-10-12 验收记录

## 范围与结论

本轮验收使用用户 Qt Creator 的主构建目录 `build/Desktop_Qt_6_11_1_MinGW_64_bit_RelWithDebInfo`，并复核同 Kit 的 Debug 构建。Qt 6.11.1、MinGW GCC 13.1.0、Windows 10 22H2、Ryzen 9 7940HX、16 GiB；GPU/驱动、供电模式、磁盘介质和正式 Ref-01 尚未冻结。

长稳期间只读查询到当前电源方案为“平衡”（2026-10-12 10:55 UTC），未更改设置或锁定全程电源状态。GPU/WMI 和物理磁盘查询被当前账号权限拒绝，未取得型号/介质，未将未知值推断为 SSD 或特定显卡。

**功能自动验收与 30 分钟隔离运行校验通过；完整发布门禁尚未通过。** 用户反馈在 50 万条测试数据下手测没有明显问题，是体验证据，未记录具体操作时长和场景。真实桌面帧门禁、正式性能环境、应用模块集成和发行项仍有缺口，见末尾清单。

测试不操作系统剪贴板、实际托盘菜单、全局热键或用户历史数据库。QML 用真实 HistoryModel/HistoryTaskQueue/Service/Repository，平台和路径边界替身只处理临时目录。平台进程测试使用独立 UUID 和临时锁目录。

## 本轮发现、修复与回归

| 项目 | 修复 | 证据 |
|---|---|---|
| CTest 整批运行首项之后 DLL 加载失败 | 每个测试显式设置所选 Qt/编译器运行库路径 | 原整批 1/10，修后 11/11；无需用户修改系统 PATH |
| 多文件内容超过 512 字符误报源文件丢失 | 展示仍截断，完整源路径独立传递；Service 在工作线程检测 | 修前长路径回归失败，修后通过 |
| 原本存在的文件被删除、恢复、再次删除时行内状态过期 | 监听全部已加载文件记录；失去父目录时监听最近存在祖先 | 文件循环删除/恢复、嵌套父目录重建回归通过 |
| 盘根目录事件匹配、异步检查期间再次变化 | 正确处理盘根分隔符；处理中变化标记后补查 | 盘根通知及延迟旧结果的确定性回归通过 |
| 三个 Dialog 的 implicitWidth 绑定循环 | 为删除/清空/导入对话框指定受窗口约束的宽度 | QML 初始化与交互用例无 QQmlEngine warning |
| 无效的 Accessible.Text | 使用 Qt 支持的 StaticText 角色 | 全文展开用例无无障碍角色警告 |
| 图片折叠裁切位置 | 显式 AlignTop | 96 px 顶部裁切、展开完整图片、收起回归通过 |
| 快滚与筛选刷新时 DelegateModel::cancel 警告 | ListView 设置 cacheBuffer: 0，关闭屏外异步预加载，保留可见区域虚拟化 | 原生 Qt 列表对照能复现；逐行移除方案在实际 delegate 中仍失败，已回退；最终配置 30 分钟 0 Qt/QML warning |
| 合成数据重复哈希与真实 Repository 算法不一致 | 生成器 v2 使用相同长度前缀算法，生成后调用真实去重接口自检 | 三轮基准生成及自检通过；未改用户原测试库 |

总数回归覆盖 120 条记录首次只加载 50 条、继续分页 100/120 条、新增、重复、删除、清空。总记录数与当前已加载条数分开验证。

## 自动功能验收

- Debug：11/11 CTest 模块通过。
- RelWithDebInfo：最终源码完整 11/11 CTest 模块通过。
- QML：真实 delegate 的全文/图片展开收起、四种主题、设置落盘、关闭对话及记住选择、录制原快捷键及 Escape 取消通过。
- Windows 控制器：直接退出、托盘退出命令的子进程正常结束，退出后同一实例锁可再次获取；没有创建真实托盘图标或发送输入。
- 原有 Repository、ImageStore、Service、Queue、MIME 分类、安全标记、迁移、日志 canary、旧库只读导入/重试等模块回归通过。

边界：AppSettings 的 schema/配置测试与 PrivacyLogger canary 证明各自模块；main/QML 尚未接入这两个 C++ 模块，实际外观设置使用 QtCore.Settings。不能把模块测试写成应用配置 schema 保护或应用隐私日志集成已验收。

原始自动化证据：[Debug JUnit](../acceptance/2026-10-12/ctest-debug.xml)、[RelWithDebInfo JUnit](../acceptance/2026-10-12/ctest-relwithdebinfo.xml)、[文件状态修前失败](../acceptance/2026-10-12/model-before-fix.xml)、[首批修后通过](../acceptance/2026-10-12/model-after-fix.xml)。新增盘根/异步回归由最终 CTest 覆盖。默认 CTest 的 optionalSoak 跳过，不将跳过算作长稳完成。

## 当前主构建性能复测

固定 seed，生成器 v2，3 个独立 RelWithDebInfo 进程。读取/入库每项预热后 30 样本；清空每个规模每轮 1 样本。只使用本轮临时数据，退出自动删除。第一轮期间有验收夹具准备，不作为独占机器测量；其他桌面负载/供电状态未锁定。

| 指标（ms） | 第 1 轮 | 第 2 轮 | 第 3 轮 | 判定 |
|---|---:|---:|---:|---|
| 500k 首 50 条 repository P95 | 0.240 | 0.230 | 0.153 | 读取很快，不能等同 GUI 冷启动 |
| 500k 深页 P95 | 42.928 | 40.133 | 31.092 | 记录结果；OFFSET 开销随页深增加 |
| 500k 命中搜索 repository P95 | 95.432 | 93.928 | 61.271 | 低于 500 ms 候选线 |
| 500k 无命中搜索 repository P95 | 666.704 | 678.587 | 483.247 | 2/3 超候选线；不能判通过 |
| 唯一短文本入队至提交 P95 | 2.787 | 2.268 | 2.506 | 低于 100 ms 候选线 |
| 16 KiB 唯一文本 P95 | 2.748 | 2.672 | 3.052 | 低于 100 ms 候选线 |
| 重复文本 P95 | 2.555 | 2.177 | 2.237 | 去重与结果正确性自检通过 |
| 1080p 合成图 P95 | 54.983 | 42.294 | 58.156 | 夹具为纯色加变化像素 |
| 4K 合成图 P95 | 170.560 | 152.137 | 257.279 | 低于 2 s 候选线；非复杂照片测量 |
| 清空 500k（单样本） | 429.519 | 403.034 | 426.408 | 验证行和图片归零，不是 P95 |

来源：[第 1 轮](../performance/2026-10-12-acceptance-relwithdebinfo-run1.json)、[第 2 轮](../performance/2026-10-12-acceptance-relwithdebinfo-run2.json)、[第 3 轮](../performance/2026-10-12-acceptance-relwithdebinfo-run3.json)，同名 CSV 为汇总数据。原 Release 三轮与本轮 RelWithDebInfo 选项不同，不能直接用差值认定代码回归。

## Profiler 证据更正

Res.qtd 实际为 106.9456515 秒、31,883 条原始记录，其中 RenderLoop 4,255 条。Skill 解析器只统计有 duration 的 range 等事件，忽略采用 timing1..5 的 SceneGraph；4 个动画采样不是总渲染帧数，333 ms 的动画 FPS 倒数不能判为实际卡顿。

RenderLoop 完成间隔 P95 为 14.0717 ms，最大 29.470 s；没有用户场景标记，长间隔可能含静止/窗口隐藏，不能全部算为卡顿或全部剔除。按大于 100 ms 的间隔分段后最长仅 7.935 秒，未证明连续 60 秒滚动。原 trace 还有 6 次对话框宽度警告和 2 次无障碍角色警告，本轮已修源码；旧 trace 不能证明修后结果。

证据：[SceneGraph 原始摘要](../performance/2026-10-12-res-scenegraph.json)、[更正后的报告](../../profiler/reports/profile-report-ClipboardHistory-2026-10-12-171558.md)。

## 长稳测试

使用独立 500k 合成副本；不复制用户手测新增加的条目，不改主程序的 ClipboardHistory.ini/config.ini。

首次 30 分钟尝试在 300 秒被 QtTest 默认单用例超时终止，属于验收工具超时；未当作应用崩溃。验收工具现根据测试时长加 60 秒余量设置超时，且捕获全局 Qt warning/critical/fatal，不能只靠 QQmlEngine warnings 断言无警告。

相同 DelegateModel::cancel 警告在纯 Window/ListView/原生 QStringListModel 重置基线中复现，模型通知检查器未失败。移除旧行方案虽在最小对照中不报警，实际复杂 delegate 仍有 4 条警告，已回退该核心改动。实际模型关闭屏外缓存后 60 秒和最终 30 分钟均得到 0 Qt/QML warning，现采用 `cacheBuffer: 0` 并保留默认 `reuseItems: false`；这只减少屏外预创建，不加载整个数据库。影响真实 GPU 快滚帧率尚未量化，不能称为性能升级或 Qt 通用根治。

保留失败证据：[首次工具超时](../acceptance/2026-10-12/qml-soak-30min.xml)、[原生重置对照](../acceptance/2026-10-12/qml-minimal-reset-60s.json)、[实际移除行方案仍警告](../acceptance/2026-10-12/qml-model-remove-60s.json)、[关闭屏外缓存的短测](../acceptance/2026-10-12/qml-cache-zero-60s.json)。诊断源码快照随证据归档，日常测试移除临时诊断开关，不隐去全局 Qt warning。

最终生产配置（原模型重置、cacheBuffer=0）的 [60 秒回归](../acceptance/2026-10-12/qml-production-cache-zero-60s.json) 通过，Qt/QML warning=0，6 次录入/读取/删除通过。短测内存和 offscreen 滚动不作为 GPU 帧门禁。

第二次长测在 699 秒主动停止：未捕获警告，但测试驱动每 16 ms 构造 QQmlExpression 来滚动，反复动态编译同一脚本，给 RSS 引入实际 WheelHandler 没有的负担。这次内存增长不能直接归因应用泄漏，也不算长稳通过。[进程采样](../acceptance/2026-10-12/qml-soak-dynamic-expression-aborted.json) 和部分 XML 保留。高频驱动改为直接读写 ListView 原生属性，定位首行采用 invokeMethod，并断言 API 调用成功；[新的 60 秒回归](../acceptance/2026-10-12/qml-native-60s.json) 0 Qt/QML warning，随后重新完整计时。真实 QML delegate、数据模型和存储路径仍参与测试。

### 最终结果

最终原生属性驱动从 2026-10-12 10:50:09 UTC 启动，循环实测 1,800,073 ms，11:20:10 UTC 写出报告，进程退出码 0。XML 无 fail、qwarn、qcritical、qfatal；179 轮录入均被队列接受，读取核对文本或图像尺寸/像素后删除，末尾仍为 500,000 条。包含 30 轮 1080p 和 29 轮 4K 简单合成图。这里的“读取/复制”是服务返回内容，不是操作系统剪贴板或自动粘贴。

| 指标 | 结果 | 计时/范围 |
|---|---:|---|
| 滚动驱动次数 | 59,869 | 修改 ListView contentY，真实 delegate/模型参与；不等同渲染帧数 |
| 有效查询次数 | 895 | 4 个固定查询轮换，无无效计时样本 |
| 最终总记录数 | 500,000 | 校验临时副本，原测试库不变 |
| Qt / QML warning | 0 / 0 | 全局消息捕获 + QQmlEngine warnings |
| 队列排空并停止 | 59.274 ms | 验证 stopped；测试进程随后正常退出 |
| RSS 首值 / 末值 | 67.98 / 82.72 MiB | 1,800 个约 1 Hz 样本，包含初始化后的升温 |
| RSS 采样峰值 | 115.23 MiB | 离散样本最大值，可能漏掉短暂峰值 |
| Windows 进程峰值观测 | 166.79 MiB | OS 高水位；最后外部读取在 1,769.5 秒，未读取退出前最后约 31 秒 |

| 5 分钟分段 | RSS 中位（MiB） | 采样峰值（MiB） |
|---|---:|---:|
| 0–5 分钟 | 79.11 | 110.88 |
| 5–10 分钟 | 81.89 | 113.28 |
| 10–15 分钟 | 82.76 | 115.23 |
| 15–20 分钟 | 83.09 | 114.99 |
| 20–25 分钟 | 83.43 | 113.00 |
| 25–30 分钟 | 83.73 | 114.64 |

该隔离场景内 RSS 低于 300 MB 候选线，后半程中位变化较小，但仍轻微上升；不能由 30 分钟宣称无限期无泄漏，也不将“无持续内存增长”的严格发布项签为通过。真实应用 GPU/剪贴板缓冲不在这个内存口径内。

| 查询 | 样本数 | 到模型完成 P95（ms） | 最大（ms） |
|---|---:|---:|---:|
| benchmark-needle | 224 | 77.799 | 85.485 |
| 无命中查询 | 224 | 431.462 | 448.946 |
| 空过滤条件 | 224 | 22.140 | 23.015 |
| common-token | 223 | 3.998 | 4.770 |

本次长稳的搜索均低于 500 ms 候选线；不能用这次升温后的循环抵消前面独立基准 2/3 超线结果。两套数据分别保留，正式冻结前不签全性能通过。

证据：[最终原始 JSON](../performance/2026-10-12-acceptance-soak-500k.json)、[分段及查询摘要](../performance/2026-10-12-acceptance-soak-summary.json)、[最终 XML](../acceptance/2026-10-12/qml-soak-30min-final-native-scroll.xml)、[外部峰值观测](../acceptance/2026-10-12/soak-process-observations.json)。[构建清单](../acceptance/2026-10-12/build-manifest.json) 的 40 个源码文件和 6 个受测二进制 SHA-256 已在结束后逐一核对一致；没有提交 SHA，不能以文档更新替代 Git 提交记录。

### 复现命令

复用既有 Qt Creator 构建，常规构建与 CTest 命令见 README。长稳需要明确指定本轮独立合成夹具，禁止指定真实历史库；测试会在 QTemporaryDir 中复制后读写，拒绝仍有 WAL 的源库。

```powershell
$taskBuildDir = 'E:\Code\ClipBoardHistoryQT\build\Desktop_Qt_6_11_1_MinGW_64_bit_RelWithDebInfo'
$env:PATH = 'C:\Qt\6.11.1\mingw_64\bin;C:\Qt\Tools\mingw1310_64\bin;' + $env:PATH
$env:CLIPBOARDHISTORY_ACCEPTANCE_SOAK_SECONDS = '1800'
$env:QTEST_FUNCTION_TIMEOUT = '1860000'
$env:CLIPBOARDHISTORY_ACCEPTANCE_SYNTHETIC_DB = "$taskBuildDir\acceptance-results\fixture-500k\history.sqlite3"
$env:CLIPBOARDHISTORY_ACCEPTANCE_REPORT = "$taskBuildDir\acceptance-results\soak-repeat.json"
& "$taskBuildDir\tst_qml_acceptance.exe" optionalSoak -o "$taskBuildDir\acceptance-results\soak-repeat.xml,xml"
```

计时从夹具复制/初始化后的循环开始；每 2 秒切换合成查询，每 10 秒核对一轮录入、读取和删除，含 1080p/4K 简单合成图；RSS 约每秒采样。search_samples 计时截止模型 loading=false，未包含搜索框 debounce 和帧呈现；query_executed=false 样本不计查询分位数。末尾核对总数并验证队列 stopped，不操作实际系统剪贴板或发送 Ctrl+V。

部署只执行了 `windeployqt --dry-run --json --no-translations --qmldir src/ui ClipboardHistory.exe`，依赖计划成功；工具提示缺少 dxcompiler.dll/dxil.dll，并说明通常仅影响 D3D12 特殊特性。未在干净系统实际运行包，不算干净部署通过。RelWithDebInfo 当前开启 QML 调试，正式发行应使用不含调试入口的 Release 构建。

## 发布前未完成项目

- 有场景标记的真实桌面连续 60 秒滚动帧测量，GUI 冷/热启动、真实剪贴板 GUI 回调耗时。
- 真实 Windows 桌面热键冲突、托盘点击、自动粘贴焦点、DPI/系统主题和干净部署验证；本轮替身测试不能替代这些平台行为。
- 自动保留、全库孤儿扫描、开机自启动、Explorer 重启恢复托盘、Linux 后端、正式安装包/升级卸载/CI 尚未实现。
- Ref-01 与正式性能线未冻结；无命中搜索本轮存在超线结果。
- 开源许可证未选定；Git 工作区尚无初始提交，现有 staged/unstaged 内容保留，报告无法提供提交 SHA。

按原开发规范，本版可以继续体验和修复，但不能称为已通过全部发布验收。

### 验收后补充：Windows 开机启动（2026-10-12）

开机启动设置随后已实现：设置页默认关闭，用户启用后注册当前用户 HKCU Run 项；应用配置仍存放在应用数据目录。以 `--startup` 参数登录启动时，托盘可用则进入托盘，否则显示窗口。Debug 与 RelWithDebInfo 构建通过，两个配置的 CTest 均为 11/11 模块通过，QML 开关及配置持久化回归通过。平台注册表用例使用隔离测试键，但当前测试沙箱拒绝 HKCU 写入（`ERROR_ACCESS_DENIED`）而跳过；真实 Windows 登录启动行为尚待桌面验收。此项不改变本报告记录的性能结论。
