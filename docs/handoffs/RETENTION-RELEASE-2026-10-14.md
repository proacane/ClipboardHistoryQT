# 定时保留与 Release 试用包交接（2026-10-14）

本任务于 2026-10-13 开始、2026-10-14 完成，原测试证据目录保留起始日期。功能和隔离自动测试完成，提供 Windows 10 x64 / Qt 6.11.1 / MinGW 13.1 的 Release 便携试用包；不代表全部发布及性能门槛通过。

## 使用和规则

设置 → 定时删除：勾选启用，选择时长和小时/天，点击“应用保留设置”。未点击应用的草稿不影响后台。默认关闭（`Appearance/retentionHours=0`），不自动删除；范围 1–87600 小时，最多 3650 天。关闭后永久保留后续记录，不恢复已经删除的数据。

- 以数据库已有的最近采集 UTC 时间计算。同内容再次采集沿用原有去重机制并更新该时间，因此续期；直接读取/展开记录不续期。
- 仅删除 `favorite=0 AND created_at_ms_utc < cutoff`，恰好等于截止时间仍保留，收藏不受该策略清理。取消收藏的过期记录在后续检查时可以删除。
- 启动时恢复已保存策略，启用或更改期限后检查；运行期间每分钟检查。保存相同期限不重新调度，继续现有定时检查；程序退出期间不执行任务，下次启动检查积累的过期记录。
- 后台存储线程每次事务最多删除 500 条，批次之间让采集、查询、收藏和配置消息执行。关闭策略在当前事务完成后阻止后续批次；退出先停定时器，按既有队列流程排空并关闭数据库。
- 事务提交成功后，只清理已无任何历史引用的应用图片，收藏/新记录共享的图片保留。文件类型仅删除历史引用，原始文件永不删除。
- SQL 失败回滚该批次，下一分钟重试。文件清理失败不恢复已经提交的删除，向界面报告未清理图片；全库孤儿重试扫描仍未实现。
- 增加可重复创建的 `(favorite, created_at_ms_utc, id)` 和 `image_ref` 索引，保持 schema v5、记录字段和迁移协议兼容。旧大数据库首次升级建索引可能额外耗时；后续启动不会重建。

定时器和 SQL 留在 HistoryWorker/Service/Repository。HistoryTaskQueue 缓存数据库初始化前读到的策略；HistoryModel 通过属性接入 QML 并刷新列表及总数；QtCore.Settings 持久化策略，不要求先打开设置页才启动。

## 界面设计约定

沿用现有设置分区、语义颜色和主题：新增“定时删除”分区，主操作为“应用保留设置”；默认永久保留、编辑未应用、启用后当前期限、关闭、删除成功和失败状态有文字说明。时长和单位在关闭时不可用；控件可通过键盘操作，有 Accessible.name。720px 宽度下检查换行和控件排布。[界面截图](../acceptance/2026-10-13-retention/retention-settings-final.png)。

## 自动测试与证据

证据根目录：[2026-10-13-retention](../acceptance/2026-10-13-retention/)。

| 检查 | 结果 | 证据 |
|---|---|---|
| Release 主程序及全部测试构建 | 成功，无 `QT_QML_DEBUG`，`-O3 -DNDEBUG` | 当前主 Release 构建目录 |
| 完整 CTest | 11/11 通过 | [最终日志](../acceptance/2026-10-13-retention/ctest-final-complete.txt) |
| Repository | 18 通过、0 失败 | [日志](../acceptance/2026-10-13-retention/history_repository.txt) |
| Service | 10 通过、0 失败 | [日志](../acceptance/2026-10-13-retention/history_service.txt) |
| Queue/Worker | 8 通过、0 失败 | [日志](../acceptance/2026-10-13-retention/history_task_queue.txt) |
| Model | 13 通过、0 失败 | [日志](../acceptance/2026-10-13-retention/history_model.txt) |
| 编译 QML 原生回归 | 19 通过、0 失败、2 个可选长场景跳过 | [原生日志](../acceptance/2026-10-13-retention/qml-native.txt) |
| 仅使用包内运行库的最终原生回归 | 19 通过、0 失败、2 个可选长场景跳过 | [最终包运行库日志](../acceptance/2026-10-13-retention/package-runtime-final.txt) |
| windeployqt | 返回 0，部署 Release 运行库、QML 插件和 MinGW 运行库 | [部署日志](../acceptance/2026-10-13-retention/deploy.txt) |

新增测试覆盖：默认永久；初始化前策略恢复；501 条过期记录分两批；恰好到期/未来记录；收藏保护；同内容重新采集续期；共享图片及唯一图片清理；文件源不删除；清理失败报告；SQL 删除触发器注入失败后的回滚；线程归属及无效参数；真实定时器 timeout 路径重复执行和关闭；行/总记录数更新及合法模型通知；小时/天转换、应用前草稿、关闭、偏好持久化及 QML 引擎重启恢复。定时器重复测试直接触发其 timeout 信号，验证实际连接路径及 60000 ms 设置，没有人为等待每个一分钟周期。

首次模型回归发现自动删除后总数未失效，已修复为重新读取全库总数；失败日志 `ctest-first.txt`、`model-first.txt` 保留，不能用来描述最终版本。

所有历史、配置、图片和文件源都是隔离的合成夹具。运行库验证复制包依赖到独立宿主，再添加仅测试所需的 Qt6Test.dll 和测试 EXE；PATH 中移除 Qt SDK，使用编译 QML，运行完成退出 0。没有启动会监听真实剪贴板的生产程序。验证证明运行库部署和相同界面/数据逻辑可执行，不替代生产主程序在干净虚拟机或真实托盘/剪贴板中的验收。

部署工具提示缺少 dxcompiler.dll / dxil.dll；当前 Windows 10 D3D11 回归通过，尚未验证 D3D12 特殊功能。新自动保留策略未重跑 500k 完整性能线或数小时长稳；上一轮性能基线绑定旧源码，不能直接代表本版本新增索引和清理负载。

## 发布内容与复现

包目录：`dist/ClipboardHistory-Windows10-x64-Release-20261014-retention`。

ZIP：`dist/ClipboardHistory-Windows10-x64-Release-20261014-retention.zip`。

内含主 EXE、Qt/MinGW 运行库、QML/平台/SQLite 插件、基础自定义主题、中文试用说明和 SHA-256 清单。不含 data、ClipboardHistory.ini、用户历史、合成夹具、测试 EXE、Qt6Test.dll、构建目录或调试入口。完整解压到可写位置再启动；首次启动的数据和配置由程序创建。旧发布目录不覆盖。

`release-manifest.json` 保存构建信息、Git 源码基线、源码/测试哈希及包内文件哈希；ZIP 的校验信息另存证据目录。源码基线包含已验证的现有应用与此次定时保留，发布包和原始 Profiler 采样不进入 Git。

## 优化对话交接

已创建“剪贴板历史：后续性能优化”，threadId：`01a135ea-0968-7100-841a-6fedca67f604`。该对话先只阅读现有交接，不与本任务同时改文件/构建。用户在新对话发起下一轮工作时，先读本交接、development-spec 和 Git 状态，再开展优化。

优先补正式主程序端到端启动、500k 无结果搜索、复杂图片密集滚动、GPU Present、真实采集/托盘和数小时后台资源；把新索引首次建立、大规模分批清理和清理期间搜索响应加入测量。保持合成隔离数据及最小改动，不把本轮功能通过当成完整性能通过。
