# ClipboardHistory QML Profiler 报告

## 采集信息

- Profile mode：full
- Trace：`build/Perf-QmlProfiler/profiler/traces/qmlprofiler-trace-ClipboardHistory-manual.qtd`（1,545,605 bytes）
- Parser 估算运行时长：约 16 ms；本次只有 1 帧，无法代表实际进程运行时长。
- Range event durations 合计：1,674.46 ms。该值是捕获到的 range 时长之和，**不是墙钟时间，也不是 CPU 总时间**；range 可以嵌套，因此 signal 中的 JS 会同时计入两层。
- Total events：14,777
- 用户确认采集时没有操作界面；因此这份 trace 没有覆盖滚动、分页或搜索交互。

## 事件类型

| Type | Count | Total ms | ms/frame |
|---|---:|---:|---:|
| Creating | 2,261 | 1,402.93 | 1,402.93 |
| Binding | 3,233 | 225.07 | 225.07 |
| Compiling | 29 | 36.10 | 36.10 |
| Javascript | 3,606 | 9.43 | 9.43 |
| HandlingSignal | 14 | 0.92 | 0.92 |

每类事件的 `count` 会随采集时长和操作变化；它不是主要性能指标。此处每帧数值直接等于总时间，是因为 trace 仅有 1 帧，不可用于推断持续帧负载。

## 帧时间

**帧时间**是相邻帧之间的墙钟间隔，越低越流畅。P50 是中位数；P95/P99 表示最慢的 5%/1% 帧所在时间；max 是单帧最差值。60 Hz 屏幕参考约 16.67 ms/帧，超过 33 ms 通常可见卡顿，超过 50 ms 表示严重停顿。

| 指标 | 本次结果 |
|---|---:|
| Frame count | 1 |
| **P50** | **16.13 ms** |
| **P95** | **16.13 ms** |
| **P99** | **16.13 ms** |
| **Max** | **16.13 ms** |
| **Frames over 25 ms / 33 ms / 50 ms** | **0 / 0 / 0** |
| Distinct frame times | 1 |

单帧样本不足以评价流畅度；零卡顿帧只表示这个样本没有超过阈值。

## QML 内存

这次 trace 记录了 5,589 个小型 JS/QML 对象分配，共 985,504 bytes（约 0.94 MiB）；记录到的回收量为 0%，GC heap page 峰值约 1.05 MiB。采集结束时这些已计数对象仍显示为 live。由于采集没有界面交互且只有 1 帧，这不能说明存在内存泄漏。

| Category | Allocations | Total allocated | Reclaimed | Peak live | Live at exit |
|---|---:|---:|---:|---:|---:|
| GC heap pages（GC 分配器管理的堆页） | 8 | 1.05 MiB | 0 B | 1.05 MiB | 1.05 MiB |
| Small JS objects（单个小型 JS/QML 对象） | 5,589 | 962.41 KiB | 0 B | 962.41 KiB | 962.41 KiB |

没有记录到 Large JS objects 分配。

## 图片缓存

| Load requests | Loaded | Failed | Unaccounted | Cache size changes |
|---:|---:|---:|---:|---:|
| 6 | 4 | 2 | 0 | 10 |

4 个已加载 pixmap 合计 551,024 pixels；最大的两张为 704×396（278,784 pixels）和 704×384（270,336 pixels），其余是 Qt Basic 样式的小图标。缓存变化数只计插入和移除次数，不能据此判断淘汰或抖动。

## 图片加载失败或取消

Trace 有 2 次失败/取消事件，覆盖 2 个 URL，占 6 次加载请求的 33.3%。两个对应的合成图片文件目前都存在。没有采集应用 stderr，因此无法区分加载失败和列表刷新期间取消的加载；不能据此认定为图片缺陷。

| URL（合成测试数据） | Count |
|---|---:|
| `.../images/7b6c13ddfe75ea6937974c1b2798538737a6c29dbf96a7e7a1f2b551b6b6ee58.png` | 1 |
| `.../images/e4b2ee10d50a9960f060175c3cd26ef92ed46f947e6c8dab4cf2419f31745715.png` | 1 |

## Top 30 hotspots

`ms/frame` 因只有 1 帧而与 `total ms` 相同。Qt 内部源码保持为 trace 中的路径；`Unknown` 没有来源文件和组件详情。

