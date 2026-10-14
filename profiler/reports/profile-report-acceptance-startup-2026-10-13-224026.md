# QML 首屏创建热点记录（2026-10-13）

## 采样范围

逻辑模式：javascript/binding/handlingsignal/compiling/creating。Trace 为 [qmlprofiler-trace-acceptance-startup-2026-10-13-224026.qtd](../traces/qmlprofiler-trace-acceptance-startup-2026-10-13-224026.qtd)，持续 **4.474 秒**。使用 Qt 6.11.1 / MinGW / Release + QT_QML_DEBUG 的隔离测试宿主，D3D11 硬件设备；10k 合成副本，首次创建界面后交替执行 60 个查询，测试自动正常退出。

Profiler 退出码 0，被测测试为 3 passed / 0 failed，Qt/QML warning=0。调试/采样开销使本 trace 的绝对时间不作为无 Profiler 性能基线。没有采集 SceneGraph、内存或 PixmapCache；不能由逻辑范围推出 FPS、进程 RSS、图片缓存命中率或 GPU 执行时间。

解析得到 82,200 个范围事件。Creating 21,864 次 / 1,538.41 ms；Binding 27,603 次 / 163.85 ms；Javascript 32,697 次 / 44.01 ms；Compiling 31 次 / 43.72 ms；HandlingSignal 5 次 / 2.24 ms。合计 1,792.22 ms 含父子嵌套、可能重叠，**不是 CPU 总时间或墙钟耗时**。创建次数包括后续查询造成的 delegate 重建，不能全部称为首屏创建次数。

## 最大五个聚合区间

| 位置 | 类型/次数 | 合计（ms） | 解释及下一步 |
|---|---|---:|---|
| [source unresolved]；filename 空、line 0、details 空 | Creating / 3 | 406.68 | Trace 没有足够来源信息，不能把它分配给某个窗口或函数；保留未知热点，必要时用 CPU 栈采样定位。 |
| [Main.qml:547](../../src/ui/Main.qml#L547)；QtQuick/Rectangle | Creating / 10 | 382.72 | 位于 SettingsSection 的图标容器。一次首次创建是 382.5718 ms，其余九次很小；区间可包含子控件/字体首次初始化和等待，未证明 Rectangle 绘制本身昂贵。设置页默认隐藏却立即实例化；优先通过 Loader 延后整页创建，然后测首次打开页面。 |
| Qt 内部 Basic/Button.qml:9；QtQuick.Templates/Button | Creating / 58 | 91.09 | Qt 按钮的创建成本，不能映射为用户源码的同名文件。结合隐藏设置页和预创建弹窗，减少启动时不需要的按钮实例；不修改 Qt 控件内部实现。 |
| [Main.qml:1668](../../src/ui/Main.qml#L1668) | Binding / 1 | 87.98 | 导入确认框的 standardButtons = Dialog.Yes \| Dialog.No；创建标准按钮内部对象可能计入该赋值。对应 JS 只有约 0.0932 ms，不能把枚举常量改写当作优化。按需创建未打开的确认框，并验证第一次打开和关闭。 |
| Qt 内部 Basic/Button.qml:24；QtQuick.Controls.impl/IconLabel | Creating / 12 | 86.55 | Qt 按钮文字/图标内容的首次实例化，可能与父按钮范围重叠。延后非首屏组件，再复测；不将它与上行直接相加估计节省。 |

来源上下文：

```qml
// Main.qml:547，SettingsSection 内
Rectangle {
    Layout.preferredWidth: 30
    Layout.preferredHeight: 30
    radius: 9
    color: Qt.rgba(settingsSection.iconColor.r, settingsSection.iconColor.g,
                   settingsSection.iconColor.b, 0.14)
    Text {
        anchors.centerIn: parent
        text: settingsSection.iconText
        color: settingsSection.iconColor
        font.pixelSize: 15
        font.weight: Font.DemiBold
    }
}
```

```qml
// Main.qml:1662
Dialog {
    id: importDialog
    width: Math.min(window.width - 48, 480)
    anchors.centerIn: parent
    modal: true
    title: "导入旧版历史？"
    standardButtons: Dialog.Yes | Dialog.No
    onAccepted: historyModel.importLegacyDatabase(window.legacyDatabasePath)
}
```

[settingsPage](../../src/ui/Main.qml#L1747) 是根窗口的直接 Item 子节点，通过 `visible: window.settingsPageVisible` 控制显示。visible=false 不阻止创建其子对象。这是本次最可操作的首屏优化方向；具体容器内部 382 ms 的 CPU 原因仍需栈信息才能确定，不宣称已证明字体扫描或磁盘 I/O 是根因。

## 后续验证

用 Loader 延迟设置页/少用对话框时保留独立的配置加载、全局热键和自启动注册；不要把必须运行的初始化一起延迟。随后对首屏与第一次打开设置分别计时，复测设置保存、主题导入、快捷键录制和关闭对话框。

当前 trace 中 JS 合计约 44 ms、compiling 约 44 ms；没有足够证据支持整体重写 JavaScript 或改数据层。原生滚动代理指标见 [Release 补测](../../docs/handoffs/PERFORMANCE-RELEASE-2026-10-13.md)，本 trace 没有渲染事件，不能承担滚动帧门槛签核。
