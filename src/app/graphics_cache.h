#pragma once

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QQuickGraphicsConfiguration>
#include <cstdlib>

namespace ClipboardHistory {

inline QQuickGraphicsConfiguration prepareGraphicsCache(const QString &dataDirectory)
{
    QQuickGraphicsConfiguration configuration;
    configuration.setAutomaticPipelineCache(false);
    const QDir cache(QDir(dataDirectory).filePath(QStringLiteral("cache")));
    if (QDir().mkpath(cache.path())) {
        const QString qmlCache = cache.filePath(QStringLiteral("qml"));
#ifdef Q_OS_WIN
        _wputenv_s(L"QML_DISK_CACHE_PATH", qmlCache.toStdWString().c_str());
#else
        qputenv("QML_DISK_CACHE_PATH", qmlCache.toUtf8());
#endif
        const QString pipelineCache = cache.filePath(QStringLiteral("pipeline.cache"));
        if (QFileInfo(pipelineCache).isFile())
            configuration.setPipelineCacheLoadFile(pipelineCache);
        configuration.setPipelineCacheSaveFile(pipelineCache);
    } else {
        // Keep compiled QML available without falling back to a system cache directory.
        qputenv("QML_DISK_CACHE", "aot");
        qunsetenv("QML_FORCE_DISK_CACHE");
        qWarning() << "Application cache directory unavailable; runtime disk caching disabled.";
    }
    return configuration;
}

} // namespace ClipboardHistory
