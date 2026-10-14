#include "../../src/core/history/history_model.h"

#include <QDir>
#include <QDateTime>
#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOperatingSystemVersion>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QSignalSpy>
#include <QSettings>
#include <QSqlQuery>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QtTest>
#include <memory>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

using namespace ClipboardHistory::Core;

#ifndef CLIPBOARDHISTORY_BUILD_TYPE
#define CLIPBOARDHISTORY_BUILD_TYPE "unspecified"
#endif

namespace {
QMutex warningMutex;
QStringList qtWarnings;
QtMessageHandler priorMessageHandler = nullptr;
void captureWarning(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        QMutexLocker lock(&warningMutex);
        qtWarnings.append(message);
    }
    if (priorMessageHandler) priorMessageHandler(type, context, message);
}
QStringList capturedWarnings()
{
    QMutexLocker lock(&warningMutex);
    return qtWarnings;
}
}

// These substitutes expose only QML boundaries; no clipboard, tray, hotkey or bootstrap is touched.
class AcceptancePaths final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentDataDirectory MEMBER directory CONSTANT)
    Q_PROPERTY(QString pendingDataDirectory MEMBER pending NOTIFY pendingDataDirectoryChanged)
    Q_PROPERTY(QUrl currentDataDirectoryUrl READ directoryUrl CONSTANT)
    Q_PROPERTY(QUrl settingsFileUrl READ settingsUrl CONSTANT)
public:
    QString directory;
    QString pending;
    bool failThemeSave = false;
    QUrl directoryUrl() const { return QUrl::fromLocalFile(directory); }
    QUrl settingsUrl() const { return QUrl::fromLocalFile(directory + "/config.ini"); }
    Q_INVOKABLE QString localFilePath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QVariantMap readCustomThemeFile(const QString &) const {
        QFile file(QStringLiteral(CLIPBOARDHISTORY_SOURCE_DIR) + "/resources/custom-theme.json");
        if (!file.open(QIODevice::ReadOnly)) return {{"ok", false}, {"error", "Missing test theme"}};
        return {{"ok", true}, {"json", QString::fromUtf8(file.readAll())}};
    }
    Q_INVOKABLE QVariantMap readImportedThemeFiles() const {
        QVariantList themes;
        const QDir folder(directory + "/themes");
        for (const auto &info : folder.entryInfoList({"*.json"}, QDir::Files, QDir::Name)) {
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly)) continue;
            themes.append(QVariantMap{{"id", info.completeBaseName()},
                                      {"json", QString::fromUtf8(file.readAll())}});
        }
        return {{"ok", true}, {"themes", themes}, {"errors", QStringList{}}};
    }
    Q_INVOKABLE QVariantMap saveImportedThemeFile(const QString &id, const QString &json) const {
        if (failThemeSave) return {{"ok", false}, {"error", "模拟主题写入失败"}};
        QDir().mkpath(directory + "/themes");
        QFile file(directory + "/themes/" + id + ".json");
        if (!file.open(QIODevice::WriteOnly)) return {{"ok", false}, {"error", "Theme test write failed"}};
        const QByteArray bytes = json.toUtf8();
        return {{"ok", file.write(bytes) == bytes.size()}};
    }
    Q_INVOKABLE bool setDataDirectory(const QString &path) { pending = path; emit pendingDataDirectoryChanged(); return true; }
signals:
    void pendingDataDirectoryChanged();
};

class AcceptanceCapture final : public QObject {
    Q_OBJECT
signals:
    void recordFailed(int result);
    void initializationFailed(int status);
};

class AcceptancePlatform final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString globalHotkey READ globalHotkey NOTIFY globalHotkeyChanged)
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
public:
    QString shortcut = QStringLiteral("Ctrl+Shift+V");
    bool startWithWindows = false;
    int hideCalls = 0;
    int quitCalls = 0;
    int autoStartCalls = 0;
    int showCalls = 0;
    bool available = true;
    QString error;
    QHash<QString, QString> rejectedShortcuts;
    QString globalHotkey() const { return shortcut; }
    bool trayAvailable() const { return available; }
    QString lastError() const { return error; }
    Q_INVOKABLE bool setGlobalHotkey(const QString &value) {
        if (rejectedShortcuts.contains(value)) {
            error = rejectedShortcuts.value(value);
            emit lastErrorChanged();
            return false;
        }
        if (shortcut != value) { shortcut = value; emit globalHotkeyChanged(); }
        error.clear();
        return true;
    }
    Q_INVOKABLE void showWindow() { ++showCalls; emit showRequested(); }
    Q_INVOKABLE bool setAutoStartEnabled(bool enabled) {
        ++autoStartCalls;
        startWithWindows = enabled;
        return true;
    }
    Q_INVOKABLE void hideToTray() { ++hideCalls; }
    Q_INVOKABLE void quit() { ++quitCalls; }
    Q_INVOKABLE bool pasteToPreviousWindow() { return true; }
signals:
    void globalHotkeyChanged();
    void trayAvailableChanged();
    void lastErrorChanged();
    void showRequested();
};

class QmlAcceptance final : public QObject {
    Q_OBJECT
private:
    QTemporaryDir directory;
    AcceptancePaths paths;
    AcceptanceCapture capture;
    AcceptancePlatform platform;
    std::unique_ptr<HistoryTaskQueue> queue;
    std::unique_ptr<HistoryModel> model;
    std::unique_ptr<QAbstractItemModelTester> modelTester;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow *window = nullptr;
    QStringList warnings;
    QElapsedTimer startupTimer;
    double startupModelMs = -1;
    const QString fullText = QStringLiteral("Synthetic full text\n") + QString(2000, QLatin1Char('x'));

