#include "app_paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>

namespace ClipboardHistory::Core {
namespace {

QString bootstrapFilePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ClipboardHistory.ini"));
}

constexpr qint64 maxThemeBytes = 64 * 1024;

QString configuredDataDirectory()
{
    QSettings settings(bootstrapFilePath(), QSettings::IniFormat);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return {};

    const QString configured = settings.value(QStringLiteral("Storage/dataDirectory")).toString();
    if (!configured.isEmpty())
        return QFileInfo(configured).isAbsolute() ? QDir::cleanPath(configured) : QString();
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
}

QString ensureDirectory(const QString &path)
{
    if (path.isEmpty())
        return {};

    QDir directory(path);
    if (directory.exists() || QDir().mkpath(path))
        return QDir::cleanPath(path);
    return {};
}

bool isWritableDirectory(const QString &path)
{
    QTemporaryFile probe(QDir(path).filePath(QStringLiteral(".clipboardhistory-write-test-XXXXXX")));
    return probe.open();
}

void rollbackCopiedFiles(const QStringList &files, const QStringList &directories)
{
    for (auto it = files.crbegin(); it != files.crend(); ++it)
        QFile::remove(*it);
    for (auto it = directories.crbegin(); it != directories.crend(); ++it)
        QDir().rmdir(*it);
}

bool copyDirectoryContents(const QString &source, const QString &destination,
                           QStringList &copiedFiles, QStringList &createdDirectories)
{
    const QDir sourceDir(source);
    QDirIterator it(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QFileInfo info = it.fileInfo();
        if (info.isSymLink())
            return false;

        const QString relativePath = sourceDir.relativeFilePath(sourcePath);
        const QString destinationPath = QDir(destination).filePath(relativePath);
        if (info.isDir()) {
            if (!QDir().mkpath(destinationPath))
                return false;
            createdDirectories.append(destinationPath);
        } else if (info.isFile()) {
            if (!QFile::copy(sourcePath, destinationPath))
                return false;
            copiedFiles.append(destinationPath);
        }
    }
    return true;
}

} // namespace

QString AppPaths::dataDirectory()
{
    return ensureDirectory(configuredDataDirectory());
}

bool AppPaths::isDataDirectoryWritable()
{
    const QString path = dataDirectory();
    return !path.isEmpty() && isWritableDirectory(path);
}

QString AppPaths::imagesDirectory()
{
    const QString path = dataDirectory();
    return path.isEmpty() ? QString() : ensureDirectory(QDir(path).filePath(QStringLiteral("images")));
}

QString AppPaths::databaseFilePath()
{
    const QString path = dataDirectory();
    return path.isEmpty() ? QString() : QDir(path).filePath(QStringLiteral("history.sqlite3"));
}

QString AppPaths::configDirectory()
{
    return dataDirectory();
}

QString AppPaths::configFilePath()
{
    const QString path = configDirectory();
    return path.isEmpty() ? QString() : QDir(path).filePath(QStringLiteral("config.ini"));
}

QUrl AppPaths::currentDataDirectoryUrl() const
{
    const QString path = currentDataDirectory();
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

QString AppPaths::customThemeFilePath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("custom-theme.json"));
}

QVariantMap AppPaths::readThemeFile(const QUrl &url) const
{
    const auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    if (!url.isLocalFile())
        return failure(QStringLiteral("请选择本地 JSON 主题文件。"));

    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    if (!info.isFile() || info.size() > maxThemeBytes)
        return failure(QStringLiteral("主题文件不存在，或超过 64 KB。"));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return failure(QStringLiteral("无法读取所选主题文件。"));
    const QByteArray contents = file.read(maxThemeBytes + 1);
    if (file.error() != QFileDevice::NoError || contents.size() > maxThemeBytes)
        return failure(QStringLiteral("主题文件读取失败，或超过 64 KB。"));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return failure(QStringLiteral("主题文件不是有效的 JSON 对象。"));

    return {{QStringLiteral("ok"), true}, {QStringLiteral("json"), QString::fromUtf8(contents)}};
}

