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
#include <QSignalSpy>
#include <QSettings>
#include <QSqlQuery>
#include <QStringListModel>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

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
    QUrl directoryUrl() const { return QUrl::fromLocalFile(directory); }
    QUrl settingsUrl() const { return QUrl::fromLocalFile(directory + "/config.ini"); }
    Q_INVOKABLE QString localFilePath(const QUrl &url) const { return url.toLocalFile(); }
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
    int hideCalls = 0;
    int quitCalls = 0;
    QString globalHotkey() const { return shortcut; }
    bool trayAvailable() const { return true; }
    QString lastError() const { return {}; }
    Q_INVOKABLE bool setGlobalHotkey(const QString &value) {
        if (shortcut != value) { shortcut = value; emit globalHotkeyChanged(); }
        return true;
    }
    Q_INVOKABLE void hideToTray() { ++hideCalls; }
    Q_INVOKABLE void quit() { ++quitCalls; }
    Q_INVOKABLE bool pasteToPreviousWindow() { return true; }
signals:
    void globalHotkeyChanged();
    void trayAvailableChanged();
    void lastErrorChanged();
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
    const QString fullText = QStringLiteral("Synthetic full text\n") + QString(2000, QLatin1Char('x'));

    QVariant evaluate(const QString &code, QObject *scope = nullptr) {
        QQmlExpression expression(qmlContext(scope ? scope : window), scope ? scope : window, code);
        QVariant result = expression.evaluate();
        if (expression.hasError()) warnings.append(expression.error().toString());
        return result;
    }
    QObject *object(const QString &id) { return qmlContext(window)->objectForName(id); }
    void configureReuseDiagnostic() {
        const QByteArray value = qgetenv("CLIPBOARDHISTORY_ACCEPTANCE_REUSE_ITEMS");
        if (value == "0" || value == "1") object("clipList")->setProperty("reuseItems", value == "1");
        if (qgetenv("CLIPBOARDHISTORY_ACCEPTANCE_CACHE_BUFFER") == "0") object("clipList")->setProperty("cacheBuffer", 0);
    }
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
    void closeDialogRemembersChoice();
    void capturesPreviousHotkeyWithoutHiding();
    void optionalSoak();
    void delegateResetBaseline();
};

