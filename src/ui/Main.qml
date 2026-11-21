import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Shapes
import QtCore
import QtQuick.Dialogs
import "ThemePalette.js" as ThemePalette

ApplicationWindow {
    id: window

    width: 580
    height: 660
    minimumWidth: resolutionLocked ? lockedWindowWidth : minimumWindowWidth
    maximumWidth: resolutionLocked ? lockedWindowWidth : maximumAllowedWindowWidth
    minimumHeight: resolutionLocked ? lockedWindowHeight : minimumWindowHeight
    maximumHeight: resolutionLocked ? lockedWindowHeight : maximumAllowedWindowHeight
    visible: !(Qt.platform.os === "windows" && Qt.application.arguments.indexOf("--startup") !== -1)
    flags: Qt.Window | Qt.FramelessWindowHint | (window.alwaysOnTop ? Qt.WindowStaysOnTopHint : 0)
    title: "剪切板历史"
    font.family: "Microsoft YaHei UI"
    font.pixelSize: 13
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
        window.commitPendingDelete()
        if (Qt.platform.os === "windows") {
            close.accepted = false
            window.requestClose()
        }
    }
    onActiveChanged: {
        if (!active && window.autoHideOnBlur && !window.alwaysOnTop
                && Qt.platform.os === "windows" && windowsPlatform.trayAvailable
                && !window.settingsPageVisible
                && !hotkeyErrorDialog.visible && !closeChoiceDialog.visible
                && !importThemeNameDialog.visible && !entryContextMenu.visible && !noteEditorDialog.visible
                && !(deleteDialogLoader.item && deleteDialogLoader.item.visible)
                && !(clearDialogLoader.item && clearDialogLoader.item.visible)
                && !(importDialogLoader.item && importDialogLoader.item.visible)) {
            window.commitPendingDelete()
            windowsPlatform.hideToTray()
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
    property int windowSizeWidth: 580
    property int windowSizeHeight: 660
    property bool resolutionLocked: false
    property bool alwaysOnTop: false
    property bool autoHideOnBlur: false
    property bool doubleClickToPaste: false
    property bool suppressNextAutoPaste: false
    property var pendingDeleteEntryId: 0
    property bool geometryReady: false
    readonly property bool compactLayout: width < 540
    readonly property int minimumWindowWidth: 440
    readonly property int minimumWindowHeight: 460
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

    function showStatusMessage(message) {
        window.historyMessage = message
        if (message.length > 0)
            statusMessageTimer.restart()
        else
            statusMessageTimer.stop()
    }

    function commitPendingDelete() {
        pendingDeleteTimer.stop()
        if (window.pendingDeleteEntryId > 0) {
            const targetId = window.pendingDeleteEntryId
            window.pendingDeleteEntryId = 0
            historyModel.removeItemById(targetId)
        }
    }

    function undoPendingDelete() {
        pendingDeleteTimer.stop()
        if (window.pendingDeleteEntryId > 0) {
            window.pendingDeleteEntryId = 0
            window.showStatusMessage("已撤销删除。")
        }
    }

    function quickDelete(entryId, isFavorite) {
        if (isFavorite) {
            window.requestDelete(entryId)
            return
        }
        window.commitPendingDelete()
        if (window.expandedEntryId === entryId) {
            window.expandedEntryId = 0
            window.expandedEntryText = ""
            window.expandedEntryLoadFailed = false
        }
        window.pendingDeleteEntryId = entryId
        pendingDeleteTimer.restart()
    }

    function triggerCopyAt(index, copyOnly) {
        if (index < 0 || index >= clipList.count)
            return
        clipList.currentIndex = index
        window.suppressNextAutoPaste = !!copyOnly
        historyModel.copyItem(index)
    }

    function escapeHtml(text) {
        return text.replace(/&/g, "&amp;")
                   .replace(/</g, "&lt;")
                   .replace(/>/g, "&gt;")
                   .replace(/"/g, "&quot;")
                   .replace(/'/g, "&#39;")
    }

    function highlightedPreview(rawText, query) {
        const trimmed = query ? query.trim() : ""
        if (trimmed.length === 0)
            return window.escapeHtml(rawText)
        const lowerSource = rawText.toLowerCase()
        const lowerQuery = trimmed.toLowerCase()
        let result = ""
        let pos = 0
        let matchIndex = lowerSource.indexOf(lowerQuery, pos)
        const bg = window.themeMode === "浅色" ? "#FEF08A" : window.accent
        const fg = window.themeMode === "浅色" ? "#0F172A" : window.onAccent
        while (matchIndex >= 0) {
            if (matchIndex > pos)
                result += window.escapeHtml(rawText.slice(pos, matchIndex))
            result += "<span style=\"background-color:" + bg + ";color:" + fg + ";\">"
                    + window.escapeHtml(rawText.slice(matchIndex, matchIndex + trimmed.length))
                    + "</span>"
            pos = matchIndex + trimmed.length
            matchIndex = lowerSource.indexOf(lowerQuery, pos)
        }
        if (pos < rawText.length)
            result += window.escapeHtml(rawText.slice(pos))
        return result
    }

    function focusHistoryOnActivate() {
        window.settingsPageVisible = false
        clipList.currentIndex = 0
        clipList.positionViewAtBeginning()
        Qt.callLater(function() {
            searchField.forceActiveFocus()
            searchField.selectAll()
        })
    }

    function performClose(action) {
        window.commitPendingDelete()
        window.saveWindowResolution()
        if (action === "tray" && windowsPlatform.trayAvailable)
            windowsPlatform.hideToTray()
        else
            windowsPlatform.quit()
    }

    function requestClose() {
        window.commitPendingDelete()
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
        const copyOnly = window.suppressNextAutoPaste
        window.suppressNextAutoPaste = false
        window.showStatusMessage(copyOnly ? "已仅复制到剪贴板。" : message)
        if (!copyOnly && Qt.platform.os === "windows" && appearanceSettings.autoPasteIntoPreviousWindow
                && !windowsPlatform.pasteToPreviousWindow())
            window.showStatusMessage("已复制到剪贴板，但未能自动粘贴。")
    }

    function refreshFilteredHistory() {
        window.commitPendingDelete()
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

    function openClearDialog() {
        clearDialogLoader.active = true
        clearDialogLoader.item.open()
    }

    function openNoteEditor(entryId, currentNote) {
        noteEditorDialog.entryId = entryId
        noteEditorDialog.initialNote = currentNote || ""
        noteEditorDialog.open()
    }

    function categoryIconName(category) {
        const names = { "全部": "all", "文本": "text", "链接": "link", "文件": "file", "图片": "image", "收藏": "star" }
        return names[category] || "text"
    }

    function kindIconName(kind) {
        const names = { "文本": "text", "链接": "link", "文件": "file", "图片": "image" }
        return names[kind] || "text"
    }

    function vectorIconPath(name) {
        switch (name) {
        case "all": return "M4 4h6v6H4z M14 4h6v6h-6z M4 14h6v6H4z M14 14h6v6h-6z"
        case "text": return "M4 6h16 M4 12h16 M4 18h10"
        case "link": return "M10 13a5 5 0 0 0 7.54.54l3-3a5 5 0 0 0-7.07-7.07l-1.72 1.71 M14 11a5 5 0 0 0-7.54-.54l-3 3a5 5 0 0 0 7.07 7.07l1.71-1.71"
        case "file": return "M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z M14 2v6h6"
        case "image": return "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z M8.5 8.5a1.5 1.5 0 1 0 0.01 0z M21 15l-5-5L5 21"
        case "star": return "M12 2.5l2.9 5.88 6.5.94-4.7 4.58 1.11 6.47L12 17.3l-5.81 3.07 1.11-6.47-4.7-4.58 6.5-.94z"
        case "search": return "M11 4a7 7 0 1 0 0 14a7 7 0 1 0 0-14z M20 20l-4.35-4.35"
        case "pause": return "M8 5v14 M16 5v14"
        case "play": return "M7 5l12 7-12 7z"
        case "pin": return "M12 17v5 M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16h14v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V6h1V4H8v2h1z"
        case "history": return "M4 6h16 M4 12h16 M4 18h16"
        case "settings": return "M12 8.5a3.5 3.5 0 1 0 0 7a3.5 3.5 0 1 0 0-7z M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 1 1-4 0v-.09a1.65 1.65 0 0 0-1-1.51 1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 1 1 0-4h.09a1.65 1.65 0 0 0 1.51-1 1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 1 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9c.26.6.85 1 1.51 1H21a2 2 0 1 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"
        case "close": return "M6 6l12 12 M18 6L6 18"
        case "copy": return "M9 9h11v11H9z M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1"
        case "external": return "M14 4h6v6 M10 14L20 4 M18 13v6a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h6"
        case "folder": return "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"
        case "trash": return "M4 7h16 M10 11v6 M14 11v6 M6 7l1 13a1 1 0 0 0 1 1h8a1 1 0 0 0 1-1l1-13 M9 7V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v3"
        case "window": return "M3 5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z M3 9h18"
        case "resize": return "M15 3h6v6 M9 21H3v-6 M21 3l-7 7 M3 21l7-7"
        case "palette": return "M12 3a9 9 0 1 0 0 18h1.5a2.5 2.5 0 0 0 0-5H12a2 2 0 0 1 0-4h4a5 5 0 0 0 0-9z"
        case "keyboard": return "M3 6a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z M7 10h.01 M11 10h.01 M15 10h.01 M8 14h8"
        case "clock": return "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M12 7v5l3 2"
        case "layers": return "M12 3l9 4.5-9 4.5-9-4.5z M3 12l9 4.5 9-4.5 M3 16.5L12 21l9-4.5"
        case "database": return "M12 3c4.97 0 9 1.34 9 3s-4.03 3-9 3-9-1.34-9-3 4.03-3 9-3z M3 6v12c0 1.66 4.03 3 9 3s9-1.34 9-3V6 M3 12c0 1.66 4.03 3 9 3s9-1.34 9-3"
        case "note": return "M12 20h9 M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4z"
        default: return "M4 6h16 M4 12h16 M4 18h10"
        }
    }

    component VectorIcon: Item {
        id: vecIcon
        property string iconName: "text"
        property real iconSize: 15
        property color iconColor: window.textMuted
        property bool filled: false
        property real strokeWidth: 1.85
        implicitWidth: iconSize
        implicitHeight: iconSize
        width: iconSize
        height: iconSize

        Shape {
            width: 24
            height: 24
            transformOrigin: Item.TopLeft
            scale: vecIcon.width / 24.0
            preferredRendererType: Shape.CurveRenderer
            layer.enabled: true
            layer.smooth: true
            layer.textureSize: Qt.size(48, 48)
            ShapePath {
                strokeColor: vecIcon.iconColor
                strokeWidth: vecIcon.strokeWidth
                fillColor: vecIcon.filled ? vecIcon.iconColor : "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: window.vectorIconPath(vecIcon.iconName) }
            }
        }
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
            return { page: "#F3F4F6", surface: "#FFFFFF", sidebarSurface: "#FAFAFB", line: "#E5E7EB",
                     textMain: "#111827", textMuted: "#6B7280", accent: "#2563EB", onAccent: "#FFFFFF", inputSurface: "#FFFFFF" }
        if (name === "北极光")
            return { page: "#262B35", surface: "#2F3643", sidebarSurface: "#2A303C", line: "#3D4657",
                     textMain: "#ECEFF4", textMuted: "#AEB7C6", accent: "#88C0D0", onAccent: "#1E232B", inputSurface: "#232831" }
        return { page: "#111315", surface: "#191C20", sidebarSurface: "#15171A", line: "#262A30",
                 textMain: "#EDEDEF", textMuted: "#8A8F98", accent: "#4C8BF5", onAccent: "#FFFFFF", inputSurface: "#16181C" }
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
                    radius: 8
                    color: Qt.rgba(settingsSection.iconColor.r, settingsSection.iconColor.g,
                                   settingsSection.iconColor.b, 0.13)
                    VectorIcon {
                        anchors.centerIn: parent
                        iconName: settingsSection.iconText
                        iconSize: 15
                        iconColor: settingsSection.iconColor
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
    readonly property color page: customMode ? activeThemePalette.page : themeMode === "浅色" ? "#F3F4F6" : nordMode ? "#262B35" : "#111315"
    readonly property color surface: customMode ? activeThemePalette.surface : themeMode === "浅色" ? "#FFFFFF" : nordMode ? "#2F3643" : "#191C20"
    readonly property color sidebarSurface: customMode ? activeThemePalette.sidebarSurface : themeMode === "浅色" ? "#FAFAFB" : nordMode ? "#2A303C" : "#15171A"
    readonly property color rowSurface: customMode ? activeThemePalette.rowSurface : themeMode === "浅色" ? "#FFFFFF" : nordMode ? "#2F3643" : "#181B1F"
    readonly property color line: customMode ? activeThemePalette.line : themeMode === "浅色" ? "#E5E7EB" : nordMode ? "#3D4657" : "#262A30"
    readonly property color textMain: customMode ? activeThemePalette.textMain : themeMode === "浅色" ? "#111827" : nordMode ? "#ECEFF4" : "#EDEDEF"
    readonly property color textMuted: customMode ? activeThemePalette.textMuted : themeMode === "浅色" ? "#6B7280" : nordMode ? "#AEB7C6" : "#8A8F98"
    readonly property color accent: customMode ? activeThemePalette.accent : themeMode === "浅色" ? "#2563EB" : nordMode ? "#88C0D0" : "#4C8BF5"
    readonly property color selectedRow: customMode ? activeThemePalette.selectedRow : themeMode === "浅色" ? "#EFF6FF" : nordMode ? "#394252" : "#1E2530"
    readonly property color selectedText: customMode ? activeThemePalette.selectedText : textMain
    readonly property color inputSurface: customMode ? activeThemePalette.inputSurface : themeMode === "浅色" ? "#FFFFFF" : nordMode ? "#232831" : "#16181C"
    readonly property color tileSurface: customMode ? activeThemePalette.tileSurface : themeMode === "浅色" ? "#EEF0F3" : nordMode ? "#3A4252" : "#22262C"
    readonly property color tagSurface: customMode ? activeThemePalette.tagSurface : themeMode === "浅色" ? "#ECEEF2" : nordMode ? "#353D4C" : "#1D2026"
    readonly property color dangerColor: customMode ? activeThemePalette.danger : themeMode === "浅色" ? "#DC2626" : "#F87171"
    readonly property color dangerSurface: customMode ? activeThemePalette.dangerSurface : themeMode === "浅色" ? "#FEE2E2" : "#2D1B1E"
    readonly property color favoriteColor: customMode ? activeThemePalette.favorite : themeMode === "浅色" ? "#D97706" : "#F5B942"
    readonly property color onAccent: customMode ? activeThemePalette.onAccent : (nordMode ? "#1E232B" : "#FFFFFF")

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
        property alias alwaysOnTop: window.alwaysOnTop
        property alias autoHideOnBlur: window.autoHideOnBlur
        property alias doubleClickToPaste: window.doubleClickToPaste
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
        function onActivationRequested() {
            window.focusHistoryOnActivate()
        }
        function onSettingsRequested() {
            window.openSettings()
        }
        function onLastErrorChanged() {
            if (windowsPlatform.lastError.length > 0)
                window.showStatusMessage(windowsPlatform.lastError)
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
        function onCopyFailed() {
            window.suppressNextAutoPaste = false
            window.showStatusMessage("复制失败，请重试。")
        }
        function onCopySourceMissing() {
            window.suppressNextAutoPaste = false
            window.showStatusMessage("源文件已不存在，无法复制到剪贴板。")
        }
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
        function onRetentionFailed() { window.showStatusMessage("自动清理失败，将在下一次检查时重试。") }
        function onRetentionCompleted(removedCount, cleanupPending) {
            window.showStatusMessage("已自动清理 " + removedCount + " 条非收藏记录。"
                    + (cleanupPending ? "部分图片文件未能清理。" : ""))
        }
        function onMutationFailed() { window.showStatusMessage("操作失败，请重试。") }
        function onMutationCompleted(cleanupPending) {
            window.showStatusMessage(cleanupPending ? "记录已更新，但有图片文件未能清理。" : "历史记录已更新。")
        }
        function onNoteSaveFinished(id, success) {
            window.showStatusMessage(success ? "备注已保存。" : "备注保存失败，请重试。")
        }
        function onLegacyImportFailed() { window.showStatusMessage("旧历史导入失败，请检查数据库和 images 文件夹。") }
        function onLegacyImportCompleted(importedRows, alreadyImportedRows, skippedRows, cleanupPending) {
            window.showStatusMessage("导入完成：新增 " + importedRows + " 条，已存在 "
                    + alreadyImportedRows + " 条，跳过 " + skippedRows + " 条"
                    + (cleanupPending ? "；有图片文件未能清理。" : "。"))
        }
    }

    Connections {
        target: clipboardCapture
        function onRecordFailed(result) { window.showStatusMessage("保存剪贴板内容失败。") }
        function onInitializationFailed(status) { window.showStatusMessage("历史数据库初始化失败。") }
    }

    Timer {
        id: windowSizeSaveTimer
        interval: 250
        repeat: false
        onTriggered: window.saveWindowResolution()
    }

    Timer {
        id: statusMessageTimer
        interval: 3500
        repeat: false
        onTriggered: window.historyMessage = ""
    }

    Timer {
        id: pendingDeleteTimer
        interval: 4500
        repeat: false
        onTriggered: window.commitPendingDelete()
    }

    Shortcut {
        sequence: "Ctrl+F"
        enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive
        onActivated: {
            searchField.forceActiveFocus()
            searchField.selectAll()
        }
    }

    Shortcut {
        sequence: "Ctrl+,"
        enabled: !window.hotkeyCaptureActive
        onActivated: window.settingsPageVisible ? (window.settingsPageVisible = false) : window.openSettings()
    }

    Shortcut {
        sequence: "Escape"
        enabled: !window.hotkeyCaptureActive
                 && !hotkeyErrorDialog.visible && !closeChoiceDialog.visible
                 && !importThemeNameDialog.visible && !entryContextMenu.visible && !noteEditorDialog.visible
                 && !(deleteDialogLoader.item && deleteDialogLoader.item.visible)
                 && !(clearDialogLoader.item && clearDialogLoader.item.visible)
                 && !(importDialogLoader.item && importDialogLoader.item.visible)
        onActivated: {
            if (window.settingsPageVisible) {
                window.settingsPageVisible = false
                return
            }
            if (searchField.text.length > 0) {
                searchField.text = ""
                window.refreshFilteredHistory()
                return
            }
            if (window.expandedEntryId !== 0) {
                window.expandedEntryId = 0
                window.expandedEntryText = ""
                window.expandedEntryLoadFailed = false
                return
            }
            if (Qt.platform.os === "windows" && windowsPlatform.trayAvailable) {
                window.commitPendingDelete()
                windowsPlatform.hideToTray()
            }
        }
    }

    Shortcut { sequence: "Alt+1"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(0, false) }
    Shortcut { sequence: "Alt+2"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(1, false) }
    Shortcut { sequence: "Alt+3"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(2, false) }
    Shortcut { sequence: "Alt+4"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(3, false) }
    Shortcut { sequence: "Alt+5"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(4, false) }
    Shortcut { sequence: "Alt+6"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(5, false) }
    Shortcut { sequence: "Alt+7"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(6, false) }
    Shortcut { sequence: "Alt+8"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(7, false) }
    Shortcut { sequence: "Alt+9"; enabled: !window.settingsPageVisible && !window.hotkeyCaptureActive; onActivated: window.triggerCopyAt(8, false) }

    Menu {
        id: entryContextMenu
        property int targetIndex: -1
        property var targetEntryId: 0
        property string targetKind: ""
        property string targetText: ""
        property string targetNote: ""
        property bool targetFavorite: false
        property bool targetCanExpand: false
        property bool targetExpanded: false
        property bool targetSourceMissing: false

        background: Rectangle {
            implicitWidth: 196
            radius: 10
            color: window.surface
            border.color: window.line
        }

        component ContextMenuItem: MenuItem {
            id: ctxItem
            implicitHeight: 34
            contentItem: Text {
                text: ctxItem.text
                color: ctxItem.enabled ? (ctxItem.highlighted ? window.accent : window.textMain) : window.textMuted
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                leftPadding: 10
                rightPadding: 10
            }
            background: Rectangle {
                radius: 6
                color: ctxItem.highlighted ? window.selectedRow : "transparent"
            }
        }

        ContextMenuItem {
            text: appearanceSettings.autoPasteIntoPreviousWindow ? "复制并粘贴　(Enter)" : "复制到剪贴板　(Enter)"
            enabled: !entryContextMenu.targetSourceMissing
            onTriggered: window.triggerCopyAt(entryContextMenu.targetIndex, false)
        }
        ContextMenuItem {
            text: "仅复制到剪贴板　(Shift+Enter)"
            visible: appearanceSettings.autoPasteIntoPreviousWindow
            height: visible ? implicitHeight : 0
            enabled: !entryContextMenu.targetSourceMissing
            onTriggered: window.triggerCopyAt(entryContextMenu.targetIndex, true)
        }
        ContextMenuItem {
            text: entryContextMenu.targetExpanded ? "收起完整内容　(Space)" : "展开完整内容　(Space)"
            visible: entryContextMenu.targetCanExpand || entryContextMenu.targetExpanded
            height: visible ? implicitHeight : 0
            onTriggered: window.toggleEntry(entryContextMenu.targetEntryId, entryContextMenu.targetKind)
        }
        ContextMenuItem {
            text: "在浏览器中打开"
            visible: entryContextMenu.targetKind === "链接" && Qt.platform.os === "windows"
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (!windowsPlatform.openExternalLink(entryContextMenu.targetText))
                    window.showStatusMessage("无法打开该链接。")
            }
        }
        ContextMenuItem {
            text: "在资源管理器中显示"
            visible: entryContextMenu.targetKind === "文件" && !entryContextMenu.targetSourceMissing && Qt.platform.os === "windows"
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (!windowsPlatform.revealFileInExplorer(entryContextMenu.targetText))
                    window.showStatusMessage("无法在资源管理器中定位该文件。")
            }
        }
        ContextMenuItem {
            text: "仅复制路径文本"
            visible: entryContextMenu.targetKind === "文件" && Qt.platform.os === "windows"
            height: visible ? implicitHeight : 0
            onTriggered: {
                if (windowsPlatform.copyPlainText(entryContextMenu.targetText))
                    window.showStatusMessage("已复制文件路径文本。")
            }
        }
        ContextMenuItem {
            text: entryContextMenu.targetNote.length > 0 ? "编辑备注　(F2)" : "添加备注　(F2)"
            onTriggered: window.openNoteEditor(entryContextMenu.targetEntryId, entryContextMenu.targetNote)
        }
        ContextMenuItem {
            text: entryContextMenu.targetFavorite ? "取消收藏" : "加入收藏"
            onTriggered: historyModel.setFavorite(entryContextMenu.targetIndex, !entryContextMenu.targetFavorite)
        }
        ContextMenuItem {
            text: "删除此记录　(Del)"
            onTriggered: window.quickDelete(entryContextMenu.targetEntryId, entryContextMenu.targetFavorite)
        }
    }

    Item {
        id: contentArea
        anchors.fill: parent
    }

    Rectangle {
        z: 10
        anchors.fill: parent
        color: "transparent"
        border.color: window.line
        border.width: 1
    }

    Rectangle {
        id: headerBar
        z: 2
        x: contentArea.x
        y: contentArea.y
        width: contentArea.width
        height: 46
        color: window.sidebarSurface
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: window.line }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 10
            spacing: 4

            Rectangle {
                Layout.preferredWidth: 26
                Layout.preferredHeight: 26
                radius: 7
                color: "transparent"
                clip: true
                Image {
                    anchors.fill: parent
                    source: "qrc:/icons/app-icon.png"
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                }
                Accessible.role: Accessible.Graphic
                Accessible.name: "剪贴板历史应用图标"
            }

            Text {
                Layout.leftMargin: 4
                text: window.settingsPageVisible ? "偏好设置" : "剪贴板历史"
                color: window.textMain
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Accessible.role: Accessible.Heading
            }

            Rectangle {
                visible: clipboardCapture.paused
                Layout.leftMargin: 4
                implicitWidth: pausedChipText.implicitWidth + 12
                implicitHeight: 20
                radius: 10
                color: Qt.rgba(window.favoriteColor.r, window.favoriteColor.g, window.favoriteColor.b, 0.16)
                Text {
                    id: pausedChipText
                    anchors.centerIn: parent
                    text: "已暂停"
                    color: window.favoriteColor
                    font.pixelSize: 10
                    font.weight: Font.Medium
                }
            }

            Item { Layout.fillWidth: true }

            ToolButton {
                id: pauseCaptureButton
                implicitWidth: 30
                implicitHeight: 30
                checkable: true
                checked: clipboardCapture.paused
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: clipboardCapture.paused ? "恢复剪贴板监听" : "暂停剪贴板监听"
                Accessible.name: clipboardCapture.paused ? "恢复剪贴板监听" : "暂停剪贴板监听"
                contentItem: Item {
                    VectorIcon {
                        anchors.centerIn: parent
                        iconName: clipboardCapture.paused ? "play" : "pause"
                        iconSize: 14
                        iconColor: clipboardCapture.paused ? window.favoriteColor
                                   : (pauseCaptureButton.hovered ? window.textMain : window.textMuted)
                        filled: clipboardCapture.paused
                    }
                }
                background: Rectangle {
                    radius: 7
                    color: clipboardCapture.paused
                           ? Qt.rgba(window.favoriteColor.r, window.favoriteColor.g, window.favoriteColor.b, 0.14)
                           : (pauseCaptureButton.hovered ? window.tileSurface : "transparent")
                }
                onClicked: {
                    clipboardCapture.togglePaused()
                    window.showStatusMessage(clipboardCapture.paused ? "已暂停剪贴板监听。" : "已恢复剪贴板监听。")
                }
            }

            ToolButton {
                id: pinWindowButton
                implicitWidth: 30
                implicitHeight: 30
                checkable: true
                checked: window.alwaysOnTop
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: window.alwaysOnTop ? "取消窗口置顶" : "窗口置顶"
                Accessible.name: window.alwaysOnTop ? "取消窗口置顶" : "窗口置顶"
                contentItem: Item {
                    VectorIcon {
                        anchors.centerIn: parent
                        iconName: "pin"
                        iconSize: 14
                        iconColor: window.alwaysOnTop ? window.accent
                                   : (pinWindowButton.hovered ? window.textMain : window.textMuted)
                    }
                }
                background: Rectangle {
                    radius: 7
                    color: window.alwaysOnTop ? window.selectedRow :
                           pinWindowButton.hovered ? window.tileSurface : "transparent"
                }
                onClicked: {
                    window.alwaysOnTop = !window.alwaysOnTop
                    appearanceSettings.sync()
                    window.showStatusMessage(window.alwaysOnTop ? "窗口已置顶。" : "已取消窗口置顶。")
                }
            }

            ToolButton {
                id: historyNavButton
                implicitWidth: 30
                implicitHeight: 30
                checkable: true
                checked: !window.settingsPageVisible
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "历史列表"
                Accessible.name: "历史列表"
                contentItem: Item {
                    VectorIcon {
                        anchors.centerIn: parent
                        iconName: "history"
                        iconSize: 15
                        iconColor: historyNavButton.checked ? window.accent
                                   : (historyNavButton.hovered ? window.textMain : window.textMuted)
                    }
                }
                background: Rectangle {
                    radius: 7
                    color: historyNavButton.checked ? window.selectedRow :
                           historyNavButton.hovered ? window.tileSurface : "transparent"
                }
                onClicked: window.settingsPageVisible = false
            }

            ToolButton {
                id: settingsButton
                objectName: "settingsButton"
                implicitWidth: 30
                implicitHeight: 30
                checkable: true
                checked: window.settingsPageVisible
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "偏好设置 (Ctrl+,)"
                Accessible.name: "偏好设置"
                contentItem: Item {
                    VectorIcon {
                        anchors.centerIn: parent
                        iconName: "settings"
                        iconSize: 15
                        iconColor: settingsButton.checked ? window.accent
                                   : (settingsButton.hovered ? window.textMain : window.textMuted)
                    }
                }
                background: Rectangle {
                    radius: 7
                    color: settingsButton.checked ? window.selectedRow :
                           settingsButton.hovered ? window.tileSurface : "transparent"
                }
                onClicked: window.openSettings()
            }

            ToolButton {
                id: headerHotkeyBadge
                visible: Qt.platform.os === "windows" && !window.compactLayout
                Layout.leftMargin: 4
                implicitWidth: headerHotkeyText.implicitWidth + 14
                implicitHeight: 24
                padding: 0
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "点击修改全局快捷键"
                Accessible.name: "全局快捷键 " + headerHotkeyText.text + "，点击修改"
                contentItem: Text {
                    id: headerHotkeyText
                    text: windowsPlatform.globalHotkey.length > 0 ? windowsPlatform.globalHotkey : "未设置快捷键"
                    color: headerHotkeyBadge.hovered ? window.accent : window.textMuted
                    font.family: "Consolas"
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 6
                    color: headerHotkeyBadge.hovered ? window.selectedRow : window.tagSurface
                }
                onClicked: window.openHotkeySettings()
            }

            ToolButton {
                id: topCloseButton
                Layout.leftMargin: 2
                implicitWidth: 30
                implicitHeight: 30
                ToolTip.visible: hovered
                ToolTip.delay: 400
                ToolTip.text: "关闭窗口"
                Accessible.name: "关闭剪贴板历史"
                contentItem: Item {
                    VectorIcon {
                        anchors.centerIn: parent
                        iconName: "close"
                        iconSize: 14
                        iconColor: topCloseButton.hovered ? window.dangerColor : window.textMuted
                    }
                }
                background: Rectangle {
                    radius: 7
                    color: topCloseButton.hovered ? window.dangerSurface : "transparent"
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

        Rectangle {
            visible: clipboardCapture.paused
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 34 : 0
            color: Qt.rgba(window.favoriteColor.r, window.favoriteColor.g, window.favoriteColor.b, 0.14)
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom; height: 1; color: window.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: "剪贴板监听已暂停，新复制的内容不会被记录。"
                    color: window.textMain
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                ToolButton {
                    implicitHeight: 24
                    padding: 6
                    contentItem: Text {
                        text: "恢复监听"
                        color: window.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? window.selectedRow : "transparent"
                    }
                    onClicked: {
                        clipboardCapture.setPaused(false)
                        window.showStatusMessage("已恢复剪贴板监听。")
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: window.compactLayout ? 12 : 16
            Layout.rightMargin: window.compactLayout ? 12 : 16
            Layout.topMargin: 12
            Layout.bottomMargin: 10
            spacing: 10

            TextField {
                id: searchField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: "搜索历史记录… (Ctrl+F)"
                onTextEdited: searchTimer.restart()
                leftPadding: 38
                rightPadding: 48
                font.pixelSize: 13
                color: window.textMain
                placeholderTextColor: window.textMuted
                selectionColor: window.accent
                selectedTextColor: window.onAccent
                Accessible.name: "搜索历史记录"
                Keys.onDownPressed: {
                    if (clipList.count > 0) {
                        clipList.forceActiveFocus()
                        if (clipList.currentIndex < 0)
                            clipList.currentIndex = 0
                        else if (clipList.currentIndex + 1 < clipList.count)
                            clipList.currentIndex += 1
                    }
                }
                Keys.onReturnPressed: function(event) {
                    if (clipList.count > 0)
                        window.triggerCopyAt(clipList.currentIndex >= 0 ? clipList.currentIndex : 0,
                                             (event.modifiers & Qt.ShiftModifier) !== 0)
                }
                Keys.onEnterPressed: function(event) {
                    if (clipList.count > 0)
                        window.triggerCopyAt(clipList.currentIndex >= 0 ? clipList.currentIndex : 0,
                                             (event.modifiers & Qt.ShiftModifier) !== 0)
                }
                Keys.onEscapePressed: {
                    if (text.length > 0) {
                        text = ""
                        window.refreshFilteredHistory()
                    } else if (Qt.platform.os === "windows" && windowsPlatform.trayAvailable) {
                        window.commitPendingDelete()
                        windowsPlatform.hideToTray()
                    }
                }
                background: Rectangle {
                    radius: 10
                    color: window.inputSurface
                    border.color: searchField.activeFocus ? window.accent : window.line
                    border.width: searchField.activeFocus ? 1.5 : 1
                    VectorIcon {
                        anchors.left: parent.left
                        anchors.leftMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        iconName: "search"
                        iconSize: 15
                        iconColor: searchField.activeFocus ? window.accent : window.textMuted
                    }
                    ToolButton {
                        visible: searchField.text.length > 0
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: 24
                        implicitHeight: 24
                        padding: 0
                        Accessible.name: "清空搜索关键词"
                        contentItem: Item {
                            VectorIcon {
                                anchors.centerIn: parent
                                iconName: "close"
                                iconSize: 12
                                iconColor: parent.parent.hovered ? window.textMain : window.textMuted
                            }
                        }
                        background: Rectangle {
                            radius: 6
                            color: parent.hovered ? window.tileSurface : "transparent"
                        }
                        onClicked: {
                            searchField.text = ""
                            window.refreshFilteredHistory()
                            searchField.forceActiveFocus()
                        }
                    }
                    Rectangle {
                        visible: searchField.text.length === 0
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: escHint.implicitWidth + 10
                        implicitHeight: 20
                        radius: 5
                        color: window.tagSurface
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

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: 9
                color: window.tagSurface

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: 3
                    columns: 6
                    columnSpacing: 2
                    rowSpacing: 2
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
                            Layout.fillHeight: true
                            padding: 0
                            Accessible.name: modelData + (checked ? "，当前分类" : "")
                            Accessible.description: "筛选" + modelData + "记录"

                            contentItem: Item {
                                implicitWidth: categoryContentGroup.implicitWidth
                                implicitHeight: categoryContentGroup.implicitHeight
                                Row {
                                    id: categoryContentGroup
                                    anchors.centerIn: parent
                                    spacing: window.compactLayout ? 4 : 5
                                    VectorIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        iconName: window.categoryIconName(categoryButton.modelData)
                                        iconSize: 13
                                        iconColor: categoryButton.checked ? window.accent : window.textMuted
                                        filled: categoryButton.modelData === "收藏" && categoryButton.checked
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: categoryButton.modelData
                                        color: categoryButton.checked ? window.textMain : window.textMuted
                                        font.pixelSize: 12
                                        font.weight: categoryButton.checked ? Font.DemiBold : Font.Normal
                                    }
                                }
                            }

                            background: Rectangle {
                                radius: 7
                                color: categoryButton.checked ? window.surface
                                       : (categoryButton.hovered ? Qt.rgba(window.textMain.r, window.textMain.g, window.textMain.b, 0.05) : "transparent")
                                border.color: categoryButton.checked ? window.line
                                              : (categoryButton.activeFocus ? window.accent : "transparent")
                                border.width: categoryButton.checked || categoryButton.activeFocus ? 1 : 0
                            }

                            onClicked: {
                                window.selectedCategory = modelData
                                window.refreshFilteredHistory()
                            }
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
            interval: 180
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
            leftMargin: window.compactLayout ? 12 : 16
            rightMargin: window.compactLayout ? 12 : 16
            Layout.topMargin: 8
            Layout.bottomMargin: 8
            spacing: 6
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
            Keys.onUpPressed: function(event) {
                if (currentIndex <= 0) {
                    searchField.forceActiveFocus()
                    event.accepted = true
                } else {
                    decrementCurrentIndex()
                    event.accepted = true
                }
            }
            Keys.onReturnPressed: function(event) {
                window.triggerCopyAt(currentIndex, (event.modifiers & Qt.ShiftModifier) !== 0)
            }
            Keys.onEnterPressed: function(event) {
                window.triggerCopyAt(currentIndex, (event.modifiers & Qt.ShiftModifier) !== 0)
            }
            Keys.onSpacePressed: {
                if (currentItem && (currentItem.canExpand || currentItem.expanded))
                    window.toggleEntry(currentItem.entryId, currentItem.kind)
            }
            Keys.onDeletePressed: {
                if (currentItem)
                    window.quickDelete(currentItem.entryId, currentItem.favorite)
            }
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_F2 && currentItem) {
                    event.accepted = true
                    window.openNoteEditor(currentItem.entryId, currentItem.note)
                }
            }

            delegate: Rectangle {
                id: entryCard
                required property string clipText
                required property string kind
                required property url imageUrl
                required property string time
                required property string uses
                required property string symbol
                required property bool favorite
                required property bool expandable
                required property bool sourceMissing
                required property string note
                required property int index
                required property var entryId

                readonly property bool expanded: window.expandedEntryId === entryId
                readonly property bool canExpand: expandable || entryTitle.truncated || kind === "图片"
                readonly property bool pendingRemoved: window.pendingDeleteEntryId === entryId

                visible: !pendingRemoved
                width: clipList.width - clipList.leftMargin - clipList.rightMargin
                height: pendingRemoved ? 0 : entryLayout.implicitHeight + 16
                color: "transparent"
                border.color: "transparent"
                Accessible.role: Accessible.Button
                Accessible.name: "复制 " + kind + "：" + clipText
                focus: ListView.isCurrentItem

                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: ListView.isCurrentItem ? window.selectedRow
                           : (cardHover.hovered ? window.tileSurface : window.rowSurface)
                    border.color: ListView.isCurrentItem
                                  ? Qt.rgba(window.accent.r, window.accent.g, window.accent.b, 0.45)
                                  : (cardHover.hovered ? window.line : Qt.rgba(window.line.r, window.line.g, window.line.b, 0.7))
                    border.width: 1

                    Rectangle {
                        visible: entryCard.ListView.isCurrentItem
                        anchors.left: parent.left
                        anchors.leftMargin: 3
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: Math.max(16, parent.height - 20)
                        radius: 2
                        color: window.accent
                    }
                }
                HoverHandler { id: cardHover }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        clipList.currentIndex = index
                        if (!appearanceSettings.doubleClickToPaste && (!expanded || kind === "图片"))
                            window.triggerCopyAt(index, false)
                    }
                    onDoubleTapped: {
                        clipList.currentIndex = index
                        if (appearanceSettings.doubleClickToPaste && (!expanded || kind === "图片"))
                            window.triggerCopyAt(index, false)
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: function(mouse) {
                        clipList.currentIndex = index
                        entryContextMenu.targetIndex = index
                        entryContextMenu.targetEntryId = entryId
                        entryContextMenu.targetKind = kind
                        entryContextMenu.targetText = clipText
                        entryContextMenu.targetNote = note
                        entryContextMenu.targetFavorite = favorite
                        entryContextMenu.targetCanExpand = canExpand
                        entryContextMenu.targetExpanded = expanded
                        entryContextMenu.targetSourceMissing = sourceMissing
                        entryContextMenu.popup()
                    }
                }

                ColumnLayout {
                    id: entryLayout
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 10
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 6

                    RowLayout {
                        id: entryHeader
                        visible: !expanded
                        Layout.fillWidth: true
                        spacing: 10

                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            Layout.alignment: Qt.AlignTop
                            Layout.topMargin: 1
                            radius: 8
                            color: window.kindTint(kind)
                            VectorIcon {
                                anchors.centerIn: parent
                                iconName: window.kindIconName(kind)
                                iconSize: 14
                                iconColor: window.kindAccent(kind)
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                Text {
                                    id: entryTitle
                                    Layout.fillWidth: true
                                    text: searchField.text.trim().length > 0
                                          ? window.highlightedPreview(clipText, searchField.text)
                                          : clipText
                                    textFormat: searchField.text.trim().length > 0 ? Text.RichText : Text.PlainText
                                    color: ListView.isCurrentItem ? window.selectedText : window.textMain
                                    font.pixelSize: 13
                                    font.weight: Font.Normal
                                    lineHeight: 1.44
                                    lineHeightMode: Text.ProportionalHeight
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    elide: Text.ElideRight
                                    maximumLineCount: 2
                                }
                                Text {
                                    visible: index < 9 && !window.compactLayout
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 2
                                    text: String(index + 1)
                                    color: window.textMuted
                                    opacity: 0.45
                                    font.family: "Consolas"
                                    font.pixelSize: 11
                                }
                                Rectangle {
                                    Layout.alignment: Qt.AlignTop
                                    Layout.topMargin: 1
                                    Layout.preferredWidth: kindLabel.implicitWidth + 10
                                    Layout.preferredHeight: 18
                                    radius: 5
                                    color: window.kindTint(kind)
                                    Text {
                                        id: kindLabel
                                        anchors.centerIn: parent
                                        text: kind
                                        color: window.kindAccent(kind)
                                        font.pixelSize: 10
                                        font.weight: Font.Medium
                                    }
                                }
                            }
                            RowLayout {
                                visible: uses.length > 0 || sourceMissing
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
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            Layout.alignment: Qt.AlignTop
                            radius: 8
                            color: window.kindTint(kind)
                            VectorIcon {
                                anchors.centerIn: parent
                                iconName: window.kindIconName(kind)
                                iconSize: 14
                                iconColor: window.kindAccent(kind)
                            }
                            TapHandler {
                                onTapped: window.triggerCopyAt(index, false)
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
                            TextEdit {
                                id: expandedText
                                width: expandedTextScroll.availableWidth
                                readOnly: true
                                selectByMouse: true
                                padding: 0
                                text: window.expandedEntryLoadFailed ? "完整内容读取失败，请收起后重试。" :
                                      (window.expandedEntryText.length > 0 ? window.expandedEntryText : "正在加载完整内容…")
                                textFormat: TextEdit.PlainText
                                color: entryTitle.color
                                font: entryTitle.font
                                selectionColor: window.accent
                                selectedTextColor: window.onAccent
                                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
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
                            onStatusChanged: if (status === Image.Error) window.showStatusMessage("图片文件已丢失，无法预览。")
                        }
                        Text {
                            anchors.centerIn: parent
                            visible: imagePreview.status === Image.Error
                            text: "图片文件已丢失，无法预览。"
                            color: window.textMuted
                            font.pixelSize: 11
                        }
                    }

                    Rectangle {
                        id: notePreviewBanner
                        visible: note.length > 0
                        Layout.fillWidth: true
                        Layout.leftMargin: 38
                        Layout.rightMargin: 4
                        implicitHeight: notePreviewRow.implicitHeight + 8
                        radius: 6
                        color: noteBannerHover.hovered
                               ? Qt.rgba(window.textMain.r, window.textMain.g, window.textMain.b, 0.05)
                               : "transparent"

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.topMargin: 4
                            anchors.bottomMargin: 4
                            width: 2
                            radius: 1
                            color: Qt.rgba(window.accent.r, window.accent.g, window.accent.b,
                                           noteBannerHover.hovered ? 0.85 : 0.45)
                        }

                        RowLayout {
                            id: notePreviewRow
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            anchors.topMargin: 4
                            anchors.bottomMargin: 4
                            spacing: 6
                            VectorIcon {
                                Layout.alignment: Qt.AlignTop
                                Layout.topMargin: 2
                                iconName: "note"
                                iconSize: 12
                                iconColor: noteBannerHover.hovered ? window.accent : window.textMuted
                            }
                            Text {
                                Layout.fillWidth: true
                                text: note
                                color: noteBannerHover.hovered ? window.textMain : window.textMuted
                                font.pixelSize: 12
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                maximumLineCount: expanded ? 12 : 2
                                elide: Text.ElideRight
                                lineHeight: 1.4
                                lineHeightMode: Text.ProportionalHeight
                            }
                        }
                        HoverHandler {
                            id: noteBannerHover
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            onTapped: window.openNoteEditor(entryId, note)
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        RowLayout {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 6
                            Rectangle {
                                visible: expanded
                                width: footerKindLabel.implicitWidth + 12
                                height: 18
                                radius: 5
                                color: window.kindTint(kind)
                                Text {
                                    id: footerKindLabel
                                    anchors.centerIn: parent
                                    text: kind
                                    color: window.kindAccent(kind)
                                    font.pixelSize: 10
                                }
                            }
                            Text {
                                visible: uses.length > 0 && expanded
                                text: uses
                                color: window.textMuted
                                font.pixelSize: 10
                            }
                            Text {
                                visible: sourceMissing && expanded
                                text: "源文件已不存在"
                                color: window.dangerColor
                                font.pixelSize: 10
                                Accessible.role: Accessible.AlertMessage
                            }
                        }
                        ToolButton {
                            id: expandToggleButton
                            anchors.centerIn: parent
                            visible: expanded || canExpand
                            Accessible.name: expanded ? "收起完整内容" : "展开完整内容"
                            implicitWidth: 64
                            implicitHeight: 24
                            contentItem: Text {
                                text: expanded ? "收起" : "展开"
                                color: expandToggleButton.hovered ? window.accent : window.textMuted
                                font.pixelSize: 11
                                font.weight: Font.Medium
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                wrapMode: Text.NoWrap
                            }
                            background: Rectangle {
                                radius: 6
                                color: expandToggleButton.hovered ? window.tagSurface : "transparent"
                                border.color: expandToggleButton.activeFocus ? window.accent : "transparent"
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
                                Layout.rightMargin: 4
                            }
                            ToolButton {
                                visible: kind === "链接" && Qt.platform.os === "windows" && (cardHover.hovered || entryCard.ListView.isCurrentItem)
                                implicitWidth: 24
                                implicitHeight: 24
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: "在浏览器中打开"
                                Accessible.name: "在浏览器中打开链接"
                                contentItem: Item {
                                    VectorIcon {
                                        anchors.centerIn: parent
                                        iconName: "external"
                                        iconSize: 13
                                        iconColor: parent.parent.hovered ? window.accent : window.textMuted
                                    }
                                }
                                background: Rectangle { radius: 6; color: parent.hovered ? window.tagSurface : "transparent" }
                                onClicked: {
                                    if (!windowsPlatform.openExternalLink(clipText))
                                        window.showStatusMessage("无法打开该链接。")
                                }
                            }
                            ToolButton {
                                visible: kind === "文件" && !sourceMissing && Qt.platform.os === "windows" && (cardHover.hovered || entryCard.ListView.isCurrentItem)
                                implicitWidth: 24
                                implicitHeight: 24
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: "在资源管理器中显示"
                                Accessible.name: "在资源管理器中显示文件"
                                contentItem: Item {
                                    VectorIcon {
                                        anchors.centerIn: parent
                                        iconName: "folder"
                                        iconSize: 13
                                        iconColor: parent.parent.hovered ? window.accent : window.textMuted
                                    }
                                }
                                background: Rectangle { radius: 6; color: parent.hovered ? window.tagSurface : "transparent" }
                                onClicked: {
                                    if (!windowsPlatform.revealFileInExplorer(clipText))
                                        window.showStatusMessage("无法在资源管理器中定位该文件。")
                                }
                            }
                            ToolButton {
                                visible: appearanceSettings.autoPasteIntoPreviousWindow && !sourceMissing && (cardHover.hovered || entryCard.ListView.isCurrentItem)
                                implicitWidth: 24
                                implicitHeight: 24
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: "仅复制到剪贴板 (Shift+Enter)"
                                Accessible.name: "仅复制到剪贴板"
                                contentItem: Item {
                                    VectorIcon {
                                        anchors.centerIn: parent
                                        iconName: "copy"
                                        iconSize: 13
                                        iconColor: parent.parent.hovered ? window.accent : window.textMuted
                                    }
                                }
                                background: Rectangle { radius: 6; color: parent.hovered ? window.tagSurface : "transparent" }
                                onClicked: window.triggerCopyAt(index, true)
                            }
                            ToolButton {
                                id: noteButton
                                visible: note.length > 0 || cardHover.hovered || entryCard.ListView.isCurrentItem
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: note.length > 0 ? "编辑备注 (F2)" : "添加备注 (F2)"
                                Accessible.name: note.length > 0 ? "编辑备注" : "添加备注"
                                implicitWidth: 24
                                implicitHeight: 24
                                contentItem: Item {
                                    VectorIcon {
                                        anchors.centerIn: parent
                                        iconName: "note"
                                        iconSize: 13
                                        iconColor: note.length > 0 ? window.accent : (noteButton.hovered ? window.textMain : window.textMuted)
                                    }
                                }
                                background: Rectangle { radius: 6; color: noteButton.hovered ? window.tagSurface : "transparent" }
                                onClicked: window.openNoteEditor(entryId, note)
                            }
                            ToolButton {
                                id: favoriteButton
                                visible: favorite || cardHover.hovered || entryCard.ListView.isCurrentItem
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: favorite ? "取消收藏" : "收藏"
                                Accessible.name: favorite ? "取消收藏" : "收藏"
                                implicitWidth: 24
                                implicitHeight: 24
                                contentItem: Item {
                                    VectorIcon {
                                        anchors.centerIn: parent
                                        iconName: "star"
                                        iconSize: 13
                                        iconColor: favorite ? window.favoriteColor : (favoriteButton.hovered ? window.textMain : window.textMuted)
                                        filled: favorite
                                    }
                                }
                                background: Rectangle { radius: 6; color: favoriteButton.hovered ? window.tagSurface : "transparent" }
                                onClicked: historyModel.setFavorite(index, !favorite)
                            }
                            ToolButton {
                                id: deleteButton
                                visible: cardHover.hovered || entryCard.ListView.isCurrentItem
                                ToolTip.visible: hovered
                                ToolTip.delay: 400
                                ToolTip.text: favorite ? "删除收藏记录（需确认）" : "删除记录 (Del)"
                                Accessible.name: "删除此记录"
                                implicitWidth: 24
                                implicitHeight: 24
                                contentItem: Item {
                                    VectorIcon {
                                        anchors.centerIn: parent
                                        iconName: "trash"
                                        iconSize: 13
                                        iconColor: deleteButton.hovered ? window.dangerColor : window.textMuted
                                    }
                                }
                                background: Rectangle { radius: 6; color: deleteButton.hovered ? window.dangerSurface : "transparent" }
                                onClicked: window.quickDelete(entryId, favorite)
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
            z: 2
            visible: window.pendingDeleteEntryId > 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 36 : 0
            color: window.surface
            Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: window.line }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 10
                Text {
                    Layout.fillWidth: true
                    text: "已删除 1 条记录"
                    color: window.textMain
                    font.pixelSize: 11
                }
                ToolButton {
                    implicitHeight: 24
                    padding: 8
                    contentItem: Text {
                        text: "撤销"
                        color: window.accent
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? window.selectedRow : "transparent"
                    }
                    onClicked: window.undoPendingDelete()
                }
            }
        }

        Rectangle {
            z: 2
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: window.sidebarSurface
            border.color: window.line
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                Text {
                    Layout.fillWidth: true
                    text: window.historyMessage.length > 0 ? window.historyMessage :
                          historyModel.totalCount < 0 ? "正在读取记录总数…" : "共 " + historyModel.totalCount + " 条"
                    color: window.historyMessage.length > 0 ? window.accent : window.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Text {
                    text: window.compactLayout
                          ? "Enter 粘贴 · Esc 隐藏"
                          : "Enter 粘贴 · Shift+Enter 仅复制 · Alt+1~9 快贴 · Esc 隐藏"
                    color: window.textMuted
                    font.pixelSize: 10
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
        id: noteEditorDialog
        property var entryId: 0
        property string initialNote: ""
        width: Math.min(window.width - 44, 420)
        anchors.centerIn: parent
        modal: true
        padding: 0
        onOpened: {
            noteEditorField.text = initialNote
            noteEditorField.forceActiveFocus()
            noteEditorField.cursorPosition = noteEditorField.length
        }
        Overlay.modal: Rectangle {
            color: Qt.rgba(0, 0, 0, window.themeMode === "浅色" ? 0.28 : 0.52)
        }
        background: Rectangle {
            color: window.surface
            border.color: window.line
            border.width: 1
            radius: 12
        }
        contentItem: ColumnLayout {
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 12
                Layout.topMargin: 14
                Layout.bottomMargin: 10
                spacing: 8
                VectorIcon {
                    iconName: "note"
                    iconSize: 15
                    iconColor: window.accent
                }
                Text {
                    Layout.fillWidth: true
                    text: noteEditorDialog.initialNote.length > 0 ? "编辑备注" : "添加备注"
                    color: window.textMain
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                }
                ToolButton {
                    implicitWidth: 28
                    implicitHeight: 28
                    Accessible.name: "关闭备注窗口"
                    contentItem: Item {
                        VectorIcon {
                            anchors.centerIn: parent
                            iconName: "close"
                            iconSize: 13
                            iconColor: parent.parent.hovered ? window.textMain : window.textMuted
                        }
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? window.tileSurface : "transparent"
                    }
                    onClicked: noteEditorDialog.reject()
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.bottomMargin: 12
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 116
                    radius: 9
                    color: window.inputSurface
                    border.color: noteEditorField.activeFocus ? window.accent : window.line
                    border.width: noteEditorField.activeFocus ? 1.5 : 1

                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 2
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded
                        background: null
                        TextArea {
                            id: noteEditorField
                            placeholderText: "写点备忘说明，方便日后搜索和识别…"
                            color: window.textMain
                            placeholderTextColor: window.textMuted
                            selectionColor: window.accent
                            selectedTextColor: window.onAccent
                            font.pixelSize: 13
                            wrapMode: TextArea.Wrap
                            padding: 10
                            background: null
                            onTextChanged: {
                                if (text.length > 2000)
                                    text = text.slice(0, 2000)
                            }
                            Keys.onPressed: function(event) {
                                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                                        && (event.modifiers & Qt.ControlModifier)) {
                                    event.accepted = true
                                    if (historyModel.setNoteById(noteEditorDialog.entryId, noteEditorField.text.trim()))
                                        noteEditorDialog.accept()
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Ctrl+Enter 快速保存"
                        color: window.textMuted
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: noteEditorField.text.length + " / 2000"
                        color: noteEditorField.text.length > 1800 ? window.dangerColor : window.textMuted
                        font.pixelSize: 11
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: window.line
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                Layout.topMargin: 12
                Layout.bottomMargin: 14
                spacing: 8

                ToolButton {
                    visible: noteEditorDialog.initialNote.length > 0
                    implicitHeight: 32
                    leftPadding: 10
                    rightPadding: 10
                    contentItem: Text {
                        text: "清空备注"
                        color: parent.hovered ? window.dangerColor : window.textMuted
                        font.pixelSize: 12
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 7
                        color: parent.hovered ? window.dangerSurface : "transparent"
                    }
                    onClicked: {
                        if (historyModel.setNoteById(noteEditorDialog.entryId, ""))
                            noteEditorDialog.accept()
                    }
                }

                Item { Layout.fillWidth: true }

                SettingsActionButton {
                    implicitWidth: 74
                    implicitHeight: 32
                    themeTokens: window
                    text: "取消"
                    onClicked: noteEditorDialog.reject()
                }
                SettingsActionButton {
                    implicitWidth: 78
                    implicitHeight: 32
                    themeTokens: window
                    text: "保存"
                    primaryAction: true
                    onClicked: {
                        if (historyModel.setNoteById(noteEditorDialog.entryId, noteEditorField.text.trim()))
                            noteEditorDialog.accept()
                    }
                }
            }
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
        width: Math.min(window.width - 48, 440)
        property var entryId: 0
        anchors.centerIn: parent
        modal: true
        title: "删除记录？"
        onAccepted: historyModel.removeItemById(entryId)
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        header: Label {
            text: deleteDialog.title
            color: window.textMain
            font.pixelSize: 14
            font.weight: Font.DemiBold
            padding: 16
            bottomPadding: 4
        }
        contentItem: Text {
            text: "此操作会永久删除选中的剪贴板历史记录。"
            color: window.textMuted
            font.pixelSize: 12
            padding: 16
            topPadding: 8
            wrapMode: Text.Wrap
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            SettingsActionButton {
                themeTokens: window
                text: "取消"
                onClicked: deleteDialog.reject()
            }
            SettingsActionButton {
                themeTokens: window
                text: "确认删除"
                primaryAction: true
                onClicked: deleteDialog.accept()
            }
        }
    }

    }

    Loader {
        id: clearDialogLoader
        active: false
        sourceComponent: Dialog {
        id: clearDialog
        parent: window.contentItem
        width: Math.min(window.width - 48, 440)
        anchors.centerIn: parent
        modal: true
        title: "清空全部历史记录？"
        onAccepted: historyModel.clearHistory()
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        header: Label {
            text: clearDialog.title
            color: window.textMain
            font.pixelSize: 14
            font.weight: Font.DemiBold
            padding: 16
            bottomPadding: 4
        }
        contentItem: Text {
            text: "将删除全部历史记录及不再引用的图片文件，此操作不可恢复。"
            color: window.textMuted
            font.pixelSize: 12
            padding: 16
            topPadding: 8
            wrapMode: Text.Wrap
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            SettingsActionButton {
                themeTokens: window
                text: "取消"
                onClicked: clearDialog.reject()
            }
            SettingsActionButton {
                themeTokens: window
                text: "确认清空"
                primaryAction: true
                onClicked: clearDialog.accept()
            }
        }
    }

    }

    Loader {
        id: importDialogLoader
        active: false
        sourceComponent: Dialog {
        id: importDialog
        parent: window.contentItem
        width: Math.min(window.width - 48, 460)
        anchors.centerIn: parent
        modal: true
        title: "导入旧版历史？"
        onAccepted: historyModel.importLegacyDatabase(window.legacyDatabasePath)
        background: Rectangle { color: window.surface; border.color: window.line; radius: 10 }
        header: Label {
            text: importDialog.title
            color: window.textMain
            font.pixelSize: 14
            font.weight: Font.DemiBold
            padding: 16
            bottomPadding: 4
        }
        contentItem: Text {
            text: "程序会读取所选 SQLite 数据库及其同级 images 文件夹，将可识别记录导入当前历史。旧文件不会被修改。重复运行会跳过已导入记录。"
            color: window.textMuted
            font.pixelSize: 12
            padding: 16
            topPadding: 8
            wrapMode: Text.Wrap
        }
        footer: RowLayout {
            spacing: 8
            Item { Layout.fillWidth: true }
            SettingsActionButton {
                themeTokens: window
                text: "取消"
                onClicked: importDialog.reject()
            }
            SettingsActionButton {
                themeTokens: window
                text: "开始导入"
                primaryAction: true
                onClicked: importDialog.accept()
            }
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
                iconText: "window"
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
                Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text { text: "失焦自动隐藏"; color: window.textMain; font.pixelSize: 12; font.weight: Font.Medium }
                        Text {
                            Layout.fillWidth: true
                            text: "点击其他应用窗口时自动隐藏到托盘；开启窗口置顶时不触发。"
                            color: window.textMuted
                            font.pixelSize: 11
                            wrapMode: Text.Wrap
                        }
                    }
                    ThemeCheckBox {
                        id: autoHideOnBlurCheckBox
                        themeTokens: window
                        text: "启用"
                        checked: window.autoHideOnBlur
                        Accessible.name: "窗口失去焦点时自动隐藏到托盘"
                        onToggled: {
                            window.autoHideOnBlur = checked
                            appearanceSettings.sync()
                        }
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                heading: "窗口尺寸"
                description: "Qt 逻辑像素：宽度 " + window.minimumWindowWidth + "–" + window.maximumAllowedWindowWidth
                              + " px，高度 " + window.minimumWindowHeight + "–"
                              + window.maximumAllowedWindowHeight + " px；未锁定时拖动窗口会自动记录尺寸"
                iconText: "resize"
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
                iconText: "palette"
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
                iconText: "keyboard"
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
                            Layout.alignment: Qt.AlignVCenter
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
                            Layout.alignment: Qt.AlignVCenter
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
                    Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3
                            Text { text: "双击记录再复制粘贴"; color: window.textMain; font.pixelSize: 12; font.weight: Font.Medium }
                            Text {
                                Layout.fillWidth: true
                                text: "开启后单击卡片仅选中，双击或按 Enter 才复制粘贴，减少误触。"
                                color: window.textMuted
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                        ThemeCheckBox {
                            id: doubleClickPasteCheckBox
                            themeTokens: window
                            text: "启用"
                            checked: window.doubleClickToPaste
                            Accessible.name: "双击记录再复制粘贴"
                            onToggled: {
                                window.doubleClickToPaste = checked
                                appearanceSettings.sync()
                            }
                        }
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                id: retentionSection
                heading: "定时删除"
                description: "保留近期非收藏记录，收藏始终保留"
                iconText: "clock"
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
                            primaryAction: (retentionEnabled.checked
                                            ? retentionDuration.value * (retentionUnit.currentIndex === 1 ? 24 : 1)
                                            : 0) !== appearanceSettings.retentionHours
                            Accessible.name: text
                            onClicked: {
                                appearanceSettings.retentionHours = retentionEnabled.checked
                                        ? retentionDuration.value * (retentionUnit.currentIndex === 1 ? 24 : 1) : 0
                                appearanceSettings.sync()
                                window.showStatusMessage(appearanceSettings.retentionHours > 0
                                        ? "保留设置已生效，过期的非收藏记录将自动删除。" : "已关闭定时删除，历史将永久保留。")
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
                iconText: "layers"
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
                            primaryAction: (maximumEntriesEnabled.checked ? maximumEntriesValue.value : 0)
                                           !== appearanceSettings.maximumEntries
                            Accessible.name: text
                            onClicked: {
                                appearanceSettings.maximumEntries = maximumEntriesEnabled.checked ? maximumEntriesValue.value : 0
                                appearanceSettings.sync()
                                window.showStatusMessage(appearanceSettings.maximumEntries > 0
                                        ? "数量上限已生效，超限的非收藏记录将自动清理。" : "已关闭数量上限。")
                            }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: window.line }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            spacing: 3
                            Text { text: "清空全部历史记录"; color: window.dangerColor; font.pixelSize: 12; font.weight: Font.Medium }
                            Text {
                                Layout.fillWidth: true
                                text: "立即删除全部剪贴板历史（包含收藏）及不再引用的图片文件。"
                                color: window.textMuted
                                font.pixelSize: 11
                                wrapMode: Text.Wrap
                            }
                        }
                        SettingsActionButton {
                            id: clearAllHistoryButton
                            themeTokens: window
                            text: "清空历史…"
                            Accessible.name: "清空全部历史记录"
                            onClicked: window.openClearDialog()
                        }
                    }
                }
            }

            SettingsSection {
                Layout.fillWidth: true
                heading: "数据存储"
                description: "SQLite 数据库路径与旧版历史迁移"
                iconText: "database"
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