QVariantMap AppPaths::readCustomThemeFile(const QString &legacyJson) const
{
    const auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    const QString path = customThemeFilePath();
    const QByteArray legacyContents = legacyJson.toUtf8();
    QJsonParseError legacyParseError;
    const QJsonDocument legacyDocument = QJsonDocument::fromJson(legacyContents, &legacyParseError);
    const bool hasValidLegacyTheme = !legacyContents.isEmpty() && legacyContents.size() <= maxThemeBytes
                                     && legacyParseError.error == QJsonParseError::NoError
                                     && legacyDocument.isObject();
    const bool sidecarExists = QFileInfo::exists(path);
    QByteArray defaultContents;
    if (!sidecarExists || hasValidLegacyTheme) {
        QFile seedFile(QStringLiteral(":/themes/custom-theme.json"));
        if (seedFile.open(QIODevice::ReadOnly))
            defaultContents = seedFile.read(maxThemeBytes + 1);
    }
    if (!sidecarExists) {
        QByteArray initialContents = hasValidLegacyTheme ? legacyContents : defaultContents;
        if (initialContents.isEmpty())
            return failure(QStringLiteral("找不到内置自定义主题模板。"));

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(initialContents) != initialContents.size()
            || !file.commit())
            return failure(QStringLiteral("无法在程序目录创建 custom-theme.json。"));
    } else if (hasValidLegacyTheme && !defaultContents.isEmpty()) {
        QFile currentFile(path);
        if (currentFile.open(QIODevice::ReadOnly)
            && currentFile.read(maxThemeBytes + 1) == defaultContents) {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)
                || file.write(legacyContents) != legacyContents.size() || !file.commit())
                return failure(QStringLiteral("无法迁移旧自定义主题到 custom-theme.json。"));
        }
    }
    return readThemeFile(QUrl::fromLocalFile(path));
}

QVariantMap AppPaths::saveCustomThemeFile(const QString &json) const
{
    const QByteArray contents = json.toUtf8();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (contents.isEmpty() || contents.size() > maxThemeBytes
        || parseError.error != QJsonParseError::NoError || !document.isObject())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("主题文件不是有效的 JSON 对象，或超过 64 KB。")}};

    QSaveFile file(customThemeFilePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("无法写入程序目录中的 custom-theme.json。")}};

    return {{QStringLiteral("ok"), true}};
}

QVariantMap AppPaths::readImportedThemeFiles() const
{
    const QDir directory(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("themes")));
    if (QFileInfo::exists(directory.path()) && !QFileInfo(directory.path()).isDir())
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("error"), QStringLiteral("程序目录中的 themes 不是文件夹。")}};

    QVariantList themes;
    QStringList errors;
    for (const auto &info : directory.entryInfoList({QStringLiteral("*.json")},
             QDir::Files | QDir::NoSymLinks, QDir::Name)) {
        const QVariantMap result = readThemeFile(QUrl::fromLocalFile(info.absoluteFilePath()));
        if (!result.value(QStringLiteral("ok")).toBool()) {
            errors.append(info.fileName() + QStringLiteral("：") + result.value(QStringLiteral("error")).toString());
            continue;
        }
        themes.append(QVariantMap{{QStringLiteral("id"), info.completeBaseName()},
                                  {QStringLiteral("json"), result.value(QStringLiteral("json"))}});
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("themes"), themes},
            {QStringLiteral("errors"), errors}};
}

QVariantMap AppPaths::saveImportedThemeFile(const QString &id, const QString &json) const
{
    const auto failure = [](const QString &message) {
        return QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}};
    };
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    if (!validId.match(id).hasMatch())
        return failure(QStringLiteral("主题文件标识无效。"));
    const QByteArray input = json.toUtf8();
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(input, &parseError);
    if (input.isEmpty() || input.size() > maxThemeBytes
        || parseError.error != QJsonParseError::NoError || !document.isObject())
        return failure(QStringLiteral("主题文件不是有效的 JSON 对象，或超过 64 KB。"));
    const QByteArray contents = document.toJson(QJsonDocument::Indented);
    if (contents.size() > maxThemeBytes)
        return failure(QStringLiteral("格式化后的主题文件超过 64 KB。"));
    const QString directory = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("themes"));
    if (!QDir().mkpath(directory))
        return failure(QStringLiteral("无法创建程序目录中的 themes 文件夹，请检查目录写入权限。"));
    const QString path = QDir(directory).filePath(id + QStringLiteral(".json"));
    if (QFileInfo::exists(path)) {
        QFile existing(path);
        if (!QFileInfo(path).isSymLink() && QFileInfo(path).size() <= maxThemeBytes
            && existing.open(QIODevice::ReadOnly)
            && QJsonDocument::fromJson(existing.read(maxThemeBytes + 1)) == document)
            return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}};
        return failure(QStringLiteral("themes 中已有同名且内容不同的文件，未覆盖该文件。"));
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        return failure(QStringLiteral("无法保存到程序目录的 themes 文件夹，请检查目录写入权限或磁盘空间。"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}};
}

