# Qt Quick Profiler report — Res.qtd

## 更正说明

重新读取原始 XML 后确认：采样持续 **106.946 秒**，包含 **31,883 条原始记录**及 **4,255 条 SceneGraph RenderLoop 事件**。原 skill parser 忽略了没有 `duration` 的 SceneGraph 事件。四条动画样本不代表总帧数；原先的 333 ms 卡顿结论、39 ms 时长估算及各表“每帧除以四”统计均已撤回。

本报告分析的是原 `Res.qtd` 捕获时的源码，不是本轮修复后的新采样。详细统计和自检见 [SceneGraph supplementary analysis](/E:/Code/ClipBoardHistoryQT/docs/performance/2026-10-12-res-scenegraph.json)。

## Capture metadata

- Exact profiler configuration is unknown; the trace contains QML ranges, SceneGraph, input, animation, memory, pixmap, and debug-message records.
- Trace: `E:/Code/ClipBoardHistoryQT/build/Desktop_Qt_6_11_1_MinGW_64_bit_RelWithDebInfo/Res.qtd` (3,402,238 bytes).
- Raw bounds: `traceStart=432895400`, `traceEnd=107378546900` ns. Capture duration: **106.9456515 s**, calculated from these bounds.
- Definitions: **1,263**. Raw `profilerDataModel` records: **31,883**. Positive-duration ranges: **5,149**.
- The bundled parser's **8,670** records are a selected subset: positive-duration ranges and selected memory, pixmap, and animation records. Its duration-based branch omits SceneGraph records without `duration`; other excluded records include input and debug messages.
- Range-duration sum: **1,665.46 ms**. This is not wall duration or total CPU time; nested ranges overlap.
- The trace includes four animation samples, an `onClicked: legacyDatabaseDialog.open()` event, and many input/SceneGraph records. It does not label a continuous 60-second scrolling workload.

## Duration-range event summary

| Type | Count | Total time (ms) |
|---|---:|---:|
| Creating | 948 | 1,448.54 |
| Binding | 1,975 | 102.19 |
| Compiling | 29 | 83.69 |
| Javascript | 2,179 | 22.74 |
| HandlingSignal | 18 | 8.30 |

These counts include positive-duration ranges only. Nested totals can overlap. Counts alone are not a performance verdict, and four animation samples do not justify per-frame normalization.

## SceneGraph and animation evidence

Local Qt 6.11.1 `qqmlprofilerdefinitions_p.h` defines the subtypes below. `qquickprofiler_p.h::reportSceneGraphFrame` records a completion timestamp, with timing fields carrying phase deltas or payloads. A missing `duration` does not mean an event was absent.

| SceneGraph subtype | Recorded events |
|---|---:|
| RenderLoop (3) | 4,255 |
| Renderer (0) | 4,254 |
| PolishAndSync (6) | 4,254 |
| AdaptationLayer (1) | 11 |
| **Total** | **12,774** |

The 4,255 RenderLoop events span **106.139 s**. They are profiling events, not proof of 4,255 display presentations. Statistics below describe consecutive **RenderLoop completion intervals**, not frame execution durations or display presentation timing. Percentiles use nearest rank.

| Completion-interval metric | All 4,254 intervals |
|---|---:|
| P50 | 6.9501 ms |
| P95 | 14.0717 ms |
| P99 | 166.5424 ms |
| Maximum | 29,470.4875 ms |
| Intervals over 33.3 ms | 94 |
| Intervals over 100 ms | 50 |

Splitting only at intervals **greater than 100 ms** produces **51 segments**; the longest spans **7.9349769 s** and contains **1,128 events**. Within-segment intervals have P95 **13.9836 ms**, P99 **34.5193 ms**, maximum **97.2757 ms**, and **44 of 4,204** intervals over 33.3 ms.

This segmentation is a timestamp grouping, not a real scrolling marker. Long gaps may include idle/on-demand rendering, hide/minimize, or stalls; the user scenarios are unknown. They cannot automatically be labelled stalls or removed to declare acceptance. The capture lasts over 60 seconds, but no uninterrupted 60-second segment under this rule is present.

The four `animationFrame` records are animation profiling samples. Inverting their reported FPS does not yield measured durations for all rendered frames. **Continuous-scroll frame-time acceptance remains unproven.**

## Warnings in the captured source

The original trace contains **eight debug-message records**:

- Six `Dialog` `implicitWidth` binding-loop warnings: two each at captured `Main.qml` lines **797, 808, and 818**.
- Two `Unable to assign [undefined] to QAccessible::Role` warnings at captured line **595**, corresponding to the old `Accessible.role: Accessible.Text` assignment.

The current acceptance work assigns explicit dialog widths and uses `Accessible.StaticText`. These source fixes do not erase the captured warnings; this is not a post-fix trace. Source line links and excerpts below describe capture-time locations and may differ from current source.

## QML memory summary

The trace records about **821 KiB** of small JavaScript/QML-object allocations and no frees. The calculated reclaimed share is **0%**. Peak live QML GC heap pages are about **886 KiB**; at trace end, the recorded small objects and heap pages are still live. This capture does not measure the process's total RSS, C++ model/database memory, scene graph memory, or a long-term memory trend.