    QVariant evaluate(const QString &code, QObject *scope = nullptr) {
        QQmlExpression expression(qmlContext(scope ? scope : window), scope ? scope : window, code);
        QVariant result = expression.evaluate();
        if (expression.hasError()) warnings.append(expression.error().toString());
        return result;
    }
    QObject *object(const QString &id) { return qmlContext(window)->objectForName(id); }
    static quint64 residentBytes() {
#ifdef Q_OS_WIN
        PROCESS_MEMORY_COUNTERS memory{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
            return memory.WorkingSetSize;
#endif
        return 0;
    }

private slots:
    void init();
    void cleanup();
    void expandsAndCollapsesFullTextAndImage();
    void changesThemesAndSettings();
    void configuresStartupWithWindows();
    void closeDialogRemembersChoice();
    void capturesPreviousHotkeyWithoutHiding();
    void showsHotkeyFailureAndRestoresPrevious();
    void showsStartupHotkeyFailure_data();
    void showsStartupHotkeyFailure();
    void reportsFailedHotkeyRestore();
    void themePreviewsKeepFixedWidth();
    void importsThemesAsIndependentFiles();
    void retriesLegacyThemeMigrationWithoutDataLoss();
    void categoryContentsAreCentered();
    void mouseWheelStopsAtHistoryBounds();
    void optionalSoak();
    void optionalPerformance();
};

void QmlAcceptance::init()
{
    QVERIFY(directory.isValid());
    paths.directory = directory.path();
    paths.failThemeSave = false;
    QVERIFY(QDir(directory.filePath("themes")).removeRecursively());
    // Each test gets fresh settings even though the temporary directory lives for the test suite.
    QFile::remove(directory.filePath("config.ini"));
    QFile::remove(directory.filePath("history.sqlite3"));
    platform.shortcut = QStringLiteral("Ctrl+Shift+V");
    platform.hideCalls = platform.quitCalls = 0;
    platform.startWithWindows = false;
    platform.autoStartCalls = 0;
    platform.showCalls = 0;
    platform.available = true;
    platform.error.clear();
    platform.rejectedShortcuts.clear();
    warnings.clear();
    {
        QMutexLocker lock(&warningMutex);
        qtWarnings.clear();
    }
    priorMessageHandler = qInstallMessageHandler(captureWarning);
    const int performanceRows = qEnvironmentVariableIntValue("CLIPBOARDHISTORY_ACCEPTANCE_PERF_ROWS");
    if (performanceRows > 0) {
        QVERIFY(performanceRows == 10000 || performanceRows == 100000);
        const QString source = qEnvironmentVariable("CLIPBOARDHISTORY_ACCEPTANCE_SYNTHETIC_DB");
        QVERIFY(QFileInfo(source).isFile());
        QVERIFY2(!QFileInfo::exists(source + "-wal"), "Refuse a raw copy of a live WAL database.");
        QFile sourceFile(source);
        QVERIFY2(sourceFile.copy(directory.filePath("history.sqlite3")), qPrintable(sourceFile.errorString()));
        QVERIFY(QDir().mkpath(directory.filePath("images")));
        const QDir sourceImages(QFileInfo(source).dir().filePath("images"));
        for (const QString &name : sourceImages.entryList({"*.png"}, QDir::Files))
            QVERIFY(QFile::copy(sourceImages.filePath(name), directory.filePath("images/" + name)));
        startupTimer.start();
    } else {
        HistoryService service(directory.filePath("history.sqlite3"), directory.filePath("images"));
        QVERIFY(service.initialize());
        for (int i = 0; i < 20; ++i)
            QVERIFY(service.record({"text", QStringLiteral("synthetic entry-%1").arg(i), {}}).status);
        QImage image(960, 540, QImage::Format_ARGB32);
        image.fill(Qt::cyan);
        QVERIFY(service.record({"image", {}, image}).status);
        QVERIFY(service.record({"text", fullText, {}}).status);
    }
    queue = std::make_unique<HistoryTaskQueue>(directory.filePath("history.sqlite3"), directory.filePath("images"));
    QSignalSpy initialized(queue.get(), &HistoryTaskQueue::initialized);
    QTRY_COMPARE(initialized.count(), 1);
    QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().first()));
    model = std::make_unique<HistoryModel>(queue.get(), directory.filePath("images"));
    if (performanceRows > 0) connect(model.get(), &HistoryModel::loadingChanged, this, [this] {
        if (!model->loading() && startupModelMs < 0)
            startupModelMs = startupTimer.nsecsElapsed() / 1000000.0;
    });
    modelTester = std::make_unique<QAbstractItemModelTester>(model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
    modelTester->setUseFetchMore(false);
    engine = std::make_unique<QQmlApplicationEngine>();
    connect(engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    engine->rootContext()->setContextProperty("appPaths", &paths);
    engine->rootContext()->setContextProperty("clipboardCapture", &capture);
    engine->rootContext()->setContextProperty("windowsPlatform", &platform);
    engine->rootContext()->setContextProperty("historyModel", model.get());
    if (performanceRows > 0)
        engine->loadFromModule("ClipboardHistoryAcceptance", "Main");
    else
        engine->load(QUrl::fromLocalFile(QStringLiteral(CLIPBOARDHISTORY_SOURCE_DIR) + "/src/ui/Main.qml"));
    QVERIFY2(!engine->rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
    window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
    QVERIFY(window);
    connect(&platform, &AcceptancePlatform::showRequested, window, &QWindow::show);
    model->refresh();
    QTRY_COMPARE(model->totalCount(), performanceRows > 0 ? performanceRows : 22);
    QTRY_VERIFY(evaluate("clipList.itemAtIndex(0) !== null").toBool());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void QmlAcceptance::cleanup()
{
    if (window) evaluate("appearanceSettings.sync()");
    engine.reset();
    window = nullptr;
    modelTester.reset(); model.reset();
    queue.reset();
    qInstallMessageHandler(priorMessageHandler);
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    const QStringList allWarnings = capturedWarnings();
    QVERIFY2(allWarnings.isEmpty(), qPrintable(allWarnings.join('\n')));
}

void QmlAcceptance::expandsAndCollapsesFullTextAndImage()
{
    const qint64 textId = model->data(model->index(0, 0), HistoryModel::IdRole).toLongLong();
    QVERIFY(model->data(model->index(0, 0), HistoryModel::ExpandableRole).toBool());
    const double compactHeight = evaluate("clipList.itemAtIndex(0).height").toDouble();
    evaluate(QStringLiteral("window.toggleEntry(%1, '文本')").arg(textId));
    QTRY_COMPARE(window->property("expandedEntryText").toString(), fullText);
    QTRY_VERIFY(evaluate("clipList.itemAtIndex(0).height").toDouble() > compactHeight);
    auto *textDelegate = qvariant_cast<QObject *>(evaluate("clipList.itemAtIndex(0)"));
    QVERIFY(textDelegate);
    QObject *expandedText = qmlContext(textDelegate)->objectForName("expandedText");
    QVERIFY(expandedText);
    QCOMPARE(expandedText->property("text").toString(), fullText);
    QCOMPARE(expandedText->property("textFormat").toInt(), 0); // Text.PlainText
    QObject *collapseButton = qmlContext(textDelegate)->objectForName("expandToggleButton");
    QVERIFY(collapseButton);
    QVERIFY(collapseButton->property("visible").toBool());
    auto *collapseItem = qobject_cast<QQuickItem *>(collapseButton);
    QVERIFY(collapseItem);
    QVERIFY(qAbs(collapseItem->x() + collapseItem->width() / 2 - collapseItem->parentItem()->width() / 2) < 1.0);
    QVERIFY(QMetaObject::invokeMethod(collapseButton, "clicked"));
    QCOMPARE(window->property("expandedEntryId").toLongLong(), 0);
    QTRY_COMPARE(evaluate("clipList.itemAtIndex(0).height").toDouble(), compactHeight);

    const qint64 imageId = model->data(model->index(1, 0), HistoryModel::IdRole).toLongLong();
    evaluate("clipList.positionViewAtIndex(1, ListView.Beginning)");
    auto *imageDelegate = qvariant_cast<QObject *>(evaluate("clipList.itemAtIndex(1)"));
    QVERIFY(imageDelegate);
    QObject *image = qmlContext(imageDelegate)->objectForName("imagePreview");
    QVERIFY(image);
    QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
    QCOMPARE(image->property("fillMode").toInt(), 2); // PreserveAspectCrop
    QCOMPARE(image->property("verticalAlignment").toInt(), int(Qt::AlignTop));
    const double croppedHeight = image->property("height").toDouble();
    QCOMPARE(croppedHeight, 96.0);
    evaluate(QStringLiteral("window.toggleEntry(%1, '图片')").arg(imageId));
    QCOMPARE(image->property("fillMode").toInt(), 1); // PreserveAspectFit
    QTRY_VERIFY(image->property("height").toDouble() > croppedHeight);
    evaluate(QStringLiteral("window.toggleEntry(%1, '图片')").arg(imageId));
    QTRY_COMPARE(image->property("height").toDouble(), croppedHeight);

    auto *imageArea = qobject_cast<QQuickItem *>(qmlContext(imageDelegate)->objectForName("imagePreviewArea"));
    QVERIFY(imageArea);
    QSignalSpy copied(model.get(), &HistoryModel::copyImageReady);
    const QPoint imageCenter = imageArea->mapToScene(QPointF(imageArea->width() / 2, imageArea->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, imageCenter);
    QTRY_COMPARE(copied.count(), 1);
}

void QmlAcceptance::changesThemesAndSettings()
{
    QObject *settingsButton = object("settingsButton");
    QVERIFY(settingsButton);
    QVERIFY(QMetaObject::invokeMethod(settingsButton, "clicked"));
    QVERIFY(window->property("settingsPageVisible").toBool());
    for (const QString &theme : {QStringLiteral("浅色"), QStringLiteral("蓝色"), QStringLiteral("暗色"), QStringLiteral("自定义")}) {
        window->setProperty("themeMode", theme);
        QCOMPARE(evaluate("appearanceSettings.themeMode").toString(), theme);
        QVERIFY(window->property("page").value<QColor>().isValid());
        if (theme == QStringLiteral("自定义"))
            QCOMPARE(window->property("accent").value<QColor>(), QColor(QStringLiteral("#65B7A8")));
    }
    window->setProperty("themeMode", QStringLiteral("蓝色"));
    // QtCore.Settings batches property changes; destroy the engine to exercise shutdown persistence.
    evaluate("appearanceSettings.sync()");
    engine.reset(); window = nullptr;
    QSettings settings(directory.filePath("config.ini"), QSettings::IniFormat);
    QCOMPARE(settings.value("Appearance/themeMode").toString(), QStringLiteral("蓝色"));
}

void QmlAcceptance::configuresStartupWithWindows()
{
    if (QOperatingSystemVersion::currentType() != QOperatingSystemVersion::Windows)
        QSKIP("The startup setting is Windows-specific.");
    window->setProperty("settingsPageVisible", true);
    QObject *checkBox = object("startWithWindowsCheckBox");
    QVERIFY(checkBox);
    QCOMPARE(platform.autoStartCalls, 0);
    QVERIFY(checkBox->setProperty("checked", true));
    QVERIFY(QMetaObject::invokeMethod(checkBox, "clicked"));
    QVERIFY(platform.startWithWindows);
    QCOMPARE(platform.autoStartCalls, 1);
    QCOMPARE(evaluate("appearanceSettings.startWithWindows").toBool(), true);
    evaluate("appearanceSettings.sync()");
    QSettings settings(directory.filePath("config.ini"), QSettings::IniFormat);
    QVERIFY(checkBox->setProperty("checked", false));
    QVERIFY(QMetaObject::invokeMethod(checkBox, "clicked"));
    QVERIFY(!platform.startWithWindows);
    QCOMPARE(platform.autoStartCalls, 2);
    QCOMPARE(evaluate("appearanceSettings.startWithWindows").toBool(), false);

    QVERIFY(checkBox->setProperty("checked", true));
    QVERIFY(QMetaObject::invokeMethod(checkBox, "clicked"));
    QVERIFY(platform.startWithWindows);
    QCOMPARE(platform.autoStartCalls, 3);
    evaluate("appearanceSettings.sync()");
    engine.reset();
    window = nullptr;
    QCOMPARE(settings.value("Appearance/startWithWindows").toBool(), true);
}

void QmlAcceptance::closeDialogRemembersChoice()
{
    if (QOperatingSystemVersion::currentType() != QOperatingSystemVersion::Windows)
        QSKIP("The close-choice UI is Windows-specific.");
    evaluate("window.requestClose()");
    QObject *dialog = object("closeChoiceDialog");
    QVERIFY(dialog);
    QTRY_VERIFY(dialog->property("visible").toBool());
    QObject *remember = object("rememberCloseChoice");
    QVERIFY(remember);
    QVERIFY(remember->setProperty("checked", true));
    // Invoke the real button handler, including persisted preference and platform boundary.
    QObject *exitButton = nullptr;
    for (QObject *child : dialog->findChildren<QObject *>())
        if (child->property("text").toString() == QStringLiteral("直接退出")
            && child->metaObject()->indexOfSignal("clicked()") >= 0) { exitButton = child; break; }
    QVERIFY(exitButton);
    QVERIFY(QMetaObject::invokeMethod(exitButton, "clicked"));
    QCOMPARE(platform.quitCalls, 1);
    QCOMPARE(platform.hideCalls, 0);
    QCOMPARE(evaluate("appearanceSettings.closeAction").toString(), QStringLiteral("exit"));
    evaluate("window.requestClose()");
    QCOMPARE(platform.quitCalls, 2);
    QVERIFY(!dialog->property("visible").toBool());
    evaluate("appearanceSettings.closeAction = 'tray'; window.requestClose()");
    QCOMPARE(platform.hideCalls, 1);
    evaluate("appearanceSettings.closeAction = 'ask'; window.requestClose()");
    QTRY_VERIFY(dialog->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
}

void QmlAcceptance::capturesPreviousHotkeyWithoutHiding()
{
    window->setProperty("settingsPageVisible", true);
    evaluate("window.beginHotkeyCapture()");
    QTRY_VERIFY(window->property("hotkeyCaptureActive").toBool());
    QCOMPARE(platform.shortcut, QString());
    auto *button = qobject_cast<QQuickItem *>(object("hotkeyCaptureButton"));
    QVERIFY(button);
    button->forceActiveFocus();
    QTest::keyClick(window, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(!window->property("hotkeyCaptureActive").toBool());
    QCOMPARE(platform.shortcut, QStringLiteral("Ctrl+Shift+V"));
    QCOMPARE(platform.hideCalls, 0);
    QVERIFY(window->isVisible());
    evaluate("window.beginHotkeyCapture()");
    QTest::keyClick(window, Qt::Key_Escape);
    QTRY_VERIFY(!window->property("hotkeyCaptureActive").toBool());
    QCOMPARE(platform.shortcut, QStringLiteral("Ctrl+Shift+V"));
}

void QmlAcceptance::showsHotkeyFailureAndRestoresPrevious()
{
    window->setProperty("settingsPageVisible", true);
    platform.rejectedShortcuts.insert("Alt+Q", "该组合键已被占用（Windows 错误码 1409）");
    evaluate("window.beginHotkeyCapture()");
    evaluate("window.applyCapturedHotkey(Qt.Key_Q, Qt.AltModifier)");
    QTRY_VERIFY(evaluate("hotkeyErrorDialog.visible").toBool());
    QCOMPARE(platform.shortcut, QStringLiteral("Ctrl+Shift+V"));
    QCOMPARE(evaluate("appearanceSettings.globalHotkey").toString(), platform.shortcut);
    QCOMPARE(evaluate("hotkeyErrorDialog.failedSequence").toString(), QStringLiteral("Alt+Q"));
    QVERIFY(evaluate("hotkeyErrorDialog.failureReason").toString().contains("已被占用"));
    QVERIFY(evaluate("hotkeyErrorDialog.recoveryDetail").toString().contains("继续使用原快捷键"));
    evaluate("changeFailedHotkeyButton.clicked()");
    QTRY_VERIFY(window->property("hotkeyCaptureActive").toBool());
    QVERIFY(window->property("settingsPageVisible").toBool());
    QVERIFY(evaluate("settingsScroll.contentItem.contentY").toDouble() > 0);
    evaluate("window.cancelHotkeyCapture()");
    QCOMPARE(platform.shortcut, QStringLiteral("Ctrl+Shift+V"));
}

void QmlAcceptance::showsStartupHotkeyFailure_data()
{
    QTest::addColumn<QString>("reason");
    QTest::newRow("occupied") << QStringLiteral("该组合键已被其他程序占用（Windows 错误码 1409）");
    QTest::newRow("system-error") << QStringLiteral("拒绝访问（Windows 错误码 5）");
}

void QmlAcceptance::showsStartupHotkeyFailure()
{
    QFETCH(QString, reason);
    platform.shortcut.clear();
    platform.rejectedShortcuts.insert("Ctrl+Shift+V", reason);
    emit platform.trayAvailableChanged();
    // Match main.cpp hiding a startup window after synchronous registration fails.
    window->hide();
    QTRY_VERIFY(evaluate("hotkeyErrorDialog.visible").toBool());
    QVERIFY(window->isVisible());
    QCOMPARE(platform.showCalls, 1);
    QCOMPARE(evaluate("hotkeyErrorDialog.failureReason").toString(), reason);
    QVERIFY(evaluate("hotkeyErrorDialog.recoveryDetail").toString().contains("启动时"));
    QCOMPARE(evaluate("appearanceSettings.globalHotkey").toString(), QStringLiteral("Ctrl+Shift+V"));
    QVERIFY(platform.shortcut.isEmpty());
    for (const auto &theme : {QStringLiteral("暗色"), QStringLiteral("浅色"),
                             QStringLiteral("北极光"), QStringLiteral("自定义")}) {
        window->setProperty("themeMode", theme);
        QCOMPARE(evaluate("hotkeyErrorDialog.background.color").value<QColor>(),
                 window->property("surface").value<QColor>());
    }
}

void QmlAcceptance::reportsFailedHotkeyRestore()
{
    window->setProperty("settingsPageVisible", true);
    evaluate("window.beginHotkeyCapture()");
    platform.rejectedShortcuts.insert("Alt+Q", "新快捷键被占用");
    platform.rejectedShortcuts.insert("Ctrl+Shift+V", "原快捷键也被占用");
    evaluate("window.applyCapturedHotkey(Qt.Key_Q, Qt.AltModifier)");
    QTRY_VERIFY(evaluate("hotkeyErrorDialog.visible").toBool());
    QCOMPARE(evaluate("hotkeyErrorDialog.failureReason").toString(), QStringLiteral("新快捷键被占用"));
    QVERIFY(evaluate("hotkeyErrorDialog.recoveryDetail").toString().contains("原快捷键也被占用"));
    QCOMPARE(evaluate("appearanceSettings.globalHotkey").toString(), QStringLiteral("Ctrl+Shift+V"));
    QVERIFY(platform.shortcut.isEmpty());
    evaluate("hotkeyErrorDialog.close(); window.beginHotkeyCapture(); window.cancelHotkeyCapture()");
    QTRY_VERIFY(evaluate("hotkeyErrorDialog.visible").toBool());
    QCOMPARE(evaluate("hotkeyErrorDialog.failureReason").toString(), QStringLiteral("原快捷键也被占用"));
    QVERIFY(evaluate("hotkeyErrorDialog.recoveryDetail").toString().contains("取消录制后"));
}

void QmlAcceptance::themePreviewsKeepFixedWidth()
{
    window->setProperty("settingsPageVisible", true);
    evaluate("window.width = 720");
    QTRY_COMPARE(evaluate("themePreviewRepeater.itemAt(0).width").toDouble(), 180.0);
    QCOMPARE(evaluate("themePreviewRepeater.itemAt(0).height").toDouble(), 166.0);
    QTRY_VERIFY(evaluate("themePreviewRepeater.itemAt(3).y > 0").toBool());
    evaluate("window.width = window.maximumAllowedWindowWidth");
    QTRY_VERIFY(evaluate("themePreviewRepeater.itemAt(3).y === Math.floor(3 / Math.max(1, Math.floor((themePreviewFlow.width + 8) / 188))) * 174").toBool());
    QCOMPARE(evaluate("themePreviewRepeater.itemAt(0).width").toDouble(), 180.0);
    QCOMPARE(evaluate("themePreviewRepeater.itemAt(0).height").toDouble(), 166.0);
    QCOMPARE(evaluate("themePreviewRepeater.itemAt(1).x - themePreviewRepeater.itemAt(0).x").toDouble(), 188.0);
}

void QmlAcceptance::importsThemesAsIndependentFiles()
{
    QFile source(QStringLiteral(CLIPBOARDHISTORY_SOURCE_DIR) + "/docs/themes/paper-pine.json");
    QVERIFY(source.open(QIODevice::ReadOnly));
    const QString json = QString::fromUtf8(source.readAll());
    const QByteArray quotedArray = QJsonDocument(QJsonArray{json}).toJson(QJsonDocument::Compact);
    const QString quotedJson = QString::fromUtf8(quotedArray).mid(1).chopped(1);
    window->setProperty("pendingImportedTheme", evaluate("window.parseCustomTheme(" + quotedJson + ")"));
    QVERIFY(evaluate("window.addImportedTheme('松林')").toBool());
    const QString firstId = window->property("selectedImportedThemeId").toString();
    const QString filePath = directory.filePath("themes/" + firstId + ".json");
    QFile saved(filePath);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const QByteArray bytes = saved.readAll();
    QCOMPARE(QJsonDocument::fromJson(bytes).object().value("name").toString(), QStringLiteral("松林"));
    evaluate("window.pendingImportedTheme = { palette: window.activeThemePalette }");
    QVERIFY(evaluate("window.addImportedTheme('松林')").toBool());
    QVERIFY(firstId != window->property("selectedImportedThemeId").toString());
    QCOMPARE(QDir(directory.filePath("themes")).entryList({"*.json"}, QDir::Files).size(), 2);
    evaluate("window.importedThemes = []; window.loadImportedThemes()");
    QCOMPARE(evaluate("window.importedThemes.length").toInt(), 2);
    QCOMPARE(window->property("accent").value<QColor>(), QColor("#3C6B52"));
    QCOMPARE(window->property("importedThemesJson").toString(), QStringLiteral("[]"));
    const QString selected = window->property("selectedImportedThemeId").toString();
    paths.failThemeSave = true;
    evaluate("window.pendingImportedTheme = { palette: window.activeThemePalette }");
    QVERIFY(!evaluate("window.addImportedTheme('无法保存')").toBool());
    QCOMPARE(evaluate("window.importedThemes.length").toInt(), 2);
    QCOMPARE(window->property("selectedImportedThemeId").toString(), selected);
    QVERIFY(window->property("themeImportMessage").toString().contains("写入失败"));
}

void QmlAcceptance::retriesLegacyThemeMigrationWithoutDataLoss()
{
    evaluate("window.importedThemesJson = JSON.stringify([{id: 'theme-legacy', name: '旧主题', palette: window.customThemePalette}]); window.selectedImportedThemeId = 'theme-legacy'");
    const QString legacy = window->property("importedThemesJson").toString();
    paths.failThemeSave = true;
    evaluate("window.loadImportedThemes()");
    QCOMPARE(window->property("importedThemesJson").toString(), legacy);
    QCOMPARE(evaluate("window.importedThemes.length").toInt(), 1);
    QCOMPARE(window->property("selectedImportedThemeId").toString(), QStringLiteral("theme-legacy"));
    paths.failThemeSave = false;
    evaluate("window.loadImportedThemes()");
    QVERIFY(QFileInfo::exists(directory.filePath("themes/theme-legacy.json")));
    QCOMPARE(window->property("importedThemesJson").toString(), QStringLiteral("[]"));
    evaluate("window.importedThemes = []; window.loadImportedThemes()");
    QCOMPARE(evaluate("window.importedThemes.length").toInt(), 1);
    QCOMPARE(evaluate("window.importedThemes[0].name").toString(), QStringLiteral("旧主题"));
}

void QmlAcceptance::categoryContentsAreCentered()
{
    for (int index = 0; index < 6; ++index) {
        auto *button = qobject_cast<QQuickItem *>(qvariant_cast<QObject *>(
            evaluate(QStringLiteral("categoryRepeater.itemAt(%1)").arg(index))));
        QVERIFY(button);
        auto *group = qobject_cast<QQuickItem *>(qmlContext(button)->objectForName("categoryContentGroup"));
        QVERIFY(group);
        const auto children = group->childItems();
        QCOMPARE(children.size(), 2);
        QTRY_VERIFY(children[1]->width() > 0);
        const qreal left = qMin(children[0]->x(), children[1]->x());
        const qreal right = qMax(children[0]->x() + children[0]->width(), children[1]->x() + children[1]->width());
        const QPointF center = group->mapToItem(button, QPointF((left + right) / 2, group->height() / 2));
        QVERIFY2(qAbs(center.x() - button->width() / 2) < 0.6, qPrintable(button->property("modelData").toString()));
        QVERIFY(qAbs(center.y() - button->height() / 2) < 0.6);
    }
}

void QmlAcceptance::mouseWheelStopsAtHistoryBounds()
{
    auto *list = qobject_cast<QQuickItem *>(object("clipList"));
    QVERIFY(list);
    const QPointF position = list->mapToScene(QPointF(5, list->height() / 2));
    const auto wheel = [&](int delta) {
        QWheelEvent event(position, window->mapToGlobal(position.toPoint()), {}, QPoint(0, delta),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(window, &event);
        QCoreApplication::processEvents();
    };
    evaluate("clipList.positionViewAtBeginning()");
    QCoreApplication::processEvents();
    const qreal beginning = list->property("contentY").toReal();
    for (int i = 0; i < 4; ++i) wheel(120);
    QTRY_VERIFY(qAbs(list->property("contentY").toReal() - beginning) < 0.5);
    wheel(-120);
    QTRY_VERIFY(list->property("contentY").toReal() > beginning);
    evaluate("clipList.cancelFlick(); clipList.positionViewAtEnd()");
    QCoreApplication::processEvents();
    const qreal end = list->property("contentY").toReal();
    QVERIFY(end > beginning);
    for (int i = 0; i < 4; ++i) wheel(-120);
    QTRY_VERIFY(qAbs(list->property("contentY").toReal() - end) < 0.5);
    evaluate("searchField.text = 'entry-19'; window.refreshFilteredHistory()");
    QTRY_COMPARE(evaluate("clipList.count").toInt(), 1);
    evaluate("clipList.positionViewAtBeginning()");
    QCoreApplication::processEvents();
    const qreal shortListStart = list->property("contentY").toReal();
    wheel(-120);
    wheel(120);
    QTRY_VERIFY(qAbs(list->property("contentY").toReal() - shortListStart) < 0.5);
}

void QmlAcceptance::optionalSoak()
{
    bool validSeconds = false;
    const int seconds = qEnvironmentVariableIntValue("CLIPBOARDHISTORY_ACCEPTANCE_SOAK_SECONDS", &validSeconds);
    if (!validSeconds || seconds <= 0) QSKIP("Opt-in soak: set CLIPBOARDHISTORY_ACCEPTANCE_SOAK_SECONDS and optional synthetic DB path.");
    QVERIFY(seconds <= 3600);
    const QString source = qEnvironmentVariable("CLIPBOARDHISTORY_ACCEPTANCE_SYNTHETIC_DB");
    if (!source.isEmpty()) {
        engine.reset(); window = nullptr; modelTester.reset(); model.reset(); queue.reset();
        const QFileInfo before(source);
        QVERIFY(before.isFile());
        QVERIFY2(!QFileInfo::exists(source + "-wal"), "Refuse a raw copy of a live WAL database.");
        QVERIFY(QFile::remove(directory.filePath("history.sqlite3")));
        QVERIFY(QFile::copy(source, directory.filePath("history.sqlite3")));
        const QFileInfo after(source);
        QVERIFY(!QFileInfo::exists(source + "-wal"));
        QCOMPARE(after.size(), before.size());
        QCOMPARE(after.lastModified(), before.lastModified());
        const QDir sourceImages(before.dir().filePath("images"));
        for (const QString &name : sourceImages.entryList({"*.png"}, QDir::Files)) {
            const QString destination = directory.filePath("images/" + name);
            if (!QFileInfo::exists(destination)) QVERIFY(QFile::copy(sourceImages.filePath(name), destination));
        }
        queue = std::make_unique<HistoryTaskQueue>(directory.filePath("history.sqlite3"), directory.filePath("images"));
        QSignalSpy initialized(queue.get(), &HistoryTaskQueue::initialized);
        QTRY_COMPARE(initialized.count(), 1);
        QVERIFY(qvariant_cast<HistoryServiceStatus>(initialized.first().first()));
        model = std::make_unique<HistoryModel>(queue.get(), directory.filePath("images"));
        modelTester = std::make_unique<QAbstractItemModelTester>(model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        modelTester->setUseFetchMore(false);
        engine = std::make_unique<QQmlApplicationEngine>();
        connect(engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &errors) {
            for (const auto &error : errors) warnings.append(error.toString());
        });
        engine->rootContext()->setContextProperty("appPaths", &paths);
        engine->rootContext()->setContextProperty("clipboardCapture", &capture);
        engine->rootContext()->setContextProperty("windowsPlatform", &platform);
        engine->rootContext()->setContextProperty("historyModel", model.get());
        engine->load(QUrl::fromLocalFile(QStringLiteral(CLIPBOARDHISTORY_SOURCE_DIR) + "/src/ui/Main.qml"));
        QVERIFY(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
        QVERIFY(window);
        model->refresh();
        QTRY_COMPARE_WITH_TIMEOUT(model->totalCount(), 500000, 10000);
    }

    QElapsedTimer timer;
    timer.start();
    const QString startedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    QJsonArray memorySamples;
    QJsonArray searchSamples;
    qint64 sampledSecond = -1;
    qint64 lastSearch = -2000;
    int searchIndex = 0;
    int scrollSteps = 0;
    auto *list = qobject_cast<QQuickItem *>(object("clipList"));
    QVERIFY(list);
    qint64 nextHeartbeatMs = 60000;
    int recordingCycles = 0;
    qint64 lastRecording = -10000;
    QSignalSpy recorded(queue.get(), &HistoryTaskQueue::recordCompleted);
    QSignalSpy copied(queue.get(), &HistoryTaskQueue::copyReady);
    QSignalSpy removed(queue.get(), &HistoryTaskQueue::mutationCompleted);
    const QStringList queries{source.isEmpty() ? "entry-0" : "benchmark-needle",
                             "no-match-acceptance-unique-92719", "",
                             source.isEmpty() ? "synthetic" : "common-token"};
    while (timer.elapsed() < qint64(seconds) * 1000) {
        if (timer.elapsed() - lastRecording >= 10000) {
            lastRecording = timer.elapsed();
            HistoryCapture snapshot{"text", QStringLiteral("Acceptance synthetic cycle %1").arg(recordingCycles), {}};
            if (recordingCycles % 3 == 2) {
                const bool large = recordingCycles % 6 == 5;
                snapshot.kind = "image";
                snapshot.content.clear();
                snapshot.image = QImage(large ? 3840 : 1920, large ? 2160 : 1080, QImage::Format_ARGB32);
                snapshot.image.fill(Qt::cyan);
                snapshot.image.setPixel(0, 0, qRgb(recordingCycles % 255, 1, 2));
            }
            QCOMPARE(queue->submit(snapshot), HistoryTaskQueue::SubmitResult::Accepted);
            QTRY_COMPARE_WITH_TIMEOUT(recorded.count(), 1, 10000);
            const auto result = qvariant_cast<HistoryRecordResult>(recorded.takeFirst().first());
            QVERIFY(result.status);
            QVERIFY(!result.incremented);
            QVERIFY(queue->requestCopy(result.id));
            QTRY_COMPARE_WITH_TIMEOUT(copied.count(), 1, 10000);
            const auto copy = qvariant_cast<HistoryCopyResult>(copied.takeFirst().first());
            QVERIFY(copy.status);
            QCOMPARE(copy.kind, snapshot.kind);
            if (snapshot.image.isNull()) QCOMPARE(copy.content, snapshot.content);
            else { QCOMPARE(copy.image.size(), snapshot.image.size()); QCOMPARE(copy.image.pixel(0, 0), snapshot.image.pixel(0, 0)); }
            QVERIFY(queue->remove(result.id));
            QTRY_COMPARE_WITH_TIMEOUT(removed.count(), 1, 10000);
            const auto mutation = qvariant_cast<HistoryMutationResult>(removed.takeFirst().first());
            QVERIFY(mutation.status);
            QVERIFY(!mutation.cleanupPending);
            ++recordingCycles;
        }
        if (timer.elapsed() - lastSearch >= 2000) {
            lastSearch = timer.elapsed();
            QTRY_VERIFY_WITH_TIMEOUT(!model->loading(), 10000);
            QElapsedTimer queryTimer;
            queryTimer.start();
            double queryElapsedMs = -1;
            const auto loaded = connect(model.get(), &HistoryModel::loadingChanged, this, [&] {
                if (!model->loading()) queryElapsedMs = double(queryTimer.nsecsElapsed()) / 1000000.0;
            });
            const QString query = queries.at(searchIndex++ % queries.size());
            model->setFilter(query, {});
            QTRY_VERIFY_WITH_TIMEOUT(!model->loading(), 10000);
            disconnect(loaded);
            QVERIFY(!model->hasError());
            searchSamples.append(QJsonObject{{"elapsed_ms", double(timer.elapsed())},
                {"query", query}, {"query_executed", queryElapsedMs >= 0}, {"query_to_model_ms", queryElapsedMs},
                {"rows_loaded", model->rowCount()}});
            QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
        }
        const qreal maximumY = qMax(qreal(0), list->property("contentHeight").toReal() - list->height());
        const qreal nextY = qBound(qreal(0), list->property("contentY").toReal() + 96, maximumY);
        QVERIFY(list->setProperty("contentY", nextY));
        ++scrollSteps;
        if (timer.elapsed() / 1000 != sampledSecond) {
            sampledSecond = timer.elapsed() / 1000;
            memorySamples.append(QJsonObject{{"elapsed_ms", double(timer.elapsed())}, {"rss_bytes", double(residentBytes())}});
        }
        if (timer.elapsed() >= nextHeartbeatMs) {
            qInfo("Acceptance progress: elapsed=%lld ms scroll=%d searches=%d cycles=%d rss=%llu bytes warnings=%d",
                  timer.elapsed(), scrollSteps, int(searchSamples.size()), recordingCycles,
                  static_cast<unsigned long long>(residentBytes()), int(capturedWarnings().size()));
            nextHeartbeatMs += 60000;
        }
        QTest::qWait(16);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
    }
    model->setFilter({}, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model->loading(), 10000);
    QTRY_COMPARE(model->totalCount(), source.isEmpty() ? 22 : 500000);
    const bool reuseItems = object("clipList")->property("reuseItems").toBool();
    const int cacheBuffer = object("clipList")->property("cacheBuffer").toInt();
    engine.reset(); window = nullptr; modelTester.reset(); model.reset();
    QSignalSpy stopped(queue.get(), &HistoryTaskQueue::stopped);
    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    queue->shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(stopped.count(), 1, 5000);
    QJsonObject report{{"rendering", QStringLiteral("offscreen software; not desktop frame-performance evidence")},
        {"scroll_driver", QStringLiteral("native QQuickItem property writes; no repeated QQmlExpression construction")},
        {"elapsed_ms", double(timer.elapsed())}, {"source_rows", source.isEmpty() ? 22 : 500000},
        {"scroll_steps", scrollSteps}, {"rss_samples", memorySamples}, {"search_samples", searchSamples},
        {"verified_record_copy_delete_cycles", recordingCycles},
        {"image_fixture", QStringLiteral("solid color plus unique pixel; includes 1080p/4K, not a complex screenshot benchmark")},
        {"shutdown_ms", double(shutdownTimer.nsecsElapsed()) / 1000000.0},
        {"qml_warning_count", warnings.size()}, {"final_row_count_verified", source.isEmpty() ? 22 : 500000}};
    report.insert("reuse_items", reuseItems);
    report.insert("cache_buffer", cacheBuffer);
    report.insert("qt_warning_count", capturedWarnings().size());
    report.insert("qt_warnings", QJsonArray::fromStringList(capturedWarnings()));
    report.insert("test_function_timeout_ms", qEnvironmentVariable("QTEST_FUNCTION_TIMEOUT"));
    report.insert("started_utc", startedUtc);
    report.insert("finished_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    report.insert("qt_version", qVersion());
    report.insert("build_type", QStringLiteral(CLIPBOARDHISTORY_BUILD_TYPE));
#ifdef __VERSION__
    report.insert("compiler", QStringLiteral(__VERSION__));
#elif defined(_MSC_VER)
    report.insert("compiler", QStringLiteral("MSVC %1").arg(_MSC_VER));
#endif
    report.insert("os", QSysInfo::prettyProductName());
    report.insert("cpu_architecture", QSysInfo::currentCpuArchitecture());
    const QString output = qEnvironmentVariable("CLIPBOARDHISTORY_ACCEPTANCE_REPORT");
    if (!output.isEmpty()) {
        QFile file(output);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray json = QJsonDocument(report).toJson();
        QCOMPARE(file.write(json), json.size());
    }
    qInfo("Acceptance soak completed: %d seconds, %d scroll steps, %d searches.", seconds, scrollSteps, int(searchSamples.size()));
}

void QmlAcceptance::optionalPerformance()
{
    const int rows = qEnvironmentVariableIntValue("CLIPBOARDHISTORY_ACCEPTANCE_PERF_ROWS");
    if (rows <= 0) QSKIP("Opt-in performance scenario: provide isolated synthetic fixture and row count.");
    QVERIFY(startupModelMs >= 0);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    QSignalSpy firstFrame(window, &QQuickWindow::frameSwapped);
    window->update();
    QTRY_VERIFY_WITH_TIMEOUT(firstFrame.count() > 0, 5000);
    const double readyMs = startupTimer.nsecsElapsed() / 1000000.0;
    const bool quick = qEnvironmentVariableIsSet("CLIPBOARDHISTORY_ACCEPTANCE_PERF_QUICK");
    QElapsedTimer clock;
    clock.start();
    QVector<double> frames, heartbeats;
    QMutex frameMutex;
    const auto frameConnection = connect(window, &QQuickWindow::frameSwapped, this, [&] {
        QMutexLocker lock(&frameMutex);
        frames.append(clock.nsecsElapsed() / 1000000.0);
    }, Qt::DirectConnection);
    QTimer heartbeat;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(16);
    connect(&heartbeat, &QTimer::timeout, this, [&] { heartbeats.append(clock.nsecsElapsed() / 1000000.0); });
    heartbeat.start();
    const auto cpuMs = [] {
#ifdef Q_OS_WIN
        FILETIME created{}, exited{}, kernel{}, user{};
        if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            ULARGE_INTEGER k{}, u{};
            k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
            u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
            return (k.QuadPart + u.QuadPart) / 10000.0;
        }
#endif
        return -1.0;
    };
    const auto summary = [](QVector<double> values) {
        std::sort(values.begin(), values.end());
        QJsonObject result{{"samples", values.size()}};
        if (values.isEmpty()) return result;
        const auto p = [&](double fraction) { return values.at(std::max(0, int(std::ceil(values.size() * fraction)) - 1)); };
        result.insert("p50_ms", p(.5)); result.insert("p95_ms", p(.95));
        result.insert("max_ms", values.last());
        result.insert("over_33_3_ms", int(std::count_if(values.begin(), values.end(), [](double x) { return x > 33.3; })));
        result.insert("over_100_ms", int(std::count_if(values.begin(), values.end(), [](double x) { return x > 100; })));
        return result;
    };
    const auto intervals = [](const QVector<double> &times, double start, double end) {
        QVector<double> result;
        for (int i = 1; i < times.size(); ++i)
            if (times.at(i - 1) >= start && times.at(i) <= end) result.append(times.at(i) - times.at(i - 1));
        return result;
    };
    QJsonArray searches, phases, resources, ingestion, expansions;
    connect(queue.get(), &HistoryTaskQueue::recordCompleted, model.get(), [this](const HistoryRecordResult &result) {
        if (result.status) model->refresh();
    });
    auto *list = qobject_cast<QQuickItem *>(object("clipList"));
    QVERIFY(list);
    for (int i = 0; i < 60; ++i) {
        const QString query = i % 2 ? "no-match-acceptance-unique-92719" : "benchmark-needle";
        double elapsed = -1;
        QElapsedTimer queryClock; queryClock.start();
        const auto loaded = connect(model.get(), &HistoryModel::loadingChanged, this, [&] {
            if (!model->loading()) elapsed = queryClock.nsecsElapsed() / 1000000.0;
        });
        model->setFilter(query, {});
        QTRY_VERIFY_WITH_TIMEOUT(elapsed >= 0, 10000);
        disconnect(loaded);
        QVERIFY(!model->hasError());
        QVERIFY(i % 2 ? model->rowCount() == 0 : model->rowCount() > 0);
        QCOMPARE(model->totalCount(), rows);
        searches.append(QJsonObject{{"query", query}, {"model_ms", elapsed}, {"rows_loaded", model->rowCount()}});
    }
    model->setFilter({}, {});
    QTRY_COMPARE(model->totalCount(), rows);
    int direction = 1;
    const auto scroll = [&] {
        const double maxY = std::min(12000.0, std::max(0.0, list->property("contentHeight").toDouble() - list->height() - 100.0));
        double next = list->property("contentY").toDouble() + direction * 32;
        if (next >= maxY || next <= 0) direction = -direction;
        list->setProperty("contentY", std::clamp(next, 0.0, maxY));
    };
    const auto runPhase = [&](const QString &name, int duration, bool scrolling, bool toggling) {
        const double start = clock.nsecsElapsed() / 1000000.0;
        const double cpuStart = cpuMs();
        QEventLoop loop;
        QTimer action, sampler;
        action.setTimerType(Qt::PreciseTimer);
        action.setInterval(toggling ? 500 : 16);
        connect(&action, &QTimer::timeout, &loop, [&] {
            if (toggling) { if (window->isVisible()) window->hide(); else window->show(); }
            else if (scrolling) scroll();
        });
        connect(&sampler, &QTimer::timeout, &loop, [&] {
            resources.append(QJsonObject{{"phase", name}, {"elapsed_ms", double(clock.elapsed())},
                {"rss_bytes", double(residentBytes())}, {"cpu_ms", cpuMs()}, {"exposed", window->isExposed()}});
        });
        sampler.start(1000); action.start();
        QTimer::singleShot(duration, &loop, &QEventLoop::quit);
        loop.exec();
        const double end = clock.nsecsElapsed() / 1000000.0;
        QMutexLocker lock(&frameMutex);
        phases.append(QJsonObject{{"name", name}, {"start_ms", start}, {"end_ms", end},
            {"one_core_cpu_percent", (cpuMs() - cpuStart) / (end - start) * 100.0},
            {"swap_intervals", summary(intervals(frames, start + 2000, end - 2000))},
            {"gui_heartbeat", summary(intervals(heartbeats, start + 2000, end - 2000))}});
        qInfo("Performance phase completed: %s, %.0f ms", qPrintable(name), end - start);
    };
    if (!quick) {
        QVERIFY(window->isExposed());
        runPhase("mixed_list_continuous_scroll", 60000, true, false);
        if (rows == 100000) {
            QImage complex(3840, 2160, QImage::Format_RGB32);
            quint32 random = 0xC11F2026;
            for (int y = 0; y < complex.height(); ++y) {
                auto *pixels = reinterpret_cast<QRgb *>(complex.scanLine(y));
                for (int x = 0; x < complex.width(); ++x) {
                    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
                    pixels[x] = qRgb((x / 8 + random) & 255, (y / 8 + (random >> 8)) & 255, (x + y + (random >> 16)) & 255);
                }
            }
            const double ingestStart = clock.nsecsElapsed() / 1000000.0;
            QTimer scrolling;
            scrolling.setTimerType(Qt::PreciseTimer); scrolling.setInterval(16);
            connect(&scrolling, &QTimer::timeout, this, scroll); scrolling.start();
            QSignalSpy recorded(queue.get(), &HistoryTaskQueue::recordCompleted);
            qint64 lastId = 0;
            for (int i = 0; i < 10; ++i) {
                complex.setPixel(0, 0, qRgb(i, 17, 42));
                QElapsedTimer persisted; persisted.start();
                QCOMPARE(queue->submit({"image", {}, complex}), HistoryTaskQueue::SubmitResult::Accepted);
                QTRY_COMPARE_WITH_TIMEOUT(recorded.count(), 1, 20000);
                const auto record = qvariant_cast<HistoryRecordResult>(recorded.takeFirst().first());
                QVERIFY(record.status); QVERIFY(!record.incremented); lastId = record.id;
                ingestion.append(QJsonObject{{"index", i}, {"enqueue_to_commit_upper_bound_ms", persisted.nsecsElapsed() / 1000000.0}});
            }
            scrolling.stop();
            const double ingestEnd = clock.nsecsElapsed() / 1000000.0;
            phases.append(QJsonObject{{"name", "complex_4k_ingestion"},
                {"gui_heartbeat", summary(intervals(heartbeats, ingestStart, ingestEnd))}});
            QSignalSpy copied(queue.get(), &HistoryTaskQueue::copyReady);
            QVERIFY(queue->requestCopy(lastId));
            QTRY_COMPARE_WITH_TIMEOUT(copied.count(), 1, 20000);
            auto copy = qvariant_cast<HistoryCopyResult>(copied.takeFirst().first());
            QVERIFY(copy.status); QCOMPARE(copy.image, complex);
            copy.image = QImage(); complex = QImage(); copied.clear();
            model->refresh();
            QTRY_COMPARE(model->totalCount(), rows + 10);
            QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtBeginning"));
            QTRY_VERIFY(evaluate("clipList.itemAtIndex(0) !== null").toBool());
            const qint64 imageId = model->data(model->index(0, 0), HistoryModel::IdRole).toLongLong();
            for (int i = 0; i < 5; ++i) {
                QElapsedTimer expanded; expanded.start();
                evaluate(QStringLiteral("window.toggleEntry(%1, '图片')").arg(imageId));
                auto *delegate = qvariant_cast<QObject *>(evaluate("clipList.itemAtIndex(0)"));
                QVERIFY(delegate);
                auto *preview = qmlContext(delegate)->objectForName("imagePreview");
                QVERIFY(preview);
                QTRY_COMPARE_WITH_TIMEOUT(preview->property("status").toInt(), 1, 10000);
                QTest::qWait(50);
                expansions.append(expanded.nsecsElapsed() / 1000000.0);
                evaluate(QStringLiteral("window.toggleEntry(%1, '图片')").arg(imageId));
                QTest::qWait(100);
            }
            window->hide();
            runPhase("hidden_window_idle", 120000, false, false);
            runPhase("repeated_show_hide", 60000, false, true);
            window->show();
        }
    }
    heartbeat.stop(); disconnect(frameConnection);
    QVERIFY2(capturedWarnings().isEmpty(), qPrintable(capturedWarnings().join('\n')));
    QJsonArray frameSamples;
    for (double value : frames) frameSamples.append(value);
    QJsonObject report{{"rows", rows}, {"build_type", QStringLiteral(CLIPBOARDHISTORY_BUILD_TYPE)},
        {"qt_version", qVersion()}, {"os", QSysInfo::prettyProductName()},
        {"graphics_api", int(window->rendererInterface()->graphicsApi())},
        {"startup_to_model_ms", startupModelMs}, {"startup_to_usable_render_upper_bound_ms", readyMs},
        {"startup_scope", "Queue initialization + Release compiled QML + first page + exposed frame; excludes process/Qt app initialization and fixture copy; QtTest polling <=50 ms"},
        {"search_scope", "Model filter to completion; excludes 250 ms user input debounce"},
        {"render_scope", "Native exposed QQuickWindow frameSwapped intervals; proxy, not GPU execution/display presentation timings"},
        {"capture_scope", "Ten sequential prebuilt high-entropy synthetic 4K images through actual queue, PNG encoder, repository; no OS clipboard accessed"},
        {"platform_scope", "Real native window hide/show; tray and hotkey remain substitutes"},
        {"searches", searches}, {"phases", phases}, {"resources", resources},
        {"ingestion", ingestion}, {"expansion_upper_bound_ms", expansions},
        {"frame_timestamps_ms", frameSamples}, {"qt_warnings", QJsonArray::fromStringList(capturedWarnings())}};
    const QString output = qEnvironmentVariable("CLIPBOARDHISTORY_ACCEPTANCE_REPORT");
    QVERIFY(!output.isEmpty());
    QFile file(output); QVERIFY(file.open(QIODevice::WriteOnly));
    const auto json = QJsonDocument(report).toJson(); QCOMPARE(file.write(json), json.size());
}

int main(int argc, char **argv)
{
    bool validSoak = false;
    const int seconds = qEnvironmentVariableIntValue("CLIPBOARDHISTORY_ACCEPTANCE_SOAK_SECONDS", &validSoak);
    if (validSoak && seconds > 0 && seconds <= 3600 && qEnvironmentVariableIsEmpty("QTEST_FUNCTION_TIMEOUT"))
        qputenv("QTEST_FUNCTION_TIMEOUT", QByteArray::number((seconds + 60) * 1000));
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen" && qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
#endif
    QQuickStyle::setStyle("Basic");
    qputenv("QSG_RHI_DISABLE_DISK_CACHE", "1");
    QGuiApplication app(argc, argv);
    app.setApplicationName("ClipboardHistoryIsolatedAcceptance");
    app.setOrganizationName("ClipboardHistoryAcceptance");
    QmlAcceptance test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_qml_acceptance.moc"