void QmlAcceptance::init()
{
    QVERIFY(directory.isValid());
    paths.directory = directory.path();
    // Each test gets fresh settings even though the temporary directory lives for the test suite.
    QFile::remove(directory.filePath("config.ini"));
    QFile::remove(directory.filePath("history.sqlite3"));
    platform.shortcut = QStringLiteral("Ctrl+Shift+V");
    platform.hideCalls = platform.quitCalls = 0;
    warnings.clear();
    {
        QMutexLocker lock(&warningMutex);
        qtWarnings.clear();
    }
    priorMessageHandler = qInstallMessageHandler(captureWarning);
    if (qgetenv("CLIPBOARDHISTORY_ACCEPTANCE_RESET_BASELINE") == "1"
        && QByteArray(QTest::currentTestFunction()) == "delegateResetBaseline")
        return;
    {
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
    QVERIFY2(!engine->rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
    window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
    QVERIFY(window);
    configureReuseDiagnostic();
    model->refresh();
    QTRY_COMPARE(model->totalCount(), 22);
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
    evaluate(QStringLiteral("window.toggleEntry(%1, '文本')").arg(textId));
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
}

void QmlAcceptance::changesThemesAndSettings()
{
    QObject *settingsButton = object("settingsButton");
    QVERIFY(settingsButton);
    QVERIFY(QMetaObject::invokeMethod(settingsButton, "clicked"));
    QVERIFY(window->property("settingsPageVisible").toBool());
    for (const QString &theme : {QStringLiteral("浅色"), QStringLiteral("蓝色"), QStringLiteral("跟随系统"), QStringLiteral("暗色")}) {
        window->setProperty("themeMode", theme);
        QCOMPARE(evaluate("appearanceSettings.themeMode").toString(), theme);
        QVERIFY(window->property("page").value<QColor>().isValid());
    }
    window->setProperty("themeMode", QStringLiteral("蓝色"));
    // QtCore.Settings batches property changes; destroy the engine to exercise shutdown persistence.
    evaluate("appearanceSettings.sync()");
    engine.reset(); window = nullptr;
    QSettings settings(directory.filePath("config.ini"), QSettings::IniFormat);
    QCOMPARE(settings.value("Appearance/themeMode").toString(), QStringLiteral("蓝色"));
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
        configureReuseDiagnostic();
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
            evaluate("clipList.positionViewAtBeginning()");
        }
        evaluate("clipList.contentY = Math.max(0, Math.min(clipList.contentY + 96, clipList.contentHeight - clipList.height))");
        ++scrollSteps;
        if (timer.elapsed() / 1000 != sampledSecond) {
            sampledSecond = timer.elapsed() / 1000;
            memorySamples.append(QJsonObject{{"elapsed_ms", double(timer.elapsed())}, {"rss_bytes", double(residentBytes())}});
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

void QmlAcceptance::delegateResetBaseline()
{
    if (qgetenv("CLIPBOARDHISTORY_ACCEPTANCE_RESET_BASELINE") != "1")
        QSKIP("Opt-in minimal Qt Window/ListView/QStringListModel reset diagnostic.");
    const int seconds = qEnvironmentVariableIntValue("CLIPBOARDHISTORY_ACCEPTANCE_SOAK_SECONDS");
    QVERIFY(seconds > 0 && seconds <= 3600);
    engine.reset(); window = nullptr; modelTester.reset(); model.reset(); queue.reset();
    QStringList rows;
    for (int i = 0; i < 500; ++i) rows.append(QStringLiteral("Minimal synthetic row %1").arg(i));
    QStringListModel resetModel(rows);
    QAbstractItemModelTester protocol(&resetModel, QAbstractItemModelTester::FailureReportingMode::QtTest);
    protocol.setUseFetchMore(false);
    engine = std::make_unique<QQmlApplicationEngine>();
    connect(engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError> &errors) {
        for (const auto &error : errors) warnings.append(error.toString());
    });
    engine->rootContext()->setContextProperty("resetModel", &resetModel);
    engine->loadData(R"(
        import QtQuick
        Window {
            visible: true; width: 720; height: 620
            ListView {
                id: clipList
                anchors.fill: parent
                model: resetModel
                delegate: Rectangle {
                    required property string display
                    width: ListView.view.width; height: 64
                    Text { text: parent.display; anchors.centerIn: parent; font.pixelSize: 12 }
                }
            }
        }
    )");
    QVERIFY2(!engine->rootObjects().isEmpty(), qPrintable(warnings.join('\n')));
    window = qobject_cast<QQuickWindow *>(engine->rootObjects().first());
    QVERIFY(window);
    configureReuseDiagnostic();
    QElapsedTimer timer;
    timer.start();
    qint64 lastReset = 0;
    int resets = 0;
    const bool removeRows = qgetenv("CLIPBOARDHISTORY_ACCEPTANCE_BASELINE_REMOVE_ROWS") == "1";
    while (timer.elapsed() < qint64(seconds) * 1000) {
        evaluate("clipList.contentY = Math.max(0, Math.min(clipList.contentY + 96, clipList.contentHeight - clipList.height))");
        if (timer.elapsed() - lastReset >= 2000) {
            lastReset = timer.elapsed();
            if (removeRows) QVERIFY(resetModel.removeRows(0, resetModel.rowCount()));
            else resetModel.setStringList({});
            QTest::qWait(300);
            resetModel.setStringList(rows);
            evaluate("clipList.positionViewAtBeginning()");
            ++resets;
        }
        QTest::qWait(16);
    }
    const bool reuse = object("clipList")->property("reuseItems").toBool();
    engine.reset(); window = nullptr;
    QJsonObject report{{"baseline", "Qt Window/ListView/QStringListModel; no application delegates or services"},
        {"elapsed_ms", double(timer.elapsed())}, {"model_reset_cycles", resets}, {"reuse_items", reuse},
        {"clear_notification", removeRows ? "removeRows" : "modelReset"},
        {"qt_warning_count", capturedWarnings().size()}, {"qt_warnings", QJsonArray::fromStringList(capturedWarnings())},
        {"qml_warning_count", warnings.size()}};
    const QString output = qEnvironmentVariable("CLIPBOARDHISTORY_ACCEPTANCE_REPORT");
    if (!output.isEmpty()) {
        QFile file(output);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray json = QJsonDocument(report).toJson();
        QCOMPARE(file.write(json), json.size());
    }
}

int main(int argc, char **argv)
{
    bool validSoak = false;
    const int seconds = qEnvironmentVariableIntValue("CLIPBOARDHISTORY_ACCEPTANCE_SOAK_SECONDS", &validSoak);
    if (validSoak && seconds > 0 && seconds <= 3600 && qEnvironmentVariableIsEmpty("QTEST_FUNCTION_TIMEOUT"))
        qputenv("QTEST_FUNCTION_TIMEOUT", QByteArray::number((seconds + 60) * 1000));
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND")) qputenv("QT_QUICK_BACKEND", "software");
#ifdef Q_OS_WIN
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
#endif
    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);
    app.setApplicationName("ClipboardHistoryIsolatedAcceptance");
    app.setOrganizationName("ClipboardHistoryAcceptance");
    QmlAcceptance test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_qml_acceptance.moc"
