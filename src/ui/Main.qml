import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtCore
import QtQuick.Dialogs
import "ThemePalette.js" as ThemePalette

ApplicationWindow {
    id: window

    width: 920
    height: 680
    minimumWidth: resolutionLocked ? lockedWindowWidth : minimumWindowWidth
    maximumWidth: resolutionLocked ? lockedWindowWidth : maximumAllowedWindowWidth
    minimumHeight: resolutionLocked ? lockedWindowHeight : minimumWindowHeight
    maximumHeight: resolutionLocked ? lockedWindowHeight : maximumAllowedWindowHeight
    visible: !(Qt.platform.os === "windows" && Qt.application.arguments.indexOf("--startup") !== -1)
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "剪切板历史"
    color: page
    palette.window: window.page
    palette.windowText: window.textMain
    palette.base: window.inputSurface
    palette.alternateBase: window.rowSurface
    palette.text: window.textMain
    palette.button: window.tileSurface
    palette.buttonText: window.textMain
    palette.highlight: window.accent
    palette.highlightedText: window.onAccent
    palette.mid: window.line
    palette.placeholderText: window.textMuted
    onClosing: function(close) {
        if (Qt.platform.os === "windows") {
            close.accepted = false
            window.requestClose()
        }
    }

    property string themeMode: "暗色"
    property var customThemePalette: ThemePalette.custom
    property string customThemeName: "自定义主题"
    property var importedThemes: []
    property var pendingImportedTheme: null
    property string importedThemesJson: "[]"
    property string selectedImportedThemeId: ""
    property string themeImportMessage: ""
    property string storageError: ""
    property bool storageErrorIsFailure: false
    property string pendingStorageDirectory: appPaths.currentDataDirectory
    property bool settingsPageVisible: false
    property bool pendingHotkeySettings: false
    onSettingsPageVisibleChanged: {
        if (settingsPageVisible) settingsPageLoader.active = true
        else {
            window.pendingHotkeySettings = false
            window.cancelHotkeyCapture()
        }
    }
    property string historyMessage: ""
    property string selectedCategory: "全部"
    property string hotkeyDraft: "Ctrl+Shift+V"
    property bool hotkeyCaptureActive: false
    property string legacyDatabasePath: ""
    property var expandedEntryId: 0
    property string expandedEntryText: ""
    property bool expandedEntryLoadFailed: false
    property int windowSizeWidth: 920
    property int windowSizeHeight: 680
    property bool resolutionLocked: false
    property bool geometryReady: false
    readonly property int minimumWindowWidth: 720
    readonly property int minimumWindowHeight: 520
    readonly property int maximumAllowedWindowWidth: Math.max(minimumWindowWidth,
        Math.min(3840, Screen.desktopAvailableWidth > 0 ? Screen.desktopAvailableWidth : 3840))
    readonly property int maximumAllowedWindowHeight: Math.max(minimumWindowHeight,
        Math.min(2160, Screen.desktopAvailableHeight > 0 ? Screen.desktopAvailableHeight : 2160))
    readonly property int lockedWindowWidth: Math.max(minimumWindowWidth,
        Math.min(maximumAllowedWindowWidth, windowSizeWidth))
    readonly property int lockedWindowHeight: Math.max(minimumWindowHeight,
        Math.min(maximumAllowedWindowHeight, windowSizeHeight))
    onWidthChanged: if (geometryReady && !resolutionLocked) windowSizeSaveTimer.restart()
    onHeightChanged: if (geometryReady && !resolutionLocked) windowSizeSaveTimer.restart()

    function performClose(action) {
        window.saveWindowResolution()
        if (action === "tray" && windowsPlatform.trayAvailable)
            windowsPlatform.hideToTray()
        else
            windowsPlatform.quit()
    }

    function requestClose() {
        window.saveWindowResolution()
        if (Qt.platform.os !== "windows") {
            window.close()
            return
        }
        if (appearanceSettings.closeAction !== "tray" && appearanceSettings.closeAction !== "exit") {
            rememberCloseChoice.checked = false
            closeChoiceDialog.open()
        } else {
            window.performClose(appearanceSettings.closeAction)
        }
    }

    function saveWindowResolution() {
        windowSizeSaveTimer.stop()
        if (!resolutionLocked) {
            window.windowSizeWidth = Math.round(window.width)
            window.windowSizeHeight = Math.round(window.height)
        }
        appearanceSettings.sync()
    }

    function toggleEntry(entryId, kind) {
        if (window.expandedEntryId === entryId) {
            window.expandedEntryId = 0
            window.expandedEntryText = ""
            window.expandedEntryLoadFailed = false
            return
        }
        window.expandedEntryId = entryId
        window.expandedEntryText = ""
        window.expandedEntryLoadFailed = false
        if (kind !== "图片")
            historyModel.loadEntry(entryId)
    }

    function copySucceeded(message) {
        window.historyMessage = message
        if (Qt.platform.os === "windows" && appearanceSettings.autoPasteIntoPreviousWindow
                && !windowsPlatform.pasteToPreviousWindow())
            window.historyMessage = "已复制到剪贴板，但未能自动粘贴。"
    }

    function refreshFilteredHistory() {
        const kinds = { "文本": "text", "链接": "link", "文件": "path", "图片": "image", "收藏": "favorite" }
        clipList.currentIndex = 0
        historyModel.setFilter(searchField.text, kinds[selectedCategory] || "")
    }

    function openSettings() {
        window.pendingStorageDirectory = appPaths.pendingDataDirectory.length > 0
                ? appPaths.pendingDataDirectory : appPaths.currentDataDirectory
        window.storageError = ""
        window.settingsPageVisible = true
    }

    function categorySymbol(category) {
        const symbols = { "全部": "▦", "文本": "T", "链接": "↗", "文件": "▤", "图片": "▧", "收藏": "★" }
        return symbols[category] || "•"
    }

    function parseCustomTheme(jsonText) {
        if (typeof jsonText !== "string" || jsonText.length === 0 || jsonText.length > 65536)
            return { ok: false, error: "主题文件为空或过大。" }

        let source
        try {
            source = JSON.parse(jsonText)
        } catch (error) {
            return { ok: false, error: "主题 JSON 格式错误。" }
        }
        if (!source || typeof source !== "object" || Array.isArray(source))
            return { ok: false, error: "主题内容必须是 JSON 对象。" }

        const colorKeys = ["page", "surface", "sidebarSurface", "rowSurface", "line", "textMain",
                           "textMuted", "accent", "selectedRow", "selectedText", "inputSurface",
                           "tileSurface", "tagSurface", "danger", "dangerSurface", "favorite", "onAccent"]
        const allowedKeys = colorKeys.concat(["name", "kindAccent", "kindTint"])
        for (const key of Object.keys(source)) {
            if (allowedKeys.indexOf(key) < 0)
                return { ok: false, error: "未知主题字段：" + key }
        }
        for (const key of ["page", "surface", "textMain", "accent"]) {
            if (!Object.prototype.hasOwnProperty.call(source, key) || typeof source[key] !== "string"
                    || !/^#[0-9a-fA-F]{6}$/.test(source[key]))
                return { ok: false, error: "必须提供 page、surface、textMain 和 accent 四个 #RRGGBB 颜色。" }
        }

        const palette = JSON.parse(JSON.stringify(ThemePalette.custom))
        for (const key of colorKeys) {
            if (source[key] === undefined)
                continue
            if (typeof source[key] !== "string" || !/^#[0-9a-fA-F]{6}$/.test(source[key]))
                return { ok: false, error: key + " 必须是 #RRGGBB 颜色。" }
            palette[key] = source[key]
        }
        const kinds = ["文本", "链接", "文件", "图片"]
        for (const key of ["kindAccent", "kindTint"]) {
            if (source[key] === undefined)
                continue
            if (!source[key] || typeof source[key] !== "object" || Array.isArray(source[key]))
                return { ok: false, error: key + " 必须是按内容类型填写的颜色对象。" }
            for (const kind of Object.keys(source[key])) {
                if (kinds.indexOf(kind) < 0 || typeof source[key][kind] !== "string"
                        || !/^#[0-9a-fA-F]{6}$/.test(source[key][kind]))
                    return { ok: false, error: key + " 中存在无效的类型或颜色。" }
                palette[key][kind] = source[key][kind]
            }
        }
        if (source.name !== undefined && (typeof source.name !== "string" || source.name.trim().length === 0
                                          || source.name.length > 48))
            return { ok: false, error: "主题名称需为 1 到 48 个字符。" }
        return { ok: true, palette: palette, name: source.name ? source.name.trim() : "自定义主题" }
    }

    function parseImportedThemes(jsonText) {
        if (typeof jsonText !== "string")
            return { ok: false, error: "已保存的导入主题列表无效。" }
        if (jsonText.length === 0)
            return { ok: true, themes: [] }

        let source
        try {
            source = JSON.parse(jsonText)
        } catch (error) {
            return { ok: false, error: "已保存的导入主题列表不是有效 JSON。" }
        }
        if (!Array.isArray(source))
            return { ok: false, error: "已保存的导入主题列表必须是数组。" }

        const themes = []
        const ids = []
        for (const entry of source) {
            if (!entry || typeof entry !== "object" || typeof entry.id !== "string"
                    || entry.id.length === 0 || entry.id.length > 64 || ids.indexOf(entry.id) >= 0
                    || typeof entry.name !== "string" || !entry.palette
                    || typeof entry.palette !== "object" || Array.isArray(entry.palette))
                return { ok: false, error: "已保存的导入主题列表包含无效条目。" }
            const parsed = window.parseCustomTheme(JSON.stringify(
                Object.assign({}, entry.palette, { name: entry.name })))
            if (!parsed.ok)
                return { ok: false, error: "已保存主题“" + entry.name + "”无效：" + parsed.error }
            ids.push(entry.id)
            themes.push({ id: entry.id, name: parsed.name, palette: parsed.palette })
        }
        return { ok: true, themes: themes }
    }

    function loadImportedThemes() {
        const files = appPaths.readImportedThemeFiles()
        const themes = []
        const ids = []
        const messages = []
        if (files.ok) {
            for (const file of files.themes) {
                const parsed = window.parseCustomTheme(file.json)
                if (!parsed.ok) {
                    messages.push(file.id + ".json：" + parsed.error)
                    continue
                }
                ids.push(file.id)
                themes.push({ id: file.id, name: parsed.name, palette: parsed.palette })
            }
            for (const error of files.errors)
                messages.push(error)
        } else {
            messages.push(files.error)
        }

        const legacy = window.parseImportedThemes(window.importedThemesJson)
        if (legacy.ok) {
            let migrated = true
            for (const theme of legacy.themes) {
                if (ids.indexOf(theme.id) >= 0)
                    continue
                const saved = appPaths.saveImportedThemeFile(theme.id,
                    JSON.stringify(Object.assign({}, theme.palette, { name: theme.name })))
                if (!saved.ok) {
                    migrated = false
                    messages.push("迁移主题“" + theme.name + "”失败：" + saved.error)
                }
                ids.push(theme.id)
                themes.push(theme)
            }
            // Keep the old settings until every theme has a file, so a failed migration can retry.
            if (migrated && legacy.themes.length > 0) {
                window.importedThemesJson = "[]"
                appearanceSettings.sync()
            }
        } else {
            messages.push(legacy.error)
        }
        window.importedThemes = themes
        if (ids.indexOf(window.selectedImportedThemeId) < 0)
            window.selectedImportedThemeId = ""
        if (messages.length > 0)
            window.themeImportMessage = messages.join("\n")
    }

    function importCustomTheme(jsonText) {
        const result = window.parseCustomTheme(jsonText)
        if (!result.ok) {
            window.themeImportMessage = result.error
            return false
        }
        window.pendingImportedTheme = result
        importThemeNameDialog.suggestedName = result.name
        window.themeImportMessage = ""
        importThemeNameDialog.open()
        return true
    }

    function addImportedTheme(name) {
        const importedName = typeof name === "string" ? name.trim() : ""
        if (importedName.length === 0 || importedName.length > 48) {
            window.themeImportMessage = "主题名称需为 1 到 48 个字符。"
            return false
        }
        if (!window.pendingImportedTheme) {
            window.themeImportMessage = "请重新选择要导入的主题文件。"
            return false
        }
        const id = "theme-" + Date.now() + "-" + (window.importedThemes.length + 1)
        const entry = {
            id: id,
            name: importedName,
            palette: JSON.parse(JSON.stringify(window.pendingImportedTheme.palette))
        }
        const saved = appPaths.saveImportedThemeFile(id,
            JSON.stringify(Object.assign({}, entry.palette, { name: importedName })))
        if (!saved.ok) {
            window.themeImportMessage = saved.error
            return false
        }
        const themes = window.importedThemes.concat([entry])
        window.importedThemes = themes
        window.selectedImportedThemeId = id
        window.themeMode = "自定义"
        window.themeImportMessage = "已添加主题“" + importedName + "”，可在上方主题列表切换。"
        window.pendingImportedTheme = null
        appearanceSettings.sync()
        return true
    }

    function themeOptions() {
        const options = [
            { id: "暗色", name: "暗色", palette: window.themePreviewColors("暗色") },
            { id: "浅色", name: "浅色", palette: window.themePreviewColors("浅色") },
            { id: "北极光", name: "北极光", palette: window.themePreviewColors("北极光") },
            { id: "custom", name: window.customThemeName, palette: window.customThemePalette }
        ]
        for (const theme of window.importedThemes)
            options.push({ id: "imported:" + theme.id, name: theme.name, palette: theme.palette })
        return options
    }

    function themeIsSelected(id) {
        if (id.indexOf("imported:") === 0)
            return window.themeMode === "自定义" && window.selectedImportedThemeId === id.substring(9)
        if (id === "custom")
            return window.themeMode === "自定义" && window.selectedImportedThemeId.length === 0
        return window.themeMode === id || (id === "北极光" && window.themeMode === "蓝色")
    }

    function selectThemeOption(id) {
        if (id.indexOf("imported:") === 0) {
            window.selectedImportedThemeId = id.substring(9)
            window.themeMode = "自定义"
        } else {
            window.selectedImportedThemeId = ""
            window.themeMode = id === "custom" ? "自定义" : id
        }
        appearanceSettings.sync()
    }

    function currentCustomThemePalette() {
        for (const theme of window.importedThemes) {
            if (theme.id === window.selectedImportedThemeId)
                return theme.palette
        }
        return window.customThemePalette
    }

    function themePreviewColors(name) {
        if (name === "custom")
            return window.customThemePalette
        if (name.indexOf("imported:") === 0) {
            for (const theme of window.importedThemes) {
                if (theme.id === name.substring(9))
                    return theme.palette
            }
            return window.customThemePalette
        }
        if (name === "浅色")
            return { page: "#F8FAFC", surface: "#FFFFFF", sidebarSurface: "#F1F5F9", line: "#E2E8F0",
                     textMain: "#0F172A", textMuted: "#475569", accent: "#4F46E5", onAccent: "#FFFFFF", inputSurface: "#F8FAFC" }
        if (name === "北极光")
            return { page: "#2E3440", surface: "#434C5E", sidebarSurface: "#3B4252", line: "#4C566A",
                     textMain: "#ECEFF4", textMuted: "#D8DEE9", accent: "#88C0D0", onAccent: "#FFFFFF", inputSurface: "#2E3440" }
        return { page: "#0A0E17", surface: "#141C30", sidebarSurface: "#101626", line: "#30384A",
                 textMain: "#F1F5F9", textMuted: "#94A3B8", accent: "#6366F1", onAccent: "#FFFFFF", inputSurface: "#0A0E17" }
    }

    function kindAccent(kind) {
        if (window.themeMode === "自定义")
            return window.activeThemePalette.kindAccent[kind] || window.activeThemePalette.accent
        if (window.themeMode === "浅色")
            return ({ "文本": "#047857", "链接": "#0369A1", "文件": "#7C3AED", "图片": "#B45309" })[kind] || window.accent
        return ({ "文本": "#10B981", "链接": "#38BDF8", "文件": "#A78BFA", "图片": "#F59E0B" })[kind] || window.accent
    }

    function kindTint(kind) {
        if (window.themeMode === "自定义")
            return window.activeThemePalette.kindTint[kind] || window.activeThemePalette.tagSurface
        if (window.themeMode === "浅色")
            return ({ "文本": "#D1FAE5", "链接": "#E0F2FE", "文件": "#F3E8FF", "图片": "#FEF3C7" })[kind] || window.tagSurface
        return ({ "文本": "#102F2A", "链接": "#102B3B", "文件": "#291E3D", "图片": "#3B2B16" })[kind] || window.tagSurface
    }

    function capturedKeyName(key) {
        if ((key >= Qt.Key_A && key <= Qt.Key_Z) || (key >= Qt.Key_0 && key <= Qt.Key_9))
            return String.fromCharCode(key)
        if (key >= Qt.Key_F1 && key <= Qt.Key_F24)
            return "F" + (key - Qt.Key_F1 + 1)
        const names = {
            [Qt.Key_Space]: "Space", [Qt.Key_Tab]: "Tab", [Qt.Key_Return]: "Return",
            [Qt.Key_Enter]: "Enter", [Qt.Key_Escape]: "Esc", [Qt.Key_Insert]: "Ins",
            [Qt.Key_Delete]: "Del", [Qt.Key_Home]: "Home", [Qt.Key_End]: "End",
            [Qt.Key_PageUp]: "PgUp", [Qt.Key_PageDown]: "PgDown", [Qt.Key_Left]: "Left",
            [Qt.Key_Up]: "Up", [Qt.Key_Right]: "Right", [Qt.Key_Down]: "Down"
        }
        return names[key] || ""
    }

    function showHotkeyFailure(sequence, reason, detail) {
        window.historyMessage = reason
        hotkeyErrorDialog.failedSequence = sequence
        hotkeyErrorDialog.failureReason = reason
        hotkeyErrorDialog.recoveryDetail = detail
        // Startup may hide the window after tray initialization; open after that completes.
        Qt.callLater(function() {
            if (!window.visible && Qt.platform.os === "windows")
                windowsPlatform.showWindow()
            hotkeyErrorDialog.open()
        })
    }

    function failCapturedHotkey(sequence, reason) {
        const previous = window.hotkeyDraft
        const restored = windowsPlatform.globalHotkey === previous
                || windowsPlatform.setGlobalHotkey(previous)
        const detail = restored
                ? (previous.length > 0 ? "继续使用原快捷键：" + previous : "当前未启用全局快捷键，可通过托盘显示窗口。")
                : "原快捷键“" + previous + "”也无法恢复：" + windowsPlatform.lastError
        window.hotkeyCaptureActive = false
        settingsPageLoader.item.hotkeyControl.focus = false
        window.showHotkeyFailure(sequence, reason, detail)
    }

    function openHotkeySettings() {
        window.pendingHotkeySettings = true
        window.openSettings()
        if (settingsPageLoader.status === Loader.Ready)
            Qt.callLater(settingsPageLoader.item.focusHotkeySettings)
    }

    function requestDelete(entryId) {
        deleteDialogLoader.active = true
        deleteDialogLoader.item.entryId = entryId
        deleteDialogLoader.item.open()
    }

    function openImportDialog() {
        importDialogLoader.active = true
        importDialogLoader.item.open()
    }

    function applyCapturedHotkey(key, modifiers) {
        if ([Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta, Qt.Key_AltGr].indexOf(key) >= 0)
            return
        const keyName = window.capturedKeyName(key)
        if (keyName.length === 0) {
            window.failCapturedHotkey("", "该按键不受支持，请按字母、数字、F1–F24 或方向/编辑键。")
            return
        }
        const parts = []
        if (modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (modifiers & Qt.AltModifier) parts.push("Alt")
        if (modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (modifiers & Qt.MetaModifier) parts.push("Meta")
        if (parts.length === 0) {
            window.failCapturedHotkey(keyName, "全局快捷键至少需要一个 Ctrl、Alt、Shift 或 Windows 修饰键。")
            return
        }
        parts.push(keyName)
        const sequence = parts.join("+")
        if (windowsPlatform.setGlobalHotkey(sequence)) {
            window.hotkeyDraft = windowsPlatform.globalHotkey
            appearanceSettings.globalHotkey = windowsPlatform.globalHotkey
            window.hotkeyCaptureActive = false
            settingsPageLoader.item.hotkeyControl.focus = false
            window.historyMessage = "全局快捷键已更新。"
        } else {
            window.failCapturedHotkey(sequence, windowsPlatform.lastError)
        }
    }

    function beginHotkeyCapture() {
        if (!settingsPageLoader.item) {
            window.openHotkeySettings()
            return
        }
        window.hotkeyDraft = appearanceSettings.globalHotkey
        window.hotkeyCaptureActive = true
        if (windowsPlatform.globalHotkey.length > 0 && !windowsPlatform.setGlobalHotkey("")) {
            window.hotkeyCaptureActive = false
            window.showHotkeyFailure(window.hotkeyDraft, windowsPlatform.lastError,
                "无法开始录制快捷键，请重试。")
            return
        }
        settingsPageLoader.item.hotkeyControl.forceActiveFocus()
    }

    function cancelHotkeyCapture() {
        if (!window.hotkeyCaptureActive)
            return
        const previous = window.hotkeyDraft
        const restored = windowsPlatform.globalHotkey === previous
                || windowsPlatform.setGlobalHotkey(previous)
        window.hotkeyCaptureActive = false
        if (!restored)
            window.showHotkeyFailure(previous, windowsPlatform.lastError,
                "取消录制后未能恢复原快捷键，可通过托盘显示窗口并重新设置。")
    }

    function applyStartWithWindows(enabled) {
        if (!windowsPlatform.setAutoStartEnabled(enabled)) {
            appearanceSettings.startWithWindows = !enabled
            window.historyMessage = windowsPlatform.lastError
            return false
        }
        appearanceSettings.startWithWindows = enabled
        window.historyMessage = enabled ? "已启用 Windows 开机启动。" : "已关闭 Windows 开机启动。"
        return true
    }

    component SettingsSection: Rectangle {
        id: settingsSection
        required property string heading
        required property string description
        required property string iconText
        required property color iconColor
        required property color surfaceColor
        required property color borderColor
        required property color titleColor
        required property color subtitleColor
        default property alias content: sectionBody.data
        implicitHeight: sectionLayout.implicitHeight
        color: surfaceColor
        border.color: borderColor
        radius: 14

        ColumnLayout {
            id: sectionLayout
            anchors.fill: parent
            spacing: 0
            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                spacing: 10
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
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: settingsSection.heading
                        color: settingsSection.titleColor
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: settingsSection.description
                        color: settingsSection.subtitleColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: settingsSection.borderColor
            }
            ColumnLayout {
                id: sectionBody
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 14
                Layout.bottomMargin: 16
                spacing: 12
            }
        }
    }

    component ThemeCheckBox: CheckBox {
        id: themedCheckBox
        required property var themeTokens
        spacing: 8
        implicitHeight: 30
        indicator: Rectangle {
            x: themedCheckBox.leftPadding
            y: themedCheckBox.height / 2 - height / 2
            implicitWidth: 18
            implicitHeight: 18
            radius: 5
            color: themedCheckBox.checked ? themedCheckBox.themeTokens.accent : themedCheckBox.themeTokens.inputSurface
            border.color: themedCheckBox.activeFocus || themedCheckBox.checked
                          ? themedCheckBox.themeTokens.accent : themedCheckBox.themeTokens.line
            border.width: themedCheckBox.activeFocus ? 2 : 1
            Text {
                anchors.centerIn: parent
                visible: themedCheckBox.checked
                text: "✓"
                color: themedCheckBox.themeTokens.onAccent
                font.pixelSize: 12
                font.bold: true
            }
        }
        contentItem: Text {
            leftPadding: themedCheckBox.indicator.width + themedCheckBox.spacing
            text: themedCheckBox.text
            color: themedCheckBox.enabled ? themedCheckBox.themeTokens.textMain : themedCheckBox.themeTokens.textMuted
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.NoWrap
        }
    }

    component SettingsActionButton: Button {
        id: actionButton
        required property var themeTokens
        property bool primaryAction: false
        implicitHeight: 36
        background: Rectangle {
            radius: 7
            color: actionButton.primaryAction
                   ? (actionButton.enabled ? actionButton.themeTokens.accent : actionButton.themeTokens.tileSurface)
                   : actionButton.hovered ? actionButton.themeTokens.selectedRow : actionButton.themeTokens.tileSurface
            border.color: actionButton.primaryAction ? actionButton.themeTokens.accent : actionButton.themeTokens.line
        }
        contentItem: Text {
            text: actionButton.text
            color: actionButton.primaryAction && actionButton.enabled
                   ? actionButton.themeTokens.onAccent : actionButton.themeTokens.textMain
            font.pixelSize: 11
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    readonly property bool nordMode: themeMode === "北极光" || themeMode === "蓝色"
    readonly property bool customMode: themeMode === "自定义"
    readonly property var activeThemePalette: window.currentCustomThemePalette()
    readonly property color page: customMode ? activeThemePalette.page : themeMode === "浅色" ? "#F8FAFC" : nordMode ? "#2E3440" : "#0A0E17"
    readonly property color surface: customMode ? activeThemePalette.surface : themeMode === "浅色" ? "#FFFFFF" : nordMode ? "#434C5E" : "#141C30"
    readonly property color sidebarSurface: customMode ? activeThemePalette.sidebarSurface : themeMode === "浅色" ? "#F1F5F9" : nordMode ? "#3B4252" : "#101626"
    readonly property color rowSurface: customMode ? activeThemePalette.rowSurface : themeMode === "浅色" ? "#FFFFFF" : nordMode ? "#3B4252" : "#141C30"
    readonly property color line: customMode ? activeThemePalette.line : themeMode === "浅色" ? "#E2E8F0" : nordMode ? "#4C566A" : "#30384A"
    readonly property color textMain: customMode ? activeThemePalette.textMain : themeMode === "浅色" ? "#0F172A" : nordMode ? "#ECEFF4" : "#F1F5F9"
    readonly property color textMuted: customMode ? activeThemePalette.textMuted : themeMode === "浅色" ? "#475569" : nordMode ? "#D8DEE9" : "#94A3B8"
    readonly property color accent: customMode ? activeThemePalette.accent : themeMode === "浅色" ? "#4F46E5" : nordMode ? "#88C0D0" : "#6366F1"
    readonly property color selectedRow: customMode ? activeThemePalette.selectedRow : themeMode === "浅色" ? "#E0E7FF" : nordMode ? "#4C566A" : "#1C2640"
    readonly property color selectedText: customMode ? activeThemePalette.selectedText : textMain
    readonly property color inputSurface: customMode ? activeThemePalette.inputSurface : themeMode === "浅色" ? "#F8FAFC" : nordMode ? "#2E3440" : "#0A0E17"
    readonly property color tileSurface: customMode ? activeThemePalette.tileSurface : themeMode === "浅色" ? "#EEF2F7" : nordMode ? "#4C566A" : "#1C2640"
    readonly property color tagSurface: customMode ? activeThemePalette.tagSurface : themeMode === "浅色" ? "#EEF2F7" : nordMode ? "#4C566A" : "#1C2640"
    readonly property color dangerColor: customMode ? activeThemePalette.danger : themeMode === "浅色" ? "#A94732" : "#F0A18D"
    readonly property color dangerSurface: customMode ? activeThemePalette.dangerSurface : themeMode === "浅色" ? "#FEE2E2" : "#3A222A"
    readonly property color favoriteColor: customMode ? activeThemePalette.favorite : "#EAB308"
    readonly property color onAccent: customMode ? activeThemePalette.onAccent : "#FFFFFF"

    Settings {
        id: appearanceSettings
        location: appPaths.settingsFileUrl
        category: "Appearance"
        property alias themeMode: window.themeMode
        property alias selectedImportedThemeId: window.selectedImportedThemeId
        // Legacy imported palettes are migrated into themes/*.json on startup.
        property alias importedThemesJson: window.importedThemesJson
        property alias windowSizeWidth: window.windowSizeWidth
        property alias windowSizeHeight: window.windowSizeHeight
        property alias resolutionLocked: window.resolutionLocked
        // Read once to migrate custom themes saved by earlier versions into the sidecar JSON file.
        property string customThemeJson: ""
        property string globalHotkey: "Ctrl+Shift+V"
        property bool autoPasteIntoPreviousWindow: true
        property bool startWithWindows: false
        property string closeAction: "ask"
        property int maximumEntries: 0
        onMaximumEntriesChanged: historyModel.maximumEntries = maximumEntries
        property int retentionHours: 0
        onRetentionHoursChanged: historyModel.retentionHours = retentionHours
    }

    Component.onCompleted: {
        historyModel.maximumEntries = appearanceSettings.maximumEntries
        appearanceSettings.maximumEntries = historyModel.maximumEntries
        historyModel.retentionHours = appearanceSettings.retentionHours
        appearanceSettings.retentionHours = historyModel.retentionHours
        window.windowSizeWidth = window.lockedWindowWidth
        window.windowSizeHeight = window.lockedWindowHeight
        window.width = window.windowSizeWidth
        window.height = window.windowSizeHeight
        window.geometryReady = true
        windowSizeSaveTimer.stop()
        if (window.themeMode === "跟随系统") {
            window.themeMode = "暗色"
            appearanceSettings.sync()
        }
        let legacyTheme = ""
        if (appearanceSettings.customThemeJson.length > 0) {
            const savedTheme = window.parseCustomTheme(appearanceSettings.customThemeJson)
            if (savedTheme.ok)
                legacyTheme = appearanceSettings.customThemeJson
        }
        const themeFile = appPaths.readCustomThemeFile(legacyTheme)
        if (themeFile.ok) {
            appearanceSettings.customThemeJson = ""
            const savedTheme = window.parseCustomTheme(themeFile.json)
            if (savedTheme.ok) {
                window.customThemePalette = savedTheme.palette
                window.customThemeName = savedTheme.name
            } else {
                window.themeImportMessage = "自定义主题文件无效：" + savedTheme.error
            }
        } else {
            window.themeImportMessage = themeFile.error
        }
        window.loadImportedThemes()
        if (Qt.platform.os === "windows") {
            window.hotkeyDraft = appearanceSettings.globalHotkey
            if (appearanceSettings.startWithWindows
                    && !windowsPlatform.setAutoStartEnabled(true))
                window.historyMessage = windowsPlatform.lastError
        }
    }

    Connections {
        target: Qt.platform.os === "windows" ? windowsPlatform : null
        function onLastErrorChanged() {
            if (windowsPlatform.lastError.length > 0)
                window.historyMessage = windowsPlatform.lastError
        }
        function onGlobalHotkeyChanged() {
            if (!window.hotkeyCaptureActive) {
                window.hotkeyDraft = windowsPlatform.globalHotkey
                appearanceSettings.globalHotkey = windowsPlatform.globalHotkey
            }
        }
        function onTrayAvailableChanged() {
            if (windowsPlatform.trayAvailable && !window.hotkeyCaptureActive) {
                const sequence = window.hotkeyDraft
                if (!windowsPlatform.setGlobalHotkey(sequence))
                    window.showHotkeyFailure(sequence, windowsPlatform.lastError,
                        "启动时未能启用快捷键，可修改组合键；暂时可通过托盘显示窗口。")
            }
        }
    }

    Connections {
        target: historyModel
        function onCopyFailed() { window.historyMessage = "复制失败，请重试。" }
        function onCopySourceMissing() { window.historyMessage = "源文件已不存在，无法复制到剪贴板。" }
        function onCopyContentReady(kind, content) { window.copySucceeded("已复制到剪贴板。") }
        function onCopyImageReady(image) { window.copySucceeded("已复制图片到剪贴板。") }
        function onEntryContentLoaded(entryId, kind, content) {
            if (window.expandedEntryId === entryId)
                window.expandedEntryText = content
        }
        function onEntryContentLoadFailed(entryId) {
            if (window.expandedEntryId === entryId)
                window.expandedEntryLoadFailed = true
        }
        function onRetentionFailed() { window.historyMessage = "自动清理失败，将在下一次检查时重试。" }
        function onRetentionCompleted(removedCount, cleanupPending) {
            window.historyMessage = "已自动清理 " + removedCount + " 条非收藏记录。"
                    + (cleanupPending ? "部分图片文件未能清理。" : "")
        }
        function onMutationFailed() { window.historyMessage = "操作失败，请重试。" }
        function onMutationCompleted(cleanupPending) {
            window.historyMessage = cleanupPending ? "记录已更新，但有图片文件未能清理。" : "历史记录已更新。"
        }
        function onLegacyImportFailed() { window.historyMessage = "旧历史导入失败，请检查数据库和 images 文件夹。" }
        function onLegacyImportCompleted(importedRows, alreadyImportedRows, skippedRows, cleanupPending) {
            window.historyMessage = "导入完成：新增 " + importedRows + " 条，已存在 "
                    + alreadyImportedRows + " 条，跳过 " + skippedRows + " 条"
                    + (cleanupPending ? "；有图片文件未能清理。" : "。")
        }
    }

    Connections {
        target: clipboardCapture
        function onRecordFailed(result) { window.historyMessage = "保存剪贴板内容失败。" }
        function onInitializationFailed(status) { window.historyMessage = "历史数据库初始化失败。" }
    }

    Timer {
        id: windowSizeSaveTimer
        interval: 250
        repeat: false
        onTriggered: window.saveWindowResolution()
    }

    Item {
        id: contentArea
        anchors.fill: parent
    }

    Rectangle {
        id: headerBar
        x: contentArea.x
        y: contentArea.y
        width: contentArea.width
        height: 52
        color: window.sidebarSurface
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: window.line }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 9
                color: window.tileSurface
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    source: "qrc:/icons/app-icon.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                Accessible.role: Accessible.Graphic
                Accessible.name: "剪贴板历史应用图标"
            }

            ColumnLayout {
                spacing: 1
                Text {
                    text: window.settingsPageVisible ? "偏好设置" : "剪贴板历史"
                    color: window.textMain
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    Accessible.role: Accessible.Heading
                }
                Text {
                    text: "本地记录 · 快速回用"
                    color: window.textMuted
                    font.pixelSize: 10
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                id: historyNavButton
                implicitWidth: 32
                implicitHeight: 32
                checkable: true
                checked: !window.settingsPageVisible
                Accessible.name: "历史列表"
                contentItem: Text {
                    text: "▤"
                    color: historyNavButton.checked ? window.accent : window.textMuted
                    font.family: "Segoe UI Symbol"
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    color: historyNavButton.checked ? window.selectedRow :
                           historyNavButton.hovered ? window.tileSurface : "transparent"
                }
                onClicked: window.settingsPageVisible = false
            }

            ToolButton {
                id: settingsButton
                objectName: "settingsButton"
                implicitWidth: 32
                implicitHeight: 32
                checkable: true
                checked: window.settingsPageVisible
                Accessible.name: "偏好设置"
                contentItem: Text {
                    text: "⚙"
                    color: settingsButton.checked ? window.accent : window.textMuted
                    font.family: "Segoe UI Symbol"
                    font.pixelSize: 17
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    color: settingsButton.checked ? window.selectedRow :
                           settingsButton.hovered ? window.tileSurface : "transparent"
                }
                onClicked: window.openSettings()
            }

            Rectangle {
                visible: Qt.platform.os === "windows"
                implicitWidth: headerHotkeyText.implicitWidth + 16
                implicitHeight: 24
                radius: 5
                color: window.themeMode === "浅色" ? "#F1F5F9" : Qt.rgba(1, 1, 1, 0.04)
                border.color: window.line
                Text {
                    id: headerHotkeyText
                    anchors.centerIn: parent
                    text: windowsPlatform.globalHotkey.length > 0 ? windowsPlatform.globalHotkey : "未设置快捷键"
                    color: window.textMuted
                    font.family: "Consolas"
                    font.pixelSize: 11
                }
                Accessible.name: "全局快捷键 " + headerHotkeyText.text
            }

            ToolButton {
                id: topCloseButton
                implicitWidth: 32
                implicitHeight: 32
                Accessible.name: "关闭剪贴板历史"
                contentItem: Text {
                    text: "×"
                    color: topCloseButton.hovered ? window.dangerColor : window.textMuted
                    font.pixelSize: 21
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    anchors.centerIn: parent
                    width: 30
                    height: 30
                    radius: 15
                    color: topCloseButton.hovered ? window.dangerSurface : "transparent"
                    border.color: window.line
                }
                onClicked: window.requestClose()
            }
        }

        DragHandler {
            target: null
            onActiveChanged: if (active) window.startSystemMove()
        }
    }

    ColumnLayout {
        id: historyPage
        x: contentArea.x
        y: contentArea.y + headerBar.height
        width: contentArea.width
        height: contentArea.height - headerBar.height
        spacing: 0
        visible: !window.settingsPageVisible

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.topMargin: 16
            Layout.bottomMargin: 12
            spacing: 12

            TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: "搜索历史记录…"
                onTextEdited: searchTimer.restart()
                leftPadding: 38
                rightPadding: 48
                font.pixelSize: 12
                color: window.textMain
                placeholderTextColor: window.textMuted
                Accessible.name: "搜索历史记录"
                Keys.onEscapePressed: {
                    text = ""
                    window.refreshFilteredHistory()
                }
                background: Rectangle {
                    radius: 11
                    color: window.rowSurface
                    border.color: searchField.activeFocus ? window.accent : window.line
                    border.width: searchField.activeFocus ? 1.5 : 1
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: "⌕"
                        color: window.textMuted
                        font.pixelSize: 19
                    }
                    Rectangle {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: escHint.implicitWidth + 10
                        implicitHeight: 18
                        radius: 4
                        color: window.tagSurface
                        border.color: window.line
                        Text {
                            id: escHint
                            anchors.centerIn: parent
                            text: "ESC"
                            color: window.textMuted
                            font.family: "Consolas"
                            font.pixelSize: 10
                        }
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 6
                columnSpacing: 6
                rowSpacing: 6
                Repeater {
                    id: categoryRepeater
                    model: ["全部", "文本", "链接", "文件", "图片", "收藏"]
                    delegate: Button {
                        id: categoryButton
                        required property string modelData
                        checkable: true
                        checked: window.selectedCategory === modelData
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        Layout.minimumWidth: 0
                        Layout.preferredHeight: 30
                        Accessible.name: modelData + (checked ? "，当前分类" : "")
                        Accessible.description: "筛选" + modelData + "记录"

                        contentItem: Item {
                            implicitWidth: categoryContentGroup.implicitWidth
                            implicitHeight: categoryContentGroup.implicitHeight
                            Row {
                                id: categoryContentGroup
                                anchors.centerIn: parent
                                spacing: 5
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: window.categorySymbol(categoryButton.modelData)
                                    color: categoryButton.checked ? window.onAccent : window.accent
                                    font.pixelSize: 12
                                    font.bold: true
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: categoryButton.modelData
                                    color: categoryButton.checked ? window.onAccent : window.textMuted
                                    font.pixelSize: 11
                                }
                            }
                        }

                        background: Rectangle {
                            radius: 10
                            color: categoryButton.checked ? window.accent : window.tagSurface
                            border.color: categoryButton.activeFocus ? window.accent : window.line
                            border.width: categoryButton.activeFocus ? 1 : 0
                        }

                        onClicked: {
                            window.selectedCategory = modelData
                            window.refreshFilteredHistory()
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: window.line
        }

        Timer {
            id: searchTimer
            interval: 250
            repeat: false
            onTriggered: window.refreshFilteredHistory()
        }

        ListView {
            id: clipList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: historyModel
            cacheBuffer: 0 // Avoid cancelling incubating offscreen delegates after a filter reset.
            leftMargin: 20
            rightMargin: 20
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            WheelHandler {
                target: null
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(event) {
                    const delta = event.pixelDelta.y !== 0 ? event.pixelDelta.y : event.angleDelta.y / 2
                    const minimum = clipList.originY - clipList.topMargin
                    const maximum = Math.max(minimum,
                        clipList.originY + clipList.contentHeight - clipList.height + clipList.bottomMargin)
                    clipList.cancelFlick()
                    clipList.contentY = Math.max(minimum, Math.min(maximum, clipList.contentY - delta))
                    event.accepted = true
                }
            }
            keyNavigationEnabled: true
            Keys.onReturnPressed: historyModel.copyItem(currentIndex)
            Keys.onEnterPressed: historyModel.copyItem(currentIndex)
            Keys.onDeletePressed: {
                if (currentItem) {
                    window.requestDelete(currentItem.entryId)
                }
            }

            delegate: Rectangle {
                required property string clipText
                required property string kind
                required property url imageUrl
                required property string time
                required property string uses
                required property string symbol
                required property bool favorite
                required property bool expandable
                required property bool sourceMissing
                required property int index
                required property var entryId

                readonly property bool expanded: window.expandedEntryId === entryId
                readonly property bool canExpand: expandable || entryTitle.truncated || kind === "图片"

                width: clipList.width - clipList.leftMargin - clipList.rightMargin
                height: entryLayout.implicitHeight + 20
                color: "transparent"
                border.color: "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: "复制 " + kind + "：" + clipText
                focus: ListView.isCurrentItem

                Rectangle {
                    anchors.fill: parent
                    radius: 14
                    color: ListView.isCurrentItem ? window.selectedRow : window.rowSurface
                    border.color: cardHover.hovered ? window.accent : window.line
                    border.width: cardHover.hovered ? 1.5 : 1
                }
                HoverHandler { id: cardHover }

                ColumnLayout {
                    id: entryLayout
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        id: entryHeader
                        visible: !expanded
                        Layout.fillWidth: true
                        Layout.preferredHeight: 38
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            radius: 10
                            color: window.kindTint(kind)
                            border.color: window.line
                            Text {
                                anchors.centerIn: parent
                                text: symbol
                                color: window.kindAccent(kind)
                                font.pixelSize: 17
                                font.bold: true
                            }
                            TapHandler {
                                onTapped: {
                                    clipList.currentIndex = index
                                    historyModel.copyItem(index)
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            TapHandler {
                                onTapped: {
                                    clipList.currentIndex = index
                                    historyModel.copyItem(index)
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 7
                                Text {
                                    id: entryTitle
                                    Layout.fillWidth: true
                                    text: clipText
                                    color: ListView.isCurrentItem ? window.selectedText : window.textMain
                                    font.pixelSize: 12
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }
                                Rectangle {
                                    Layout.preferredWidth: kindLabel.implicitWidth + 14
                                    Layout.preferredHeight: 19
                                    radius: 6
                                    color: window.kindTint(kind)
                                    Text {
                                        id: kindLabel
                                        anchors.centerIn: parent
                                        text: kind
                                        color: window.kindAccent(kind)
                                        font.pixelSize: 11
                                        font.weight: Font.Medium
                                    }
                                }
                                Text {
                                    visible: favorite
                                    text: "★"
                                    color: window.favoriteColor
                                    font.pixelSize: 12
                                    Accessible.name: favorite ? "已收藏" : ""
                                }
                            }
                            RowLayout {
                                spacing: 8
                                Text {
                                    visible: uses.length > 0
                                    text: uses
                                    color: window.textMuted
                                    font.pixelSize: 11
                                }
                                Text {
                                    visible: sourceMissing
                                    text: "源文件已不存在"
                                    color: window.dangerColor
                                    font.pixelSize: 11
                                    Accessible.role: Accessible.AlertMessage
                                }
                            }
                        }

                    }

                    RowLayout {
                        visible: expanded && kind !== "图片"
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 34
                            Layout.alignment: Qt.AlignTop
                            radius: 10
                            color: window.kindTint(kind)
                            border.color: window.line
                            Text {
                                anchors.centerIn: parent
                                text: symbol
                                color: window.kindAccent(kind)
                                font.pixelSize: 17
                                font.bold: true
                            }
                            TapHandler {
                                onTapped: {
                                    clipList.currentIndex = index
                                    historyModel.copyItem(index)
                                }
                            }
                        }
                        ScrollView {
                            id: expandedTextScroll
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(300, Math.max(48, expandedText.implicitHeight))
                            clip: true
                            padding: 0
                            rightPadding: 12
                            contentWidth: availableWidth
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                            background: null
                            Text {
                                id: expandedText
                                width: expandedTextScroll.availableWidth
                                text: window.expandedEntryLoadFailed ? "完整内容读取失败，请收起后重试。" :
                                      (window.expandedEntryText.length > 0 ? window.expandedEntryText : "正在加载完整内容…")
                                textFormat: Text.PlainText
                                color: entryTitle.color
                                font: entryTitle.font
                                lineHeight: 1.5
                                lineHeightMode: Text.ProportionalHeight
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                Accessible.role: Accessible.StaticText
                            }
                        }
                    }

                    Item {
                        id: imagePreviewArea
                        visible: kind === "图片"
                        Layout.fillWidth: true
                        Layout.preferredHeight: visible
                            ? (expanded && imagePreview.status === Image.Ready
                               ? Math.max(180, imagePreview.implicitHeight) : 96)
                            : 0
                        clip: true
                        Image {
                            id: imagePreview
                            anchors.left: parent.left
                            anchors.top: parent.top
                            width: parent.width
                            height: expanded && status === Image.Ready ? implicitHeight : parent.height
                            source: imageUrl
                            sourceSize.width: Math.max(1, Math.round(width))
                            fillMode: expanded ? Image.PreserveAspectFit : Image.PreserveAspectCrop
                            verticalAlignment: Image.AlignTop
                            asynchronous: true
                            cache: true
                            onStatusChanged: if (status === Image.Error) window.historyMessage = "图片文件已丢失，无法预览。"
                        }
                        TapHandler {
                            id: imagePreviewTapHandler
                            onTapped: {
                                clipList.currentIndex = index
                                historyModel.copyItem(index)
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: imagePreview.status === Image.Error
                            text: "图片文件已丢失，无法预览。"
                            color: window.textMuted
                            font.pixelSize: 11
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 32
                        RowLayout {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            Rectangle {
                                visible: expanded
                                width: footerKindLabel.implicitWidth + 12
                                height: 19
                                radius: 6
                                color: window.kindTint(kind)
                                Text {
                                    id: footerKindLabel
                                    anchors.centerIn: parent
                                    text: kind
                                    color: window.kindAccent(kind)
                                    font.pixelSize: 11
                                }
                            }
                            Text {
                                visible: uses.length > 0 && expanded
                                text: uses
                                color: window.textMuted
                                font.pixelSize: 11
                            }
                            Text {
                                visible: sourceMissing
                                text: "源文件已不存在"
                                color: window.dangerColor
                                font.pixelSize: 11
                                Accessible.role: Accessible.AlertMessage
                            }
                        }
                        ToolButton {
                            id: expandToggleButton
                            anchors.centerIn: parent
                            visible: expanded || canExpand
                            Accessible.name: expanded ? "收起完整内容" : "展开完整内容"
                            implicitWidth: 76
                            implicitHeight: 29
                            contentItem: Text {
                                text: expanded ? "收起" : "展开"
                                color: window.textMain
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.NoWrap
                            }
                            background: Rectangle {
                                radius: 9
                                color: expandToggleButton.hovered ? window.tileSurface : "transparent"
                                border.color: expandToggleButton.activeFocus ? window.accent : window.line
                            }
                            onClicked: window.toggleEntry(entryId, kind)
                        }
                        RowLayout {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text {
                                text: time
                                color: window.textMuted
                                font.pixelSize: 11
                            }
                            ToolButton {
                                id: favoriteButton
                                text: favorite ? "★" : "☆"
                                Accessible.name: favorite ? "取消收藏" : "收藏"
                                implicitWidth: 28
                                implicitHeight: 29
                                palette.buttonText: favorite ? window.favoriteColor : window.textMuted
                                background: Rectangle { radius: 8; color: favoriteButton.hovered ? window.tileSurface : "transparent" }
                                onClicked: historyModel.setFavorite(index, !favorite)
                            }
                            ToolButton {
                                id: deleteButton
                                text: "×"
                                Accessible.name: "删除此记录"
                                implicitWidth: 28
                                implicitHeight: 29
                                palette.buttonText: window.textMuted
                                background: Rectangle { radius: 8; color: deleteButton.hovered ? window.dangerSurface : "transparent" }
                                onClicked: window.requestDelete(entryId)
                            }
                        }
                    }
                }
            }

            footer: Item {
                width: clipList.width
                height: historyModel.loading || historyModel.hasError || clipList.count === 0 ? 56 : 1
                Text {
                    anchors.centerIn: parent
                    text: historyModel.hasError ? "读取历史记录失败" : historyModel.loading ? "正在加载…" :
                          (searchField.text.length > 0 || window.selectedCategory !== "全部" ? "没有匹配记录" : "暂无剪贴板记录")
                    color: window.textMuted
                    font.pixelSize: 12
                    visible: clipList.count === 0
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: window.sidebarSurface
            border.color: window.line
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                Text {
                    text: window.historyMessage.length > 0 ? window.historyMessage :
                          historyModel.totalCount < 0 ? "正在读取记录总数…" : "共 " + historyModel.totalCount + " 条"
                    color: window.textMuted
                    font.pixelSize: 11
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "Enter 复制　·　Del 删除"
                    color: window.textMuted
                    font.pixelSize: 11
                }
            }
        }
    }

    Dialog {
        id: hotkeyErrorDialog
        property string failedSequence: ""
        property string failureReason: ""
        property string recoveryDetail: ""
        width: Math.min(window.width - 48, 480)
        anchors.centerIn: parent
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        title: "快捷键无法启用"
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        header: Label {
            text: hotkeyErrorDialog.title
            color: window.textMain
            font.pixelSize: 14
            font.weight: Font.DemiBold
            padding: 16
        }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                visible: hotkeyErrorDialog.failedSequence.length > 0
                text: "快捷键：" + hotkeyErrorDialog.failedSequence
                color: window.textMain
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: hotkeyErrorDialog.failureReason
                color: window.dangerColor
                font.pixelSize: 12
                wrapMode: Text.Wrap
                Accessible.role: Accessible.AlertMessage
            }
            Text {
                Layout.fillWidth: true
                text: hotkeyErrorDialog.recoveryDetail
                color: window.textMuted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            SettingsActionButton {
                themeTokens: window
                text: "知道了"
                onClicked: hotkeyErrorDialog.close()
            }
            SettingsActionButton {
                id: changeFailedHotkeyButton
                themeTokens: window
                text: "修改快捷键"
                primaryAction: true
                onClicked: {
                    hotkeyErrorDialog.close()
                    window.openHotkeySettings()
                }
            }
        }
    }

    FileDialog {
        id: legacyDatabaseDialog
        title: "选择旧版数据库"
        fileMode: FileDialog.OpenFile
        nameFilters: ["SQLite 数据库 (*.db *.sqlite *.sqlite3)", "所有文件 (*)"]
        onAccepted: {
            const path = appPaths.localFilePath(selectedFile)
            if (path.length > 0)
                window.legacyDatabasePath = path
        }
    }

    FileDialog {
        id: customThemeFileDialog
        title: "导入自定义主题"
        fileMode: FileDialog.OpenFile
        nameFilters: ["JSON 主题文件 (*.json)"]
        onAccepted: {
            const result = appPaths.readThemeFile(selectedFile)
            if (result.ok)
                window.importCustomTheme(result.json)
            else
                window.themeImportMessage = result.error
        }
    }

    Dialog {
        id: importThemeNameDialog
        title: "命名导入主题"
        width: Math.min(window.width - 48, 440)
        anchors.centerIn: parent
        modal: true
        property string suggestedName: ""
        onOpened: {
            importedThemeNameField.text = suggestedName
            importedThemeNameField.selectAll()
            importedThemeNameField.forceActiveFocus()
        }
        onRejected: window.pendingImportedTheme = null
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        contentItem: ColumnLayout {
            spacing: 10
            Text {
                Layout.fillWidth: true
                text: "输入名称后，主题会作为新选项加入列表。"
                color: window.textMuted
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }
            TextField {
                id: importedThemeNameField
                Layout.fillWidth: true
                maximumLength: 48
                placeholderText: "主题名称"
                color: window.textMain
                selectionColor: window.accent
                selectedTextColor: window.onAccent
                background: Rectangle {
                    color: window.inputSurface
                    border.color: importedThemeNameField.activeFocus ? window.accent : window.line
                    radius: 7
                }
                Keys.onReturnPressed: {
                    if (window.addImportedTheme(importedThemeNameField.text))
                        importThemeNameDialog.accept()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: window.themeImportMessage.length > 0
                text: window.themeImportMessage
                color: window.dangerColor
                font.pixelSize: 11
                wrapMode: Text.Wrap
            }
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            SettingsActionButton {
                themeTokens: window
                text: "取消"
                onClicked: importThemeNameDialog.reject()
            }
            SettingsActionButton {
                themeTokens: window
                text: "添加主题"
                primaryAction: true
                enabled: importedThemeNameField.text.trim().length > 0
                onClicked: {
                    if (window.addImportedTheme(importedThemeNameField.text))
                        importThemeNameDialog.accept()
                }
            }
        }
    }

    FolderDialog {
        id: storageFolderDialog
        title: "选择数据存储文件夹"
        currentFolder: appPaths.currentDataDirectoryUrl
        onAccepted: {
            const selectedPath = appPaths.localFilePath(selectedFolder)
            if (selectedPath.length > 0) {
                window.pendingStorageDirectory = selectedPath
                window.storageError = ""
            }
        }
    }

    Loader {
        id: deleteDialogLoader
        active: false
        sourceComponent: Dialog {
        id: deleteDialog
        parent: window.contentItem
        width: Math.min(window.width - 48, 480)
        property var entryId: 0
        anchors.centerIn: parent
        modal: true
        title: "删除记录？"
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: historyModel.removeItemById(entryId)
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        contentItem: Text { text: "此操作会删除选中的剪贴板历史记录。"; color: window.textMain; padding: 16; wrapMode: Text.Wrap }
    }

    }

    Loader {
        id: clearDialogLoader
        active: false
        sourceComponent: Dialog {
        id: clearDialog
        parent: window.contentItem
        width: Math.min(window.width - 48, 480)
        anchors.centerIn: parent
        modal: true
        title: "清空历史记录？"
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: historyModel.clearHistory()
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        contentItem: Text { text: "将删除全部历史记录及不再引用的图片文件。"; color: window.textMain; padding: 16; wrapMode: Text.Wrap }
    }

    }

    Loader {
        id: importDialogLoader
        active: false
        sourceComponent: Dialog {
        id: importDialog
        parent: window.contentItem
        width: Math.min(window.width - 48, 480)
        anchors.centerIn: parent
        modal: true
        title: "导入旧版历史？"
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: historyModel.importLegacyDatabase(window.legacyDatabasePath)
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        contentItem: Text {
            text: "程序会读取所选 SQLite 数据库及其同级 images 文件夹，将可识别记录导入当前历史。旧文件不会被修改。重复运行会跳过已导入记录。"
            color: window.textMain
            padding: 16
            wrapMode: Text.Wrap
        }
    }

    }

    Dialog {
        id: closeChoiceDialog
        anchors.centerIn: parent
        modal: true
        title: "关闭剪切板历史"
        closePolicy: Popup.NoAutoClose
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        contentItem: ColumnLayout {
            spacing: 12
            Text {
                Layout.fillWidth: true
                text: "选择关闭方式：保存到托盘后，程序会继续在后台监听剪贴板。"
                color: window.textMain
                wrapMode: Text.Wrap
            }
            ThemeCheckBox {
                id: rememberCloseChoice
                themeTokens: window
                text: "记住此选择，可在设置中更改"
                Accessible.name: text
            }
            RowLayout {
                Layout.fillWidth: true
                SettingsActionButton {
                    Layout.fillWidth: true
                    themeTokens: window
                    text: "保存到托盘"
                    primaryAction: true
                    enabled: windowsPlatform.trayAvailable
                    Accessible.name: "保存到系统托盘并继续监听"
                    onClicked: {
                        if (rememberCloseChoice.checked)
                            appearanceSettings.closeAction = "tray"
                        closeChoiceDialog.close()
                        window.performClose("tray")
                    }
                }
                SettingsActionButton {
                    Layout.fillWidth: true
                    themeTokens: window
                    text: "直接退出"
                    Accessible.name: "退出程序"
                    onClicked: {
                        if (rememberCloseChoice.checked)
                            appearanceSettings.closeAction = "exit"
                        closeChoiceDialog.close()
                        window.performClose("exit")
                    }
                }
            }
            SettingsActionButton {
                Layout.alignment: Qt.AlignRight
                themeTokens: window
                text: "取消"
                onClicked: closeChoiceDialog.close()
            }
        }
    }

    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.left: parent.left; width: 5; height: parent.height - 10; y: 5; cursorShape: Qt.SizeHorCursor; onPressed: window.startSystemResize(Qt.LeftEdge) }
    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.right: parent.right; width: 5; height: parent.height - 10; y: 5; cursorShape: Qt.SizeHorCursor; onPressed: window.startSystemResize(Qt.RightEdge) }
    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.top: parent.top; height: 5; width: parent.width - 10; x: 5; cursorShape: Qt.SizeVerCursor; onPressed: window.startSystemResize(Qt.TopEdge) }
    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.bottom: parent.bottom; height: 5; width: parent.width - 10; x: 5; cursorShape: Qt.SizeVerCursor; onPressed: window.startSystemResize(Qt.BottomEdge) }
    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.left: parent.left; anchors.top: parent.top; width: 10; height: 10; cursorShape: Qt.SizeFDiagCursor; onPressed: window.startSystemResize(Qt.LeftEdge | Qt.TopEdge) }
    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.right: parent.right; anchors.top: parent.top; width: 10; height: 10; cursorShape: Qt.SizeBDiagCursor; onPressed: window.startSystemResize(Qt.RightEdge | Qt.TopEdge) }
    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.left: parent.left; anchors.bottom: parent.bottom; width: 10; height: 10; cursorShape: Qt.SizeBDiagCursor; onPressed: window.startSystemResize(Qt.LeftEdge | Qt.BottomEdge) }
    MouseArea { z: 1; enabled: !window.resolutionLocked; anchors.right: parent.right; anchors.bottom: parent.bottom; width: 10; height: 10; cursorShape: Qt.SizeFDiagCursor; onPressed: window.startSystemResize(Qt.RightEdge | Qt.BottomEdge) }
    Text {
        anchors.centerIn: parent
        visible: window.settingsPageVisible && settingsPageLoader.status === Loader.Loading
        text: "正在加载设置…"
        color: window.textMuted
    }

    Loader {
        id: settingsPageLoader
        active: false
        asynchronous: true
        onLoaded: {
            if (window.pendingHotkeySettings) Qt.callLater(item.focusHotkeySettings)
        }
        x: contentArea.x
        y: contentArea.y + headerBar.height
        width: contentArea.width
        height: contentArea.height - headerBar.height
        visible: window.settingsPageVisible
        sourceComponent: Item {
        id: settingsPage
        property alias hotkeyControl: hotkeyCaptureButton
        function focusHotkeySettings() {
            if (!window.settingsPageVisible || !window.pendingHotkeySettings) return
            window.pendingHotkeySettings = false
            const flickable = settingsScroll.contentItem
            const position = hotkeySection.mapToItem(flickable.contentItem, 0, 0)
            flickable.contentY = Math.max(0, Math.min(position.y - 20,
                flickable.contentHeight - flickable.height))
            window.beginHotkeyCapture()
        }


        Rectangle { anchors.fill: parent; color: window.page }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            ScrollView {
                id: settingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                padding: 20
                clip: true

                ColumnLayout {
                    width: settingsScroll.availableWidth
                    spacing: 14

            SettingsSection {
                Layout.fillWidth: true
                visible: Qt.platform.os === "windows"
                heading: "窗口与托盘"
                description: "关闭行为和系统启动方式"
                iconText: "▱"
                iconColor: window.accent
                surfaceColor: window.surface
                borderColor: window.line
                titleColor: window.textMain
                subtitleColor: window.textMuted

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: "关闭按钮行为"; color: window.textMain; font.pixelSize: 12; font.weight: Font.Medium }
                        Text {
                            Layout.fillWidth: true
                            text: "选择保存到托盘后，窗口关闭时仍会在后台监听。"
                            color: window.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                    }
                    ComboBox {
                        id: closeActionCombo
                        Layout.preferredWidth: 190
                        Layout.preferredHeight: 38
                        model: ["每次询问", "保存到托盘", "直接退出"]
                        currentIndex: appearanceSettings.closeAction === "tray" ? 1 : appearanceSettings.closeAction === "exit" ? 2 : 0
                        Accessible.name: "右上角关闭按钮行为"
                        onActivated: appearanceSettings.closeAction = ["ask", "tray", "exit"][currentIndex]
                        background: Rectangle {
                            radius: 7
                            color: closeActionCombo.down ? window.selectedRow : window.inputSurface
                            border.color: closeActionCombo.activeFocus ? window.accent : window.line
                        }
                        contentItem: Text {
                            leftPadding: 12
                            rightPadding: 30
                            text: closeActionCombo.displayText
                            color: window.textMain
                            font.pixelSize: 11
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                        indicator: Text {
                            x: closeActionCombo.width - width - 11
                            y: (closeActionCombo.height - height) / 2
                            text: closeActionCombo.popup.visible ? "⌃" : "⌄"
                            color: window.accent
                            font.family: "Segoe UI Symbol"
                            font.pixelSize: 16
                        }
                        delegate: ItemDelegate {
                            id: closeActionDelegate
                            required property int index
                            required property string modelData
                            width: closeActionCombo.width
                            height: 36
                            highlighted: closeActionCombo.highlightedIndex === index
                            background: Rectangle {
                                radius: 5
                                color: parent.highlighted ? window.selectedRow : "transparent"
                            }
                            contentItem: Text {
                                text: modelData
                                leftPadding: 10
                                color: closeActionDelegate.highlighted ? window.accent : window.textMain
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        popup: Popup {
                            y: closeActionCombo.height + 4
                            width: closeActionCombo.width
                            implicitHeight: Math.min(contentItem.implicitHeight + 8, 132)
                            padding: 4
                            contentItem: ListView {
                                clip: true
                                implicitHeight: contentHeight
                                model: closeActionCombo.popup.visible ? closeActionCombo.delegateModel : null
                                currentIndex: closeActionCombo.highlightedIndex
                                boundsBehavior: Flickable.StopAtBounds
                            }
                            background: Rectangle {
                                radius: 8
                                color: window.surface
                                border.color: window.line
                            }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: "开机自动启动"; color: window.textMain; font.pixelSize: 12; font.weight: Font.Medium }
                        Text {
                            Layout.fillWidth: true
                            text: "登录 Windows 后启动并隐藏到托盘。"
                            color: window.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                    }
                    ThemeCheckBox {
                        id: startWithWindowsCheckBox
                        objectName: "startWithWindowsCheckBox"
                        themeTokens: window
                        text: "启用"
                        checked: appearanceSettings.startWithWindows
                        Accessible.name: "登录 Windows 后启动并隐藏到托盘"
                        onClicked: window.applyStartWithWindows(checked)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: "托盘不可用时窗口会显示。启动项只含程序路径和启动参数；配置与历史数据仍保存在软件数据目录。"
                    color: window.textMuted
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                heading: "窗口尺寸"
                description: "Qt 逻辑像素：宽度 720–" + window.maximumAllowedWindowWidth + " px，高度 520–"
                              + window.maximumAllowedWindowHeight + " px；未锁定时拖动窗口会自动记录尺寸"
                iconText: "↔"
                iconColor: window.accent
                surfaceColor: window.surface
                borderColor: window.line
                titleColor: window.textMain
                subtitleColor: window.textMuted

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        text: "宽"
                        color: window.textMain
                        font.pixelSize: 11
                    }
                    SpinBox {
                        id: windowWidthSpinBox
                        Layout.preferredWidth: 132
                        Layout.preferredHeight: 36
                        from: window.minimumWindowWidth
                        to: window.maximumAllowedWindowWidth
                        stepSize: 10
                        editable: true
                        leftPadding: 8
                        rightPadding: 24
                        enabled: !window.resolutionLocked
                        Accessible.name: "窗口宽度，像素"
                        onValueModified: window.width = value
                        Binding {
                            target: windowWidthSpinBox
                            property: "value"
                            value: window.width
                            when: !windowWidthSpinBox.activeFocus
                        }
                        contentItem: TextInput {
                            text: windowWidthSpinBox.displayText
                            color: window.textMain
                            selectionColor: window.accent
                            selectedTextColor: window.onAccent
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !windowWidthSpinBox.editable
                            validator: windowWidthSpinBox.validator
                            inputMethodHints: Qt.ImhDigitsOnly
                        }
                        up.indicator: Rectangle {
                            implicitWidth: 18
                            implicitHeight: 14
                            x: parent.width - width - 4
                            y: 4
                            radius: 3
                            color: windowWidthSpinBox.hovered ? window.tileSurface : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: "⌃"
                                color: window.textMuted
                                font.pixelSize: 10
                            }
                        }
                        down.indicator: Rectangle {
                            implicitWidth: 18
                            implicitHeight: 14
                            x: parent.width - width - 4
                            y: parent.height - height - 4
                            radius: 3
                            color: windowWidthSpinBox.hovered ? window.tileSurface : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: "⌄"
                                color: window.textMuted
                                font.pixelSize: 10
                            }
                        }
                        background: Rectangle {
                            radius: 7
                            color: window.inputSurface
                            border.color: windowWidthSpinBox.activeFocus ? window.accent : window.line
                            opacity: windowWidthSpinBox.enabled ? 1 : 0.55
                        }
                    }
                    Text {
                        text: "×"
                        color: window.textMuted
                        font.pixelSize: 14
                    }
                    Text {
                        text: "高"
                        color: window.textMain
                        font.pixelSize: 11
                    }
                    SpinBox {
                        id: windowHeightSpinBox
                        Layout.preferredWidth: 132
                        Layout.preferredHeight: 36
                        from: window.minimumWindowHeight
                        to: window.maximumAllowedWindowHeight
                        stepSize: 10
                        editable: true
                        leftPadding: 8
                        rightPadding: 24
                        enabled: !window.resolutionLocked
                        Accessible.name: "窗口高度，像素"
                        onValueModified: window.height = value
                        Binding {
                            target: windowHeightSpinBox
                            property: "value"
                            value: window.height
                            when: !windowHeightSpinBox.activeFocus
                        }
                        contentItem: TextInput {
                            text: windowHeightSpinBox.displayText
                            color: window.textMain
                            selectionColor: window.accent
                            selectedTextColor: window.onAccent
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            readOnly: !windowHeightSpinBox.editable
                            validator: windowHeightSpinBox.validator
                            inputMethodHints: Qt.ImhDigitsOnly
                        }
                        up.indicator: Rectangle {
                            implicitWidth: 18
                            implicitHeight: 14
                            x: parent.width - width - 4
                            y: 4
                            radius: 3
                            color: windowHeightSpinBox.hovered ? window.tileSurface : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: "⌃"
                                color: window.textMuted
                                font.pixelSize: 10
                            }
                        }
                        down.indicator: Rectangle {
                            implicitWidth: 18
                            implicitHeight: 14
                            x: parent.width - width - 4
                            y: parent.height - height - 4
                            radius: 3
                            color: windowHeightSpinBox.hovered ? window.tileSurface : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: "⌄"
                                color: window.textMuted
                                font.pixelSize: 10
                            }
                        }
                        background: Rectangle {
                            radius: 7
                            color: window.inputSurface
                            border.color: windowHeightSpinBox.activeFocus ? window.accent : window.line
                            opacity: windowHeightSpinBox.enabled ? 1 : 0.55
                        }
                    }
                    Item { Layout.fillWidth: true }
                    ThemeCheckBox {
                        id: resolutionLockCheckBox
                        themeTokens: window
                        text: "锁定分辨率"
                        checked: window.resolutionLocked
                        Accessible.name: "锁定窗口分辨率，禁止拖拽调整大小"
                        onToggled: {
                            windowSizeSaveTimer.stop()
                            window.windowSizeWidth = window.width
                            window.windowSizeHeight = window.height
                            window.resolutionLocked = checked
                            appearanceSettings.sync()
                        }
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                heading: "外观主题"
                description: "选择主题配色，或导入自己的主题"
                iconText: "◐"
                iconColor: window.favoriteColor
                surfaceColor: window.surface
                borderColor: window.line
                titleColor: window.textMain
                subtitleColor: window.textMuted
                Flow {
                    id: themePreviewFlow
                    Layout.fillWidth: true
                    Layout.minimumWidth: 180
                    spacing: 8
                    Repeater {
                        id: themePreviewRepeater
                        model: window.themeOptions()
                        delegate: Button {
                            id: themePreviewButton
                            required property var modelData
                            readonly property var preview: modelData.palette
                            width: 180
                            height: 166
                            readonly property bool selected: window.themeIsSelected(modelData.id)
                            Accessible.name: modelData.name
                                              + (selected ? "，当前主题" : "")
                            background: Rectangle {
                                radius: 10
                                color: themePreviewButton.selected ? window.selectedRow : window.surface
                                border.color: themePreviewButton.selected ? window.accent : window.line
                                border.width: themePreviewButton.selected ? 2 : 1
                            }
                            contentItem: ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 9
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.fillWidth: true
                                        text: themePreviewButton.modelData.name
                                        color: window.textMain
                                        font.pixelSize: 11
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        visible: themePreviewButton.selected
                                        text: "✓ 当前"
                                        color: window.accent
                                        font.pixelSize: 10
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 82
                                    radius: 7
                                    color: themePreviewButton.preview.page
                                    border.color: themePreviewButton.preview.line
                                    clip: true
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 5
                                        spacing: 3
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 12
                                            Rectangle {
                                                Layout.preferredWidth: 8
                                                Layout.preferredHeight: 8
                                                radius: 3
                                                color: themePreviewButton.preview.accent
                                            }
                                            Text {
                                                text: "剪贴板历史"
                                                color: themePreviewButton.preview.textMain
                                                font.pixelSize: 8
                                                font.weight: Font.DemiBold
                                            }
                                            Item { Layout.fillWidth: true }
                                        }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 15
                                            radius: 4
                                            color: themePreviewButton.preview.inputSurface
                                            border.color: themePreviewButton.preview.line
                                            Text {
                                                anchors.left: parent.left
                                                anchors.leftMargin: 6
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "⌕  搜索历史记录…"
                                                color: themePreviewButton.preview.textMuted
                                                font.pixelSize: 7
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 3
                                            Repeater {
                                                model: ["全部", "文本", "链接", "图片"]
                                                delegate: Rectangle {
                                                    id: previewFilterButton
                                                    required property int index
                                                    required property string modelData
                                                    Layout.fillWidth: true
                                                    Layout.preferredHeight: 12
                                                    radius: 4
                                                    color: index === 0 ? themePreviewButton.preview.accent : themePreviewButton.preview.surface
                                                    Row {
                                                        anchors.centerIn: parent
                                                        spacing: 1
                                                        Text {
                                                            text: window.categorySymbol(previewFilterButton.modelData)
                                                            color: previewFilterButton.index === 0
                                                                   ? (themePreviewButton.preview.onAccent || "#FFFFFF")
                                                                   : themePreviewButton.preview.accent
                                                            font.pixelSize: 6
                                                            font.bold: true
                                                        }
                                                        Text {
                                                            text: previewFilterButton.modelData
                                                            color: previewFilterButton.index === 0
                                                                   ? (themePreviewButton.preview.onAccent || "#FFFFFF")
                                                                   : themePreviewButton.preview.textMuted
                                                            font.pixelSize: 6
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        Rectangle {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 17
                                            radius: 4
                                            color: themePreviewButton.preview.surface
                                            border.color: themePreviewButton.preview.line
                                            RowLayout {
                                                anchors.fill: parent
                                                anchors.leftMargin: 5
                                                anchors.rightMargin: 5
                                                spacing: 4
                                                Rectangle {
                                                    Layout.preferredWidth: 5
                                                    Layout.preferredHeight: 5
                                                    radius: 3
                                                    color: themePreviewButton.preview.accent
                                                }
                                                Text {
                                                    Layout.fillWidth: true
                                                    text: "文本记录示例"
                                                    color: themePreviewButton.preview.textMain
                                                    font.pixelSize: 7
                                                }
                                                Text {
                                                    text: "刚刚"
                                                    color: themePreviewButton.preview.textMuted
                                                    font.pixelSize: 6
                                                }
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Repeater {
                                        model: [
                                            { label: "页面", color: themePreviewButton.preview.page },
                                            { label: "卡片", color: themePreviewButton.preview.surface },
                                            { label: "强调", color: themePreviewButton.preview.accent },
                                            { label: "文字", color: themePreviewButton.preview.textMain }
                                        ]
                                        delegate: RowLayout {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            spacing: 3
                                            Rectangle {
                                                Layout.preferredWidth: 8
                                                Layout.preferredHeight: 8
                                                radius: 4
                                                color: modelData.color
                                                border.color: themePreviewButton.preview.line
                                            }
                                            Text {
                                                text: modelData.label
                                                color: window.textMuted
                                                font.pixelSize: 7
                                            }
                                        }
                                    }
                                }
                            }
                            onClicked: window.selectThemeOption(modelData.id)
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    Item { Layout.fillWidth: true }
                    SettingsActionButton {
                        Layout.preferredWidth: 150
                        themeTokens: window
                        text: "导入主题 JSON…"
                        Accessible.name: "从 JSON 文件导入并命名新主题"
                        onClicked: customThemeFileDialog.open()
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: window.themeImportMessage.length > 0
                    text: window.themeImportMessage
                    color: window.themeImportMessage.indexOf("已添加") === 0 ? window.accent : window.dangerColor
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                visible: Qt.platform.os === "windows"
                heading: "全局快捷键"
                description: "呼出历史面板并控制复制后的粘贴行为"
                iconText: "⌘"
                iconColor: window.accent
                surfaceColor: window.surface
                borderColor: window.line
                titleColor: window.textMain
                subtitleColor: window.textMuted
                ColumnLayout {
                    id: hotkeySection
                    Layout.fillWidth: true
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: "唤醒面板"; color: window.textMain; font.pixelSize: 12; font.weight: Font.Medium }
                            Text {
                                Layout.fillWidth: true
                                text: window.hotkeyCaptureActive ? "按下至少包含一个 Ctrl、Alt、Shift 或 Windows 键的组合。" : "点击当前快捷键后，直接按下要录制的组合键。"
                                color: window.textMuted
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                        Button {
                            id: hotkeyCaptureButton
                            Layout.preferredWidth: 190
                            activeFocusOnTab: true
                            text: window.hotkeyCaptureActive ? "请按下组合键…（Esc 取消）" :
                                  (appearanceSettings.globalHotkey.length > 0 ? appearanceSettings.globalHotkey : "尚未设置")
                            Accessible.name: window.hotkeyCaptureActive ? "正在录制全局快捷键，按 Escape 取消" :
                                             "当前全局快捷键：" + text + "，点击后录制"
                            onClicked: window.beginHotkeyCapture()
                            onActiveFocusChanged: if (!activeFocus) window.cancelHotkeyCapture()
                            Keys.onPressed: function(event) {
                                if (!window.hotkeyCaptureActive)
                                    return
                                event.accepted = true
                                if (event.key === Qt.Key_Escape) {
                                    window.cancelHotkeyCapture()
                                    return
                                }
                                window.applyCapturedHotkey(event.key, event.modifiers)
                            }
                            background: Rectangle {
                                radius: 5
                                color: hotkeyCaptureButton.activeFocus ? window.tileSurface : window.inputSurface
                                border.color: window.hotkeyCaptureActive ? window.accent : window.line
                            }
                            contentItem: Text {
                                text: hotkeyCaptureButton.text
                                color: window.hotkeyCaptureActive ? window.accent : window.textMain
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                font.pixelSize: 12
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: "复制后自动粘贴"; color: window.textMain; font.pixelSize: 12; font.weight: Font.Medium }
                            Text {
                                Layout.fillWidth: true
                                text: "向上一个窗口发送 Ctrl+V，可能替换其中已选中的内容。"
                                color: window.textMuted
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                        ThemeCheckBox {
                            themeTokens: window
                            text: "启用"
                            checked: appearanceSettings.autoPasteIntoPreviousWindow
                            Accessible.name: "复制后自动粘贴到上一个窗口"
                            onToggled: appearanceSettings.autoPasteIntoPreviousWindow = checked
                        }
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                id: retentionSection
                heading: "定时删除"
                description: "保留近期非收藏记录，收藏始终保留"
                iconText: "◷"
                iconColor: window.accent
                surfaceColor: window.surface
                borderColor: window.line
                titleColor: window.textMain
                subtitleColor: window.textMuted
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ThemeCheckBox {
                        id: retentionEnabled
                        themeTokens: window
                        text: "启用自动删除过期记录"
                        checked: appearanceSettings.retentionHours > 0
                        Accessible.name: text
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "保留最近"; color: window.textMain }
                        SpinBox {
                            id: retentionDuration
                            Layout.preferredWidth: 140
                            enabled: retentionEnabled.checked
                            from: 1
                            to: retentionUnit.currentIndex === 1 ? 3650 : 87600
                            value: appearanceSettings.retentionHours > 0
                                   ? (retentionUnit.currentIndex === 1 ? appearanceSettings.retentionHours / 24
                                                                       : appearanceSettings.retentionHours) : 30
                            editable: true
                            palette.text: window.textMain
                            palette.buttonText: window.textMain
                            palette.button: window.inputSurface
                            palette.base: window.inputSurface
                            palette.highlight: window.accent
                            Accessible.name: "非收藏历史保留时长"
                            background: Rectangle { radius: 5; color: window.inputSurface; border.color: parent.activeFocus ? window.accent : window.line }
                        }
                        ComboBox {
                            id: retentionUnit
                            Layout.preferredWidth: 90
                            enabled: retentionEnabled.checked
                            model: ["小时", "天"]
                            currentIndex: appearanceSettings.retentionHours === 0 || appearanceSettings.retentionHours % 24 === 0 ? 1 : 0
                            palette.text: window.textMain
                            palette.buttonText: window.textMain
                            palette.button: window.inputSurface
                            palette.base: window.surface
                            palette.highlight: window.accent
                            Accessible.name: "保留时长单位"
                            background: Rectangle { radius: 5; color: window.inputSurface; border.color: parent.activeFocus ? window.accent : window.line }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "按最近一次采集时间计算；相同内容再次采集会续期。启用或修改期限后立即检查，运行期间每分钟检查一次。删除不可恢复，文件记录的源文件不会被删除。"
                        color: window.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            Layout.fillWidth: true
                            text: appearanceSettings.retentionHours > 0
                                  ? "当前：保留最近 " + appearanceSettings.retentionHours + " 小时的非收藏记录"
                                  : "当前：永久保留，不自动删除"
                            color: window.textMuted
                            wrapMode: Text.Wrap
                            font.pixelSize: 11
                        }
                        SettingsActionButton {
                            id: applyRetentionButton
                            themeTokens: window
                            text: "应用保留设置"
                            Accessible.name: text
                            onClicked: {
                                appearanceSettings.retentionHours = retentionEnabled.checked
                                        ? retentionDuration.value * (retentionUnit.currentIndex === 1 ? 24 : 1) : 0
                                appearanceSettings.sync()
                                window.historyMessage = appearanceSettings.retentionHours > 0
                                        ? "保留设置已生效，过期的非收藏记录将自动删除。" : "已关闭定时删除，历史将永久保留。"
                            }
                        }
                    }
                }
            }

            SettingsSection {
                id: maximumEntriesSection
                Layout.fillWidth: true
                heading: "总条目数量上限"
                description: "默认无限制，开启后清理最旧的非收藏记录"
                iconText: "≡"
                iconColor: window.accent
                surfaceColor: window.surface
                borderColor: window.line
                titleColor: window.textMain
                subtitleColor: window.textMuted
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    ThemeCheckBox {
                        id: maximumEntriesEnabled
                        themeTokens: window
                        text: "启用总条目数量上限"
                        checked: appearanceSettings.maximumEntries > 0
                        Accessible.name: text
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "最多保存"; color: window.textMain }
                        SpinBox {
                            id: maximumEntriesValue
                            Layout.preferredWidth: 160
                            enabled: maximumEntriesEnabled.checked
                            from: 1
                            to: 2147483647
                            value: appearanceSettings.maximumEntries > 0 ? appearanceSettings.maximumEntries : 10000
                            editable: true
                            palette.text: window.textMain
                            palette.buttonText: window.textMain
                            palette.button: window.inputSurface
                            palette.base: window.inputSurface
                            palette.highlight: window.accent
                            Accessible.name: "历史总条目数量上限"
                            background: Rectangle { radius: 5; color: window.inputSurface; border.color: parent.activeFocus ? window.accent : window.line }
                        }
                        Text { text: "条"; color: window.textMain }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "总数包含收藏。启用或修改后检查，采集新记录后继续检查；超过上限时从最旧的非收藏记录开始删除。收藏始终保留，若收藏数超过上限，实际总数仍会超过上限。删除不可恢复。"
                        color: window.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text {
                            Layout.fillWidth: true
                            text: appearanceSettings.maximumEntries > 0
                                  ? "当前：最多 " + appearanceSettings.maximumEntries + " 条，收藏受保护"
                                  : "当前：无数量上限"
                            color: window.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                        SettingsActionButton {
                            id: applyMaximumEntriesButton
                            themeTokens: window
                            text: "应用数量上限"
                            Accessible.name: text
                            onClicked: {
                                appearanceSettings.maximumEntries = maximumEntriesEnabled.checked ? maximumEntriesValue.value : 0
                                appearanceSettings.sync()
                                window.historyMessage = appearanceSettings.maximumEntries > 0
                                        ? "数量上限已生效，超限的非收藏记录将自动清理。" : "已关闭数量上限。"
                            }
                        }
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                heading: "数据存储"
                description: "SQLite 数据库路径与旧版历史迁移"
                iconText: "▤"
                iconColor: window.accent
                surfaceColor: window.surface
                borderColor: window.line
                titleColor: window.textMain
                subtitleColor: window.textMuted
                ColumnLayout {
                    id: storageSection
                    Layout.fillWidth: true
                    spacing: 8
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 7
                        Text {
                            text: "导入旧版历史"
                            color: window.textMain
                            font.pixelSize: 12
                            font.weight: Font.Medium
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "选择旧项目的 SQLite 数据库；程序会读取同级 images 文件夹。旧数据只读，重复导入可安全重试。"
                            color: window.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField {
                                Layout.fillWidth: true
                                readOnly: true
                                text: window.legacyDatabasePath
                                placeholderText: "尚未选择数据库"
                                color: window.textMain
                                placeholderTextColor: window.textMuted
                                Accessible.name: "所选旧版 SQLite 数据库路径"
                                background: Rectangle {
                                    radius: 7
                                    color: window.inputSurface
                                    border.color: parent.activeFocus ? window.accent : window.line
                                }
                            }
                            SettingsActionButton {
                                themeTokens: window
                                text: "选择数据库…"
                                Accessible.name: "选择旧版 SQLite 数据库"
                                onClicked: legacyDatabaseDialog.open()
                            }
                            SettingsActionButton {
                                themeTokens: window
                                text: "导入"
                                primaryAction: true
                                enabled: window.legacyDatabasePath.length > 0
                                onClicked: window.openImportDialog()
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                    Text {
                        Layout.fillWidth: true
                        text: "选择一个可写的空文件夹。应用后重启，程序会在服务启动前复制数据；原目录保留。"
                        color: window.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        color: window.inputSurface
                        border.color: window.line
                        radius: 5
                        Text {
                            anchors.fill: parent
                            anchors.margins: 9
                            text: window.pendingStorageDirectory
                            color: window.textMain
                            font.pixelSize: 11
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideMiddle
                            verticalAlignment: Text.AlignVCenter
                            Accessible.name: "当前选择的数据文件夹"
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Item { Layout.fillWidth: true }
                        SettingsActionButton {
                            id: storageFolderButton
                            themeTokens: window
                            text: "选择文件夹…"
                            Accessible.name: "选择数据存储文件夹"
                            onClicked: storageFolderDialog.open()
                        }
                        SettingsActionButton {
                            themeTokens: window
                            text: "安排迁移"
                            primaryAction: true
                            Accessible.name: "安排数据目录迁移，在下次启动时应用"
                            enabled: window.pendingStorageDirectory !== (appPaths.pendingDataDirectory.length > 0
                                                                          ? appPaths.pendingDataDirectory
                                                                          : appPaths.currentDataDirectory)
                            onClicked: {
                                appearanceSettings.sync()
                                if (appPaths.setDataDirectory(window.pendingStorageDirectory)) {
                                    window.storageError = "迁移已安排。重启后切换目录；本次运行继续使用原目录。"
                                    window.storageErrorIsFailure = false
                                } else {
                                    window.storageError = "无法应用：目标需为空且可写，程序目录也必须可写。"
                                    window.storageErrorIsFailure = true
                                }
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: window.storageError.length > 0
                        text: window.storageError
                        color: window.storageErrorIsFailure ? window.dangerColor : window.textMuted
                        font.pixelSize: 11
                        wrapMode: Text.Wrap
                    }
                }
            }
            }
        }
    }
    }

    }

}