| # | Total ms | Count | Avg ms | ms/frame | Type | Source | Details |
|---:|---:|---:|---:|---:|---|---|---|
| 1 | 418.57 | 48 | 8.720 | 418.57 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Button.qml:9` | `QtQuick.Templates/Button` |
| 2 | 416.64 | 34 | 12.254 | 416.64 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Button.qml:24` | `QtQuick.Controls.impl/IconLabel` |
| 3 | 264.15 | 3 | 88.050 | 264.15 | Creating | [source unresolved] | 空 |
| 4 | 211.89 | 1 | 211.892 | 211.89 | Binding | [Main.qml:823](../../src/ui/Main.qml#L823) | 空 |
| 5 | 206.35 | 2 | 103.174 | 206.35 | Creating | [Main.qml:994](../../src/ui/Main.qml#L994) | `QtQuick.Layouts/RowLayout` |
| 6 | 22.55 | 2 | 11.275 | 22.55 | Compiling | [Main.qml](../../src/ui/Main.qml) | `qrc:/qt/qml/ClipboardHistory/src/ui/Main.qml` |
| 7 | 12.82 | 128 | 0.100 | 12.82 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ToolButton.qml:9` | `QtQuick.Templates/ToolButton` |
| 8 | 7.25 | 88 | 0.082 | 7.25 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ToolButton.qml:23` | `QtQuick.Controls.impl/IconLabel` |
| 9 | 6.68 | 20 | 0.334 | 6.68 | Creating | [Main.qml:670](../../src/ui/Main.qml#L670) | `QtQuick/Text` |
| 10 | 5.91 | 2 | 2.953 | 5.91 | Creating | [Main.qml:249](../../src/ui/Main.qml#L249) | `QtQuick/Rectangle` |
| 11 | 5.34 | 20 | 0.267 | 5.34 | Creating | [Main.qml:437](../../src/ui/Main.qml#L437) | `QtQuick.Layouts/ColumnLayout` |
| 12 | 4.83 | 2 | 2.413 | 4.83 | Creating | [Main.qml:310](../../src/ui/Main.qml#L310) | `QtQuick/Rectangle` |
| 13 | 4.59 | 21 | 0.219 | 4.59 | Creating | [Main.qml:407](../../src/ui/Main.qml#L407) | `QtQuick/Rectangle` |
| 14 | 4.46 | 12 | 0.371 | 4.46 | Creating | [Main.qml:277](../../src/ui/Main.qml#L277) | `QtQuick/Text` |
| 15 | 4.04 | 20 | 0.202 | 4.04 | Creating | [Main.qml:573](../../src/ui/Main.qml#L573) | `QtQuick/Text` |
| 16 | 3.26 | 1 | 3.259 | 3.26 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Calendar.qml:0` | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Calendar.qml` |
| 17 | 3.21 | 1 | 3.213 | 3.21 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/TextField.qml:0` | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/TextField.qml` |
| 18 | 3.00 | 4 | 0.749 | 3.00 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/CheckBox.qml:9` | `QtQuick.Templates/CheckBox` |
| 19 | 2.91 | 4 | 0.726 | 2.91 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/CheckBox.qml:41` | `QtQuick.Controls.impl/ColorImage` |
| 20 | 2.50 | 1 | 2.505 | 2.50 | Creating | [Main.qml:7](../../src/ui/Main.qml#L7) | `ApplicationWindow` |
| 21 | 2.05 | 20 | 0.103 | 2.05 | Creating | [Main.qml:443](../../src/ui/Main.qml#L443) | `QtQuick.Layouts/RowLayout` |
| 22 | 1.55 | 1 | 1.553 | 1.55 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/impl/TextEditingContextMenu.qml:0` | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/impl/TextEditingContextMenu.qml` |
| 23 | 1.41 | 1 | 1.411 | 1.41 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ScrollView.qml:0` | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ScrollView.qml` |
| 24 | 1.37 | 46 | 0.030 | 1.37 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ScrollBar.qml:9` | `QtQuick.Templates/ScrollBar` |
| 25 | 1.20 | 20 | 0.060 | 1.20 | Creating | [Main.qml:563](../../src/ui/Main.qml#L563) | `QtQuick.Layouts/RowLayout` |
| 26 | 1.16 | 20 | 0.058 | 1.16 | Creating | [Main.qml:627](../../src/ui/Main.qml#L627) | `QtQuick.Layouts/RowLayout` |
| 27 | 1.07 | 8 | 0.134 | 1.07 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/DialogButtonBox.qml:8` | `QtQuick.Templates/DialogButtonBox` |
| 28 | 1.03 | 2 | 0.513 | 1.03 | Creating | [Main.qml:316](../../src/ui/Main.qml#L316) | `QtQuick/Text` |
| 29 | 0.94 | 2 | 0.469 | 0.94 | Creating | [Main.qml:917](../../src/ui/Main.qml#L917) | `QtQuick.Layouts/RowLayout` |
| 30 | 0.82 | 22 | 0.037 | 0.82 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ScrollView.qml:9` | `QtQuick.Templates/ScrollView` |

## 重点分析

### Qt Quick Controls Basic 的 Button / IconLabel 创建

前两项在 Qt Basic 样式内部，分别为 418.57 ms 和 416.64 ms；这类事件主要反映按钮及其图标/文本内容首次创建。界面中的对话框使用 `standardButtons`，会创建 Qt 提供的标准按钮。它们属于一次性创建成本，不是每次滚动都执行的路径。本次 trace 没有足够样本判断是否值得延迟构建设置页或对话框。

### 未知组件创建（source unresolved）

3 次创建合计 264.15 ms，但 trace 的 filename、line 和 details 均为空。用户确认采集时没有操作界面，因此当前只能记录为启动期间的未知组件，不能映射到项目源代码或推断具体成因。

### [Main.qml:823](../../src/ui/Main.qml#L823)

```qml
Dialog {
    id: importDialog
    modal: true
    title: "导入旧版历史？"
    standardButtons: Dialog.Yes | Dialog.No
    onAccepted: historyModel.importLegacyDatabase(window.legacyDatabasePath)
}
```

单个 Binding range 记为 211.89 ms，位置是导入对话框的 `standardButtons`。这可能包含首次实例化 Qt 标准按钮的延迟；其附近的 Qt 内部 Button/`IconLabel` 创建也较重。它只出现一次且采集只有 1 帧，因此先把它视为冷启动热点候选，不把它当作持续运行卡顿。若用户可感知启动慢，再针对启动过程重复采集，并考虑把未打开的对话框延迟创建。

### [Main.qml:994](../../src/ui/Main.qml#L994)

```qml
RowLayout {
    Layout.fillWidth: true
    spacing: 6
    Repeater {
        model: ["暗色", "浅色", "跟随系统", "蓝色"]
```

2 个 RowLayout 创建事件合计 206.35 ms，落在设置页主题选项附近。可能是 QML 组件首次加载成本；trace 没有用户交互，也没有足够样本确认是否与滚动性能有关。若启动耗时是目标，应把设置页的首次显示作为独立场景采集，再评估是否延迟加载。

## 后续步骤

1. 当前 trace 只有 1 帧且没有界面操作，不能判断长列表滚动时的帧时间，也没有测到搜索过程。需要一份包含持续滚动、跨页加载和输入搜索的交互 trace，才可判断 QML 渲染是否有卡顿。
2. 性能基准报告中的 500k 无命中搜索 P95 为约 428 ms，接近候选 500 ms 门槛；用户当前认为搜索速度可接受，先保留当前查询实现，后续只在阈值超限或体验恶化时再评估索引/FTS。
3. 若应用冷启动也显得慢，单独采集启动场景并复核隐藏设置页和未打开对话框的组件创建成本；当前单次 trace 不足以支持结构性改动。
4. 完成剪贴板入库/图片场景、内存趋势和长时间稳定性测量后，更新完整性能门禁结论。

## 限制

- 未获得 profiler 进程退出码或应用 stderr；trace 本身存在且解析成功。
- 仅 1 个 frame event；帧统计不是滚动性能结论。
- 该 trace 的 QML 事件只反映 UI 层，不能代替 SQLite 搜索/写入基准。
- 图片失败/取消无法用本次采集确认；两个对应测试文件存在。

> AI assistance has been used to create this output.