QUrl AppPaths::settingsFileUrl() const
{
    const QString path = configFilePath();
    return path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path);
}

bool AppPaths::setDataDirectory(const QString &path)
{
    const QFileInfo requested(path);
    if (path.trimmed().isEmpty() || !requested.isAbsolute())
        return false;

    const QString source = dataDirectory();
    const QString destination = QDir::cleanPath(requested.absoluteFilePath());
    if (source.isEmpty())
        return false;

    QSettings settings(bootstrapFilePath(), QSettings::IniFormat);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return false;

    if (destination == source) {
        settings.remove(QStringLiteral("Storage/pendingDataDirectory"));
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return false;
        emit dataDirectoryChanged();
        return true;
    }

    const QString relativeDestination = QDir::fromNativeSeparators(
        QDir(source).relativeFilePath(destination));
    if (relativeDestination != QStringLiteral(".")
        && relativeDestination != QStringLiteral("..")
        && !relativeDestination.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relativeDestination))
        return false;
    if (!QDir().mkpath(destination) || !isWritableDirectory(destination))
        return false;

    const QDir destinationDir(destination);
    if (!destinationDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                  | QDir::Hidden | QDir::System).isEmpty())
        return false;

    settings.setValue(QStringLiteral("Storage/pendingDataDirectory"), destination);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return false;

    emit dataDirectoryChanged();
    return true;
}

QString AppPaths::pendingDataDirectory() const
{
    QSettings settings(bootstrapFilePath(), QSettings::IniFormat);
    settings.sync();
    return settings.status() == QSettings::NoError
        ? settings.value(QStringLiteral("Storage/pendingDataDirectory")).toString()
        : QString();
}

bool AppPaths::applyPendingDataDirectoryChange()
{
    QSettings settings(bootstrapFilePath(), QSettings::IniFormat);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return false;

    const QString pending = settings.value(QStringLiteral("Storage/pendingDataDirectory")).toString();
    if (pending.isEmpty())
        return true;

    const QFileInfo requested(pending);
    if (!requested.isAbsolute())
        return false;

    const QString source = dataDirectory();
    const QString destination = QDir::cleanPath(requested.absoluteFilePath());
    if (source.isEmpty())
        return false;

    if (destination == source) {
        settings.remove(QStringLiteral("Storage/pendingDataDirectory"));
        settings.sync();
        return settings.status() == QSettings::NoError;
    }

    const QString relativeDestination = QDir::fromNativeSeparators(
        QDir(source).relativeFilePath(destination));
    if (relativeDestination != QStringLiteral(".")
        && relativeDestination != QStringLiteral("..")
        && !relativeDestination.startsWith(QStringLiteral("../"))
        && !QDir::isAbsolutePath(relativeDestination))
        return false;
    if (!QDir().mkpath(destination) || !isWritableDirectory(destination))
        return false;

    const QDir destinationDir(destination);
    if (!destinationDir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot
                                  | QDir::Hidden | QDir::System).isEmpty())
        return false;

    QStringList copiedFiles;
    QStringList createdDirectories;
    if (!copyDirectoryContents(source, destination, copiedFiles, createdDirectories)) {
        rollbackCopiedFiles(copiedFiles, createdDirectories);
        return false;
    }

    settings.setValue(QStringLiteral("Storage/dataDirectory"), destination);
    settings.remove(QStringLiteral("Storage/pendingDataDirectory"));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        rollbackCopiedFiles(copiedFiles, createdDirectories);
        return false;
    }
    return true;
}

} // namespace ClipboardHistory::Core
