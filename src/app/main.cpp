#include "../core/settings/app_paths.h"
#include "graphics_cache.h"
#include "../core/history/clipboard_capture.h"
#include "../core/history/history_model.h"
#ifdef Q_OS_WIN
#include "../platform/windows/windows_platform_controller.h"
#endif

#include <QGuiApplication>
#include <QClipboard>
#include <QDebug>
#include <QIcon>
#include <QMimeData>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QUrl>
#include <QWindow>

#include <cstdlib>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

void reportDataDirectoryError(const QString &configPath, const QString &dataPath, bool writable)
{
    qCritical() << "Clipboard History data directory startup check failed.";
#ifdef Q_OS_WIN
    const QString message = QStringLiteral(
        "无法写入软件数据目录。\n配置文件：%1\n数据目录：%2\n写入检查：%3")
                                .arg(configPath.isEmpty() ? QStringLiteral("读取失败") : configPath,
                                     dataPath.isEmpty() ? QStringLiteral("未解析") : dataPath,
                                     writable ? QStringLiteral("通过") : QStringLiteral("失败"));
    const std::wstring wideMessage = message.toStdWString();
    MessageBoxW(nullptr, wideMessage.c_str(), L"剪切板历史", MB_OK | MB_ICONERROR);
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_DisableShaderDiskCache);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app-icon.png")));
    QCoreApplication::setApplicationName(QStringLiteral("Clipboard History"));
    QCoreApplication::setOrganizationName(QStringLiteral("ClipboardHistory"));

    const QString initialConfigPath = ClipboardHistory::Core::AppPaths::configFilePath();
    const QString initialDataPath = ClipboardHistory::Core::AppPaths::dataDirectory();
    const bool initialDataWritable = !initialConfigPath.isEmpty()
                                     && ClipboardHistory::Core::AppPaths::isDataDirectoryWritable();
    if (initialConfigPath.isEmpty() || !initialDataWritable) {
        reportDataDirectoryError(initialConfigPath, initialDataPath, initialDataWritable);
        return EXIT_FAILURE;
    }

#ifdef Q_OS_WIN
    const bool launchedAtStartup = app.arguments().contains(QStringLiteral("--startup"));
    app.setQuitOnLastWindowClosed(false);
    ClipboardHistory::Platform::Windows::WindowsPlatformController windowsPlatform;
    const auto instanceResult = windowsPlatform.acquireSingleInstance(
        QStringLiteral("ClipboardHistory"), ClipboardHistory::Core::AppPaths::dataDirectory());
    if (instanceResult == ClipboardHistory::Platform::Windows::WindowsPlatformController::InstanceResult::Forwarded)
        return EXIT_SUCCESS;
    if (instanceResult == ClipboardHistory::Platform::Windows::WindowsPlatformController::InstanceResult::Error) {
        qCritical() << "Clipboard History single-instance setup failed:" << windowsPlatform.lastError();
        MessageBoxW(nullptr, L"无法启动剪切板历史（单实例服务初始化失败）。", L"剪切板历史", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
#endif

    if (!ClipboardHistory::Core::AppPaths::applyPendingDataDirectoryChange()) {
        qWarning() << "Clipboard History could not migrate the pending data directory; continuing with the current directory.";
#ifdef Q_OS_WIN
        MessageBoxW(nullptr, L"数据目录迁移失败，本次仍使用原目录。请检查目标文件夹后在设置中重试。", L"剪切板历史", MB_OK | MB_ICONWARNING);
#endif
    }
    const QString activeConfigPath = ClipboardHistory::Core::AppPaths::configFilePath();
    const QString activeDataPath = ClipboardHistory::Core::AppPaths::dataDirectory();
    const bool activeDataWritable = !activeConfigPath.isEmpty()
                                    && ClipboardHistory::Core::AppPaths::isDataDirectoryWritable();
    if (activeConfigPath.isEmpty() || !activeDataWritable) {
        reportDataDirectoryError(activeConfigPath, activeDataPath, activeDataWritable);
        return EXIT_FAILURE;
    }

    ClipboardHistory::Core::AppPaths appPaths;
    ClipboardHistory::Core::HistoryTaskQueue historyQueue(
        ClipboardHistory::Core::AppPaths::databaseFilePath(),
        ClipboardHistory::Core::AppPaths::imagesDirectory());
    ClipboardHistory::Core::ClipboardCapture clipboardCapture(QGuiApplication::clipboard(), &historyQueue);
    ClipboardHistory::Core::HistoryModel historyModel(
        &historyQueue, ClipboardHistory::Core::AppPaths::imagesDirectory());

    QObject::connect(&historyQueue, &ClipboardHistory::Core::HistoryTaskQueue::initialized,
                     &historyModel, [&historyModel](const auto &status) {
        if (status)
            historyModel.refresh();
    });
    QObject::connect(&historyQueue, &ClipboardHistory::Core::HistoryTaskQueue::recordCompleted,
                     &historyModel, [&historyModel](const auto &result) {
        if (result.status)
            historyModel.refresh();
    });
    QObject::connect(&historyModel, &ClipboardHistory::Core::HistoryModel::copyContentReady,
                     &app, [&app](const QString &kind, const QString &content) {
        QMimeData *mimeData = new QMimeData;
        if (kind == QStringLiteral("path")) {
            QList<QUrl> urls;
            for (const QString &path : content.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                urls.append(QUrl::fromLocalFile(path));
            mimeData->setUrls(urls);
        } else {
            mimeData->setText(content);
        }
        QGuiApplication::clipboard()->setMimeData(mimeData);
    });
    QObject::connect(&historyModel, &ClipboardHistory::Core::HistoryModel::copyImageReady,
                     &app, [](const QImage &image) {
        QGuiApplication::clipboard()->setImage(image);
    });

    const auto graphicsConfiguration = ClipboardHistory::prepareGraphicsCache(activeDataPath);
    QQmlApplicationEngine engine;
    // Configure graphics before showing the QML window and starting its render thread.
    engine.setInitialProperties({{QStringLiteral("visible"), false}});
    engine.rootContext()->setContextProperty(QStringLiteral("appPaths"), &appPaths);
    engine.rootContext()->setContextProperty(QStringLiteral("historyModel"), &historyModel);
    engine.rootContext()->setContextProperty(QStringLiteral("clipboardCapture"), &clipboardCapture);
#ifdef Q_OS_WIN
    engine.rootContext()->setContextProperty(QStringLiteral("windowsPlatform"), &windowsPlatform);
    windowsPlatform.rememberForegroundWindow();
#endif
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("ClipboardHistory"), QStringLiteral("Main"));
    if (!engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        if (window) {
            window->setGraphicsConfiguration(graphicsConfiguration);
            window->show();
        }
    }
#ifdef Q_OS_WIN
    if (!engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst());
        if (!windowsPlatform.setWindow(window)) {
            qWarning() << "Clipboard History system tray is unavailable:" << windowsPlatform.lastError();
            if (launchedAtStartup && window)
                window->show();
        } else if (launchedAtStartup) {
            windowsPlatform.hideToTray();
        }
    }
#endif

    return app.exec();
}
