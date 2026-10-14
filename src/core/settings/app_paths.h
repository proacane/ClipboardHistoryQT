#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QUrl>

namespace ClipboardHistory::Core {

class AppPaths final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentDataDirectory READ currentDataDirectory NOTIFY dataDirectoryChanged)
    Q_PROPERTY(QString pendingDataDirectory READ pendingDataDirectory NOTIFY dataDirectoryChanged)
    Q_PROPERTY(QUrl currentDataDirectoryUrl READ currentDataDirectoryUrl NOTIFY dataDirectoryChanged)
    Q_PROPERTY(QUrl settingsFileUrl READ settingsFileUrl NOTIFY dataDirectoryChanged)
    Q_PROPERTY(QString customThemeFilePath READ customThemeFilePath CONSTANT)

public:
    using QObject::QObject;

    static QString dataDirectory();
    static QString imagesDirectory();
    static QString databaseFilePath();
    static QString configDirectory();
    static QString configFilePath();
    static bool isDataDirectoryWritable();
    static bool applyPendingDataDirectoryChange();

    QString currentDataDirectory() const { return dataDirectory(); }
    QString pendingDataDirectory() const;
    QUrl currentDataDirectoryUrl() const;
    QString customThemeFilePath() const;
    Q_INVOKABLE QString localFilePath(const QUrl &url) const { return url.toLocalFile(); }
    Q_INVOKABLE QVariantMap readThemeFile(const QUrl &url) const;
    Q_INVOKABLE QVariantMap readCustomThemeFile(const QString &legacyJson = {}) const;
    Q_INVOKABLE QVariantMap saveCustomThemeFile(const QString &json) const;
    Q_INVOKABLE QVariantMap readImportedThemeFiles() const;
    Q_INVOKABLE QVariantMap saveImportedThemeFile(const QString &id, const QString &json) const;
    QUrl settingsFileUrl() const;
    Q_INVOKABLE bool setDataDirectory(const QString &path);

signals:
    void dataDirectoryChanged();
};

} // namespace ClipboardHistory::Core