| Category | Allocations | Total allocated | Reclaimed | Peak live | Live at exit |
|---|---:|---:|---:|---:|---:|
| GC heap pages — pages managed by the QML JavaScript heap | 5 | 885.5 KiB | 0 B | 885.5 KiB | 885.5 KiB |
| Small JS objects — per-object allocations in the QML JavaScript heap | 3,497 | 820.7 KiB | 0 B | 820.7 KiB | 820.7 KiB |

Large JS objects: no allocations recorded. The reported peak is the running sum of live heap pages, not the largest individual allocation.

## Pixmap cache

| Load requests | Loaded | Failed | Unaccounted | Cache size changes |
|---:|---:|---:|---:|---:|
| 2 | 2 | 0 | 0 | 4 |

All two requests completed successfully. Cache size changes count insertions and removals together; they do not distinguish evictions. No failed or cancelled image loads were recorded.

| Image | Dimensions | Pixels |
|---|---:|---:|
| `double-arrow.png` | 40 × 28 | 1,120 |
| `check.png` | 28 × 28 | 784 |
| **Total** |  | **1,904** |

## Top 30 hotspots

Times are captured range durations. Nested ranges can overlap. Qt-internal paths are shown as recorded; the empty bytecode location cannot be mapped to a project file.

| Rank | Total (ms) | Count | Avg (ms) | Type | Source location | Details |
|---:|---:|---:|---:|---|---|---|
| 1 | 343.34 | 48 | 7.153 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Button.qml:9` | `QtQuick.Templates/Button` |
| 2 | 340.84 | 34 | 10.025 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Button.qml:24` | `QtQuick.Controls.impl/IconLabel` |
| 3 | 290.13 | 3 | 96.710 | Creating | [source unresolved] | `<字节码>`; no filename or details |
| 4 | 260.94 | 2 | 130.469 | Creating | [Main.qml:994](../../src/ui/Main.qml#L994) | `QtQuick.Layouts/RowLayout` |
| 5 | 83.34 | 1 | 83.337 | Binding | [Main.qml:823](../../src/ui/Main.qml#L823) | `standardButtons: Dialog.Yes \| Dialog.No` |
| 6 | 58.81 | 2 | 29.405 | Compiling | [Main.qml](../../src/ui/Main.qml) | `Main.qml` |
| 7 | 27.90 | 1 | 27.898 | Creating | [Main.qml:7](../../src/ui/Main.qml#L7) | `ApplicationWindow` |
| 8 | 15.45 | 4 | 3.864 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/CheckBox.qml:9` | `QtQuick.Templates/CheckBox` |
| 9 | 15.28 | 4 | 3.821 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/CheckBox.qml:41` | `QtQuick.Controls.impl/ColorImage` |
| 10 | 14.39 | 2 | 7.193 | Creating | [Main.qml:249](../../src/ui/Main.qml#L249) | `QtQuick/Rectangle` |
| 11 | 13.99 | 32 | 0.437 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ToolButton.qml:9` | `QtQuick.Templates/ToolButton` |
| 12 | 10.98 | 2 | 5.488 | Creating | [Main.qml:243](../../src/ui/Main.qml#L243) | `QtQuick.Layouts/ColumnLayout` |
| 13 | 10.23 | 2 | 5.114 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ApplicationWindow.qml:9` | `QtQuick.Templates/ApplicationWindow` |
| 14 | 9.86 | 4 | 2.465 | Creating | [Main.qml:670](../../src/ui/Main.qml#L670) | `QtQuick/Text` |
| 15 | 7.92 | 24 | 0.330 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ToolButton.qml:23` | `QtQuick.Controls.impl/IconLabel` |
| 16 | 7.05 | 2 | 3.523 | Creating | [Main.qml:257](../../src/ui/Main.qml#L257) | `QtQuick.Layouts/RowLayout` |
| 17 | 6.41 | 4 | 1.602 | Creating | [Main.qml:573](../../src/ui/Main.qml#L573) | `QtQuick/Text` |
| 18 | 6.32 | 1 | 6.318 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Calendar.qml:0` | `Calendar.qml` |
| 19 | 6.11 | 1 | 6.109 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/ComboBox.qml:0` | `ComboBox.qml` |
| 20 | 5.93 | 2 | 2.966 | Creating | [Main.qml:310](../../src/ui/Main.qml#L310) | `QtQuick/Rectangle` |
| 21 | 5.76 | 12 | 0.480 | Creating | [Main.qml:277](../../src/ui/Main.qml#L277) | `QtQuick/Text` |
| 22 | 4.29 | 1 | 4.291 | HandlingSignal | [Main.qml:1055](../../src/ui/Main.qml#L1055) | `onClicked: legacyDatabaseDialog.open()` |
| 23 | 4.29 | 1 | 4.288 | Javascript | [Main.qml:1055](../../src/ui/Main.qml#L1055) | `expression for onClicked` |
| 24 | 3.97 | 2 | 1.985 | Creating | [Main.qml:316](../../src/ui/Main.qml#L316) | `QtQuick/Text` |
| 25 | 3.10 | 2 | 1.551 | Creating | [Main.qml:308](../../src/ui/Main.qml#L308) | `QtQuick.Layouts/RowLayout` |
| 26 | 2.87 | 1 | 2.867 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Dialog.qml:0` | `Dialog.qml` |
| 27 | 2.87 | 8 | 0.358 | Creating | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/Dialog.qml:9` | `QtQuick.Templates/Dialog` |
| 28 | 2.41 | 2 | 1.205 | Creating | [Main.qml:894](../../src/ui/Main.qml#L894) | `QtQuick/Item` |
| 29 | 2.33 | 1 | 2.330 | Creating | [Main.qml:901](../../src/ui/Main.qml#L901) | `ScrollView` |
| 30 | 2.29 | 1 | 2.293 | Compiling | `qrc:/qt-project.org/imports/QtQuick/Controls/Basic/impl/TextEditingContextMenu.qml:0` | `TextEditingContextMenu.qml` |

## Detailed analysis of project hotspots

### [Main.qml:994](../../src/ui/Main.qml#L994) — settings theme controls are built eagerly

```qml
Item {
    id: settingsPage
    visible: window.settingsPageVisible
    ScrollView {
        ColumnLayout {
            // settings cards
            RowLayout { // line 994: theme choices
                Repeater {
                    model: ["暗色", "浅色", "跟随系统", "蓝色"]
                    delegate: Button { ... }
                }
            }
        }
    }
}
```

This is the largest project-owned `Creating` range: 260.94 ms across two events. `settingsPage` is a child of the main window and only changes `visible`; QML still creates its controls at startup. The four theme buttons also construct Qt Quick Controls internals, matching the large Qt `Button` and `IconLabel` creation hotspots. If cold startup matters, load the settings component through a `Loader` only when the user opens Settings. This trace does not show repeated settings creation during list scrolling.

### [Main.qml:823](../../src/ui/Main.qml#L823) — import dialog button setup

```qml
Dialog {
    id: importDialog
    title: "导入旧版历史？"
    standardButtons: Dialog.Yes | Dialog.No
    onAccepted: historyModel.importLegacyDatabase(window.legacyDatabasePath)
}
```

The one `Binding` range takes 83.34 ms. The expression is a constant enum combination; this is not evidence of an expensive JavaScript calculation or a binding that reevaluates during scrolling. Creating the dialog's standard buttons and their Controls internals may be included in this startup range. If startup is a target, defer rarely used dialogs with a loader; do not rewrite this constant expression based on this sample alone.

### [Main.qml](../../src/ui/Main.qml) — Main.qml compilation

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
import QtQuick.Dialogs

ApplicationWindow {
```

Two `Compiling` ranges total 58.81 ms. The trace does not identify a source line or show whether these ranges are first-use compilation versus another compilation phase. The project declares `Main.qml` in `qt_add_qml_module`, and the build has QML cache generation. Confirm the packaged binary uses the expected QML cache artifacts before treating this as a release startup issue. Splitting the file is not justified by this trace alone.

### [Main.qml:7](../../src/ui/Main.qml#L7) — root window construction

```qml
ApplicationWindow {
    id: window
    width: 720
    height: 620
    minimumWidth: 620
    minimumHeight: 460
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
```

One root `ApplicationWindow` creation takes 27.90 ms. This is a one-time root construction range and is expected; the trace does not identify a costly expression or recurring work here. Keep it as context unless a longer cold-start trace shows it repeatedly dominating startup.

### [Main.qml:249](../../src/ui/Main.qml#L249) — main category bar

```qml
ColumnLayout {
    id: historyPage
    visible: !window.settingsPageVisible
    Rectangle {
        id: categoryBar
        Layout.fillWidth: true
        Layout.preferredHeight: 40
        RowLayout {
            Repeater { model: ["全部", "文本", "链接", "文件", "图片", "收藏"] }
```

The category bar's `Rectangle` creation totals 14.39 ms across two events. It owns the six primary filters, so this looks like normal initial UI construction. The trace does not show this row rebuilding during interaction. The more promising cold-start change is to defer the secondary Settings tree, while leaving this profile's main navigation intact.

## Next steps

1. Record a labelled **60-second continuous scroll** workload with the 500,000-record database and known start/end markers and window visibility. Use suitable render/presentation timing tooling, identifying RenderLoop completion intervals as a separate metric. Give search and expansion their own labelled windows.
2. Re-profile a fresh launch after the dialog-width and accessibility-role fixes to verify that their warnings disappear. If cold startup remains a concern, test lazy Settings creation; existing hotspot ranges suggest a candidate but do not quantify its user-visible benefit.
3. Measure process RSS separately during a multi-minute scroll/search session and assess long-term stability. This 106.946-second trace records the QML JavaScript heap, not total process memory.

The project hotspots are concentrated in `src/ui/Main.qml`; these specific locations provide a starting point for a broader structural review.

> AI assistance has been used to create this output.
