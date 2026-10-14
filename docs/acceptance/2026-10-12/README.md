# 验收原始证据索引

结论及计时边界以 [验收记录](../../handoffs/ACCEPTANCE-2026-10-12.md) 为准。本目录保存合成测试结果；不包含用户历史库、配置、图片或完整环境变量。

| 文件 | 含义 |
|---|---|
| ctest-debug.xml / ctest-relwithdebinfo.xml | 最终构建的整批 11 个 CTest 模块结果；默认长稳用例跳过 |
| model-before-fix.xml / model-after-fix.xml | 文件源删除、父目录恢复和超过 512 字符路径列表的修前/首批修后对照 |
| qml-soak-30min.xml | 首次尝试被 QtTest 默认 300 秒超时终止，不能写为应用崩溃或成功长稳 |
| qml-minimal-reset-60s.* | 无应用业务代码的原生 Qt 列表重置对照，复现取消警告 |
| qml-minimal-remove-60s.* | 最小原生列表移除行对照无警告；不代表复杂应用场景修复 |
| qml-model-remove-60s.* | 实际复杂 delegate 移除行后仍有警告，方案已回退 |
| qml-cache-zero-60s.* | 关闭缓存的短测；此时仍使用诊断中的移除行方案 |
| qml-production-cache-zero-60s.* | 最终原 modelReset + cacheBuffer=0 生产配置的 60 秒回归 |
| qml-soak-dynamic-expression-aborted.json / qml-soak-dynamic-expression-partial.xml | 第二次长测主动停止的进程采样与部分 XML；动态表达式驱动污染 RSS，不能当作应用泄漏或成功长稳 |
| qml-native-60s.* | 调整测试驱动后直接读写 ListView 原生属性的短回归；0 Qt/QML warning |
| qml-soak-30min-final-native-scroll.xml | 最终 30 分钟完成、退出码 0、0 Qt/QML warning、500k 最终计数正确 |
| soak-process-observations.json | 独立外部 Windows 工作集/进程峰值观测；最后约 31 秒未取得外部读数 |
| tst_qml_acceptance-diagnostic-snapshot.cpp | 原生对照和缓存/复用诊断开关的源码快照，不参与项目构建；日常测试已精简 |
| build-manifest.json | 受测源码与最终构建文件 SHA-256；工作区暂无初始提交，无法给提交 SHA |

最终 30 分钟 XML 已归档。RSS、搜索、计数等长稳原始样本保存在 [性能证据目录](../../performance/)，测试使用 offscreen/software 渲染，不能据此判真实 GPU 帧率。
